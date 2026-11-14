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
                       << l.owner.other_name << ")";
                }
            } else {
                f.sink_kind = SinkKind::CondVar;
                os << "waiting on a condition variable (or other futex) while holding the lock";
            }
            break;
        case Wait::Running:
            f.sink_kind = SinkKind::Running;
            os << "running on a CPU";
            if (auto it = r.cpu_pct.find(sink); it != r.cpu_pct.end())
                os << " (" << static_cast<int>(it->second + 0.5) << "% CPU)";
            break;
        case Wait::Other:
            f.sink_kind = SinkKind::Other;
            os << "in " << syscall_name(ti.why.nr);
            break;
        case Wait::Unknown:
            f.sink_kind = SinkKind::Unknown;
            os << "state " << t.state << " (syscall unknown)";
            break;
    }
    f.sink_state = os.str();
}

// ---------------------------------------------------------------- stage 2 --
void find_structure(Report& r, int pool_min) {
                       << l.owner.other_name << ")";
                }
            } else {
                f.sink_kind = SinkKind::CondVar;
                os << "waiting on a condition variable (or other futex) while holding the lock";
            }
            break;
        case Wait::Running:
            f.sink_kind = SinkKind::Running;
            os << "running on a CPU";
            if (auto it = r.cpu_pct.find(sink); it != r.cpu_pct.end())
                os << " (" << static_cast<int>(it->second + 0.5) << "% CPU)";
            break;
        case Wait::Other:
            f.sink_kind = SinkKind::Other;
            os << "in " << syscall_name(ti.why.nr);
            break;
        case Wait::Unknown:
            f.sink_kind = SinkKind::Unknown;
            os << "state " << t.state << " (syscall unknown)";
            break;
    }
    f.sink_state = os.str();
}

// ---------------------------------------------------------------- stage 2 --
void find_structure(Report& r, int pool_min) {
    // Deadlocks: every SCC of size >= 2, plus self-loops.
    std::set<pid_t> in_cycle;
    for (auto& cyc : r.graph.cycles()) {
        Finding f;
        f.type = FindingType::Deadlock;
        f.threads = cyc;
        for (pid_t t : cyc) in_cycle.insert(t);
        for (pid_t t : cyc)
            for (const auto& e : r.graph.out(t))
                if (std::find(cyc.begin(), cyc.end(), e.holder) != cyc.end())
                    for (std::size_t i = 0; i < r.locks.size(); ++i)
                        if (r.locks[i].lock.addr == e.lock_addr) f.locks.push_back(static_cast<int>(i));
        f.behind = r.graph.waiting_on(std::set<pid_t>(cyc.begin(), cyc.end()));
        f.persistent = true;   // a cycle can never resolve itself
        r.findings.push_back(std::move(f));
    }
    for (pid_t t : r.graph.self_loops()) {
        Finding f;
        f.type = FindingType::SelfDeadlock;
        f.threads = {t};
        for (const auto& e : r.graph.out(t))
            if (e.holder == t)
                for (std::size_t i = 0; i < r.locks.size(); ++i)
                    if (r.locks[i].lock.addr == e.lock_addr) f.locks.push_back(static_cast<int>(i));
        f.behind = r.graph.waiting_on({t});
        f.persistent = true;
        in_cycle.insert(t);
        r.findings.push_back(std::move(f));
    }

    // Orphaned and cross-process locks: their owners are not nodes in our graph.
    for (std::size_t i = 0; i < r.locks.size(); ++i) {
        const LockInfo& l = r.locks[i];
        if (l.owner.status != OwnerStatus::Gone && l.owner.status != OwnerStatus::InOtherProcess) continue;
        Finding f;
        f.type = l.owner.status == OwnerStatus::Gone ? FindingType::Orphaned : FindingType::CrossProcess;
        f.threads = l.waiters;
        f.locks = {static_cast<int>(i)};
        f.persistent = l.owner.status == OwnerStatus::Gone;   // a dead owner never unlocks
        r.findings.push_back(std::move(f));
    }

    // Chains: everything blocked but not in a cycle, grouped by root cause.
    for (const Chain& c : r.graph.chains(in_cycle)) {
        Finding f;
        f.type = FindingType::Chain;
        f.sink = c.sink;
        f.threads = c.waiters;
        f.depth = c.max_depth;
        f.sink_holds = locks_held_by(r, c.sink);
        diagnose_sink(r, c.sink, f);
        r.findings.push_back(std::move(f));
    }

    // Priority inversion: an RT thread waits on a NON-PI mutex held by a lower-priority thread.
    for (const Edge& e : r.graph.edges()) {
        const ThreadSnap* w = r.thread(e.waiter);
        const ThreadSnap* h = r.thread(e.holder);
        if (!w || !h) continue;
        const bool w_rt = w->policy == SCHED_FIFO || w->policy == SCHED_RR;
        const bool h_rt = h->policy == SCHED_FIFO || h->policy == SCHED_RR;
        if (!w_rt || (h_rt && h->rt_priority >= w->rt_priority)) continue;
        for (std::size_t i = 0; i < r.locks.size(); ++i) {
            const LockInfo& l = r.locks[i];
            if (l.lock.addr != e.lock_addr || l.lock.kind != LockKind::Mutex || l.lock.pi) continue;
            Finding f;
            f.type = FindingType::PriorityInversion;
            f.threads = {e.waiter, e.holder};
            f.locks = {static_cast<int>(i)};
            r.findings.push_back(std::move(f));
        }
    }

    // Pools: "worker-0 .. worker-7" -> "worker". Exhausted when EVERY member is lock-blocked.
    // A core file has no thread names (Knot labels them "tid-N"), so grouping
    // would invent one giant fake pool: skip it.
    if (r.from_core) return;
    std::map<std::string, PoolInfo> pools;
    for (std::size_t i = 0; i < r.snap.threads.size(); ++i) {
        const auto& t = r.snap.threads[i];
        PoolInfo& p = pools[pool_key(t.name)];
        p.name = pool_key(t.name);
        ++p.total;
        const auto& ti = r.threads[i];
        if (ti.lock >= 0) ++p.lock_blocked;
        else if (ti.why.kind == Wait::EventLoopIdle || ti.condvar_or_unknown_futex) ++p.idle;
        else if (ti.why.kind == Wait::Running) ++p.busy;
    }
    for (auto& [name, p] : pools) {
        if (p.total < 2) continue;
        r.pools.push_back(p);
        if (p.total >= pool_min && p.lock_blocked == p.total) {
            Finding f;
            f.type = FindingType::PoolExhausted;
            f.pool = name;
            f.value = p.total;
            r.findings.push_back(std::move(f));
        }
    }
}

