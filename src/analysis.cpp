// src/analysis.cpp
#include "knot/analysis.hpp"

#include "knot/explain.hpp"
#include "knot/fd_resolver.hpp"
#include "knot/symbols.hpp"

#include <sched.h>

#include <algorithm>
#include <sstream>
#include <thread>

namespace knot {

int Report::index_of(pid_t tid) const {
    for (std::size_t i = 0; i < snap.threads.size(); ++i)
        if (snap.threads[i].tid == tid) return static_cast<int>(i);
    return -1;
}

std::string pool_key(const std::string& name) {
    auto end = name.find_last_not_of("0123456789");
    if (end == std::string::npos) return name;
    std::string key = name.substr(0, end + 1);
    while (!key.empty() && (key.back() == '-' || key.back() == '_' || key.back() == '#' || key.back() == '/'))
        key.pop_back();
    return key.empty() ? name : key;
}

namespace {

std::string hex(std::uint64_t v) {
    std::ostringstream os;
    os << "0x" << std::hex << v;
    return os.str();
}

// ---------------------------------------------------------------- stage 1 --
// Decode every futex a thread is blocked on, resolve owners, build edges.
void decode_and_build(const Source& src, Report& r, SymbolResolver* symbols) {
    LockDecoder decoder(src);
    Identity identity(r.snap);
    std::map<std::uint64_t, int> by_addr;              // lock object address -> index
    std::map<std::uint64_t, DecodedLock> by_futex;     // decode each futex address ONCE
                                                       // (64 idle workers on one condvar = 1 decode)

    r.threads.resize(r.snap.threads.size());
    for (std::size_t i = 0; i < r.snap.threads.size(); ++i) {
        const ThreadSnap& t = r.snap.threads[i];
        ThreadInfo& ti = r.threads[i];
        ti.why = classify(t);
        if (ti.why.kind != Wait::FutexWait) continue;

        auto cached = by_futex.find(ti.why.futex_addr);
        if (cached == by_futex.end()) cached = by_futex.emplace(ti.why.futex_addr, decoder.decode(ti.why.futex_addr)).first;
        const DecodedLock& d = cached->second;
        if (d.kind == LockKind::CondVarOrUnknown) { ti.condvar_or_unknown_futex = true; continue; }

        auto [it, fresh] = by_addr.try_emplace(d.addr, static_cast<int>(r.locks.size()));
        if (fresh) {
            LockInfo li;
            li.lock = d;
            li.owner = identity.resolve(d.owner_ns_tid, d.process_shared);
            if (symbols) {
                if (auto s = symbols->resolve(d.addr)) li.name = s->offset ? s->name + "+" + std::to_string(s->offset) : s->name;
                li.location = symbols->location(d.addr);
            } else {
                li.location = hex(d.addr);
            }
            r.locks.push_back(std::move(li));
        }
        LockInfo& li = r.locks[static_cast<std::size_t>(it->second)];
        li.waiters.push_back(t.tid);
        (d.reader_wait ? li.reader_waiters : li.writer_waiters)++;
        ti.lock = it->second;

        if (li.owner.status == OwnerStatus::InThisProcess)
            r.graph.add(Edge{t.tid, li.owner.host_tid, li.lock.addr});
    }
}

std::vector<int> locks_held_by(const Report& r, pid_t tid) {
    std::vector<int> out;
    for (std::size_t i = 0; i < r.locks.size(); ++i)
        if (r.locks[i].owner.status == OwnerStatus::InThisProcess && r.locks[i].owner.host_tid == tid)
            out.push_back(static_cast<int>(i));
    return out;
}

// What is the root-cause thread doing? This sentence IS the diagnosis.
void diagnose_sink(const Report& r, pid_t sink, Finding& f) {
    const int idx = r.index_of(sink);
    if (idx < 0) { f.sink_kind = SinkKind::Unknown; f.sink_state = "exited"; return; }
    const ThreadSnap& t = r.snap.threads[static_cast<std::size_t>(idx)];
    const ThreadInfo& ti = r.threads[static_cast<std::size_t>(idx)];
    FdResolver fds(r.snap.pid);
    std::ostringstream os;
    if (t.state == 'D') {
        f.sink_kind = SinkKind::DiskWait;
        os << "in uninterruptible I/O (state D: disk, NFS or a driver) in " << syscall_name(t.sc.nr);
    } else switch (ti.why.kind) {
        case Wait::FdIo:
            f.sink_kind = SinkKind::Io;
            os << "blocked in " << syscall_name(ti.why.nr) << "(fd " << ti.why.fd << ")  ->  "
               << fds.describe(ti.why.fd);
            break;
        case Wait::Sleep:
            f.sink_kind = SinkKind::Sleep;
            os << "sleeping (" << syscall_name(ti.why.nr) << ") while holding the lock";
            break;
        case Wait::EventLoopIdle:
            f.sink_kind = SinkKind::EventLoop;
            os << "waiting for events in " << syscall_name(ti.why.nr) << " while holding the lock";
            break;
        case Wait::FutexWait:
            if (ti.lock >= 0) {
                const LockInfo& l = r.locks[static_cast<std::size_t>(ti.lock)];
                if (l.owner.status == OwnerStatus::Gone) {
                    f.sink_kind = SinkKind::OrphanWait;
                    os << "waiting on " << l.label() << ", whose owner (TID " << l.lock.owner_ns_tid << ") no longer exists";
                } else {
                    f.sink_kind = SinkKind::CrossWait;
                    os << "waiting on " << l.label() << ", held by process " << l.owner.other_pid << " ("
