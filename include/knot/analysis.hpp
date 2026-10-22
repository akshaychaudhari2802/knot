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