// ---------------------------------------------------------------- verdict --
void decide(Report& r) {
    auto has = [&](FindingType t) {
        return std::any_of(r.findings.begin(), r.findings.end(), [&](const Finding& f) { return f.type == t; });
    };
    auto count_waiters = [&](FindingType t) {
        std::size_t n = 0;
        for (const auto& f : r.findings) if (f.type == t) n += f.threads.size() + f.behind.size();
        return n;
    };
    Verdict& v = r.verdict;
    std::ostringstream os;
    if (r.snap.syscalls_denied) {
        v = {VerdictKind::Indeterminate, "cannot read thread syscalls (ptrace access needed)", 3};
    } else if (has(FindingType::Deadlock)) {
        std::size_t cycles = 0, members = 0;
        for (const auto& f : r.findings) if (f.type == FindingType::Deadlock) { ++cycles; members += f.threads.size(); }
        os << "DEADLOCK: " << members << " threads in " << cycles << (cycles == 1 ? " lock cycle" : " lock cycles");
        v = {VerdictKind::Deadlock, os.str(), 1};
    } else if (has(FindingType::SelfDeadlock)) {
        v = {VerdictKind::SelfDeadlock, "SELF-DEADLOCK: a thread waits for a lock it already holds", 1};
    } else if (has(FindingType::Orphaned)) {
        os << "ORPHANED LOCK: " << count_waiters(FindingType::Orphaned)
           << " thread(s) wait on a lock whose owner no longer exists";
        v = {VerdictKind::Orphaned, os.str(), 2};
    } else if (has(FindingType::Chain)) {
        // Headline chain: a PERSISTENT one if any exists (a hang anywhere is a hang,
        // even if a bigger chain is merely contended), else the one with most waiters.
        const Finding* top = nullptr;
        for (const auto& f : r.findings) {
            if (f.type != FindingType::Chain) continue;
            if (!top || (f.persistent && !top->persistent) ||
                (f.persistent == top->persistent && f.threads.size() > top->threads.size()))
                top = &f;
        }
        const bool stuck = r.samples > 1 && top->persistent;
        if (stuck) {
            os << "HUNG, not a deadlock. " << top->threads.size() << " thread(s) are waiting on one thread.";
            v = {VerdictKind::Hung, os.str(), 2};
        } else if (r.samples > 1) {
            os << "CONTENDED: threads keep waiting on locks, but work is still moving.";
            v = {VerdictKind::Contended, os.str(), 2};
        } else {
            os << "BLOCKED right now: " << top->threads.size() << " thread(s) waiting on one thread. "
               << "Re-run without --once to tell a hang from contention.";
            v = {VerdictKind::Blocked, os.str(), 2};
        }
    } else if (has(FindingType::CrossProcess) || has(FindingType::PriorityInversion) || has(FindingType::Busy) ||
               has(FindingType::HotLock) || has(FindingType::Livelock)) {
        v = {VerdictKind::Attention, "No hang, but see the findings below.", 2};
    } else {
        v = {VerdictKind::Healthy, "HEALTHY: no deadlock, no stuck lock chain.", 0};
    }
}

