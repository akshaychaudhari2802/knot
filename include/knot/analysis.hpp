// knot/analysis.hpp — snapshot(s) in, findings and a verdict out.
#pragma once

#include "knot/graph.hpp"
#include "knot/identity.hpp"
#include "knot/lock.hpp"
#include "knot/snapshot.hpp"
#include "knot/source.hpp"
#include "knot/stacks.hpp"
#include "knot/syscall.hpp"

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace knot {

class SymbolResolver;

struct LockInfo {
    DecodedLock lock;
    Owner owner;
    std::string name;       // "g_batch_mu" when the symbol table knows it
    std::string location;   // "order_pipeline+0x5e1a0" or "heap/anon 0x..."
    std::vector<pid_t> waiters;
    int reader_waiters = 0, writer_waiters = 0;

    std::string label() const { return name.empty() ? location : name; }
};

struct ThreadInfo {
    BlockReason why;
    int lock = -1;             // index into Report::locks when blocked on a decoded lock
    bool condvar_or_unknown_futex = false;
};

enum class FindingType { Deadlock, SelfDeadlock, Orphaned, CrossProcess, Chain,
                         PriorityInversion, PoolExhausted, HotLock, Busy,
                         Livelock };   // Tier 2: Busy + the same top function in every sample

enum class SinkKind { Io, Sleep, EventLoop, CondVar, OrphanWait, CrossWait, DiskWait, Running, Other, Unknown };

struct Finding {
    FindingType type;
    std::vector<pid_t> threads;     // cycle members; or waiters; or the single subject
    std::vector<int> locks;         // indices into Report::locks
    std::vector<pid_t> behind;      // threads stuck behind a deadlock cycle
    pid_t sink = 0;                 // Chain: the root-cause thread
    SinkKind sink_kind = SinkKind::Unknown;
    std::string sink_state;         // "blocked in sendto(fd 12) -> TCP ... (postgres port)"
    std::vector<int> sink_holds;    // locks the sink holds
    int depth = 0;
    std::string pool;               // PoolExhausted
    double value = 0;               // Busy: CPU %; HotLock: average waiters
    bool persistent = false;        // sampling: present in every sample
    // Filled by the explain engine:
    std::string likely_cause, fix_hint;
};

struct PoolInfo {
    std::string name;
