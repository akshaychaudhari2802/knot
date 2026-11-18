// src/explain.cpp
#include "knot/explain.hpp"

#include "knot/analysis.hpp"

#include <string>

namespace knot {

namespace {
struct Text { const char* cause; const char* fix; };

Text chain_text(SinkKind k) {
    switch (k) {
        case SinkKind::Io:
            return {"Likely: an I/O call is made while holding a lock other threads need.",
                    "Do the I/O outside the critical section: copy what you need, unlock, then write."};
        case SinkKind::Sleep:
            return {"Likely: a sleep, retry or back-off inside a critical section.",
                    "Never sleep while holding a lock: release, sleep, re-acquire."};
        case SinkKind::EventLoop:
            return {"Likely: a lock is held across an event-loop wait.",
                    "Release locks before returning to epoll_wait/poll."};
        case SinkKind::CondVar:
            return {"Likely: the lock holder waits on a condition that nobody signals (lost wakeup), "
                    "while still holding a lock others need.",
                    "Check the predicate and every notify path; wait with the lock the condvar protects, not another one."};
        case SinkKind::OrphanWait:
            return {"Likely: the chain ends at a lock whose owner exited without unlocking.",
                    "See the ORPHANED LOCK finding."};
        case SinkKind::CrossWait:
            return {"Likely: contention on a lock shared with another process.",
                    "Inspect the other process with knot as well."};
        case SinkKind::DiskWait:
            return {"Likely: slow storage (disk, NFS, a hung mount) while holding a lock.",
                    "Check the device (iostat, dmesg); avoid file I/O under locks."};
        case SinkKind::Running:
            return {"Likely: a long computation inside a critical section.",
                    "Shrink the critical section, or move the work outside the lock."};
        case SinkKind::Other:
        case SinkKind::Unknown:
            return {"The lock holder is blocked in a syscall Knot does not classify.",
                    "Look at its stack (knot --stacks) to see where it is."};
    }
    return {"", ""};
}
}  // namespace

void explain(Report& r) {
    for (auto& f : r.findings) {
        Text t{"", ""};
        switch (f.type) {
            case FindingType::Deadlock:
                t = {"Two or more threads each hold a lock the next one needs, in a cycle. None can ever proceed.",
                     "Acquire these locks in one global order everywhere, or take them together with std::scoped_lock."};
                break;
            case FindingType::SelfDeadlock:
                t = {"A thread tried to lock a NORMAL (non-recursive) mutex it already holds. It blocks forever.",
                     "Lock once. If re-entry is intended, use std::recursive_mutex deliberately."};
                break;
            case FindingType::Orphaned:
                t = {"The thread that locked this mutex exited without unlocking it: an exception or early "
                     "return between lock() and unlock(), or pthread_exit/pthread_cancel.",
                     "Use std::lock_guard / std::unique_lock, never manual lock()/unlock(). If an owner may "
                     "legitimately die, make the mutex ROBUST: the next locker gets EOWNERDEAD instead of hanging."};
                break;
            case FindingType::CrossProcess:
                t = {"This lock lives in shared memory and is held by a thread of another process.",
                     "Run knot on that process to see what the holder is doing."};
                break;
            case FindingType::Chain: {
                const Text c = chain_text(f.sink_kind);
                t = c;
                break;