// Tier 2: one stack per thread. Live targets: each thread is stopped only while
// its own stack is walked (budget: < 50 ms per thread).
void collect_stacks(Report& r, StackProvider* sp) {
    if (!sp) return;
    for (const auto& t : r.snap.threads) {
        if (auto st = sp->stack(t.tid)) r.stacks[t.tid] = std::move(*st);
        else if (r.stacks_note.empty()) r.stacks_note = "T" + std::to_string(t.tid) + ": " + sp->last_error();
    }
    r.max_pause_ms = sp->max_pause_ms();
}

}  // namespace

Report analyze_snapshot(const Source& src, Snapshot snap, SymbolResolver* symbols, int pool_min_size) {
    const auto t0 = std::chrono::steady_clock::now();
    Report r;
    r.snap = std::move(snap);
    r.from_core = !src.is_live();
    decode_and_build(src, r, symbols);
    find_structure(r, pool_min_size);
    decide(r);
    explain(r);
    r.elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0);
    return r;
}

// ---------------------------------------------------------------- sampling --
// One snapshot can't tell STUCK from SLOW. N snapshots can:
//   - a thread whose voluntary context switches did not change, sitting in the
//     same syscall with the same arguments, has not woken up once: stuck
//   - a chain whose sink is stuck in EVERY sample is a hang
//   - locks that have waiters in some samples but not others are contention
Report analyze(const Source& src, const AnalyzeOptions& opt, SymbolResolver* symbols) {
    const auto t0 = std::chrono::steady_clock::now();
    const int n = std::max(1, opt.samples);
    std::vector<Report> reps;
    std::vector<std::map<pid_t, std::string>> tops;   // Tier 2: per sample, top function of each RUNNING thread
    for (int i = 0; i < n; ++i) {
        if (i > 0) std::this_thread::sleep_for(opt.interval);
        reps.push_back(analyze_snapshot(src, src.snapshot(), symbols, opt.pool_min_size));
        if (opt.stacks && src.is_live()) {
            auto& top = tops.emplace_back();
            for (const auto& t : reps.back().snap.threads)
                if (t.sc.running)
                    if (auto st = opt.stacks->stack(t.tid, 8)) top[t.tid] = top_function(*st);
        }
        if (!src.is_live()) break;   // a core file never changes
    }
    // COPY, not move: the loops below still read every sample, including the
    // last. (A move here left reps.back() empty and silently broke persistence.)
    Report r = reps.back();
    r.samples = static_cast<int>(reps.size());
    if (r.samples == 1) {
        collect_stacks(r, opt.stacks);
        r.elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0);
        return r;
    }

    const Snapshot& first = reps.front().snap;
    r.window_s = std::chrono::duration<double>(r.snap.taken_at - first.taken_at).count();
    const double hz = static_cast<double>(proc::clock_ticks_per_second());

    for (const auto& t : r.snap.threads) {
        const ThreadSnap* f = first.find(t.tid);
        if (!f) continue;
        if (r.window_s > 0) r.cpu_pct[t.tid] = 100.0 * static_cast<double>(t.cpu_ticks - f->cpu_ticks) / hz / r.window_s;
        if (!t.sc.running && t.sc.known && t.sc.nr >= 0 && t.vol_ctxt == f->vol_ctxt &&
            t.sc.nr == f->sc.nr && t.sc.args == f->sc.args)
            r.no_wakeups.insert(t.tid);
    }

    // A chain is persistent when its sink was a sink in EVERY sample and never woke up.
    for (auto& f : r.findings) {
        if (f.type != FindingType::Chain) continue;
        bool everywhere = true;
        for (const auto& rep : reps) {
            const bool seen = std::any_of(rep.findings.begin(), rep.findings.end(),
                                          [&](const Finding& g) { return g.type == FindingType::Chain && g.sink == f.sink; });
            everywhere = everywhere && seen;
        }
        f.persistent = everywhere && r.no_wakeups.count(f.sink);
        if (f.sink_kind == SinkKind::Running) diagnose_sink(r, f.sink, f);   // now with CPU %
    }

    // Locks that ARE the hang (cycle, orphan, other-process owner, held by a hung
    // sink) are explained by that finding. Calling them "hot ... not a hang" as
    // well contradicted the verdict (seen on the order_pipeline faults), so they
