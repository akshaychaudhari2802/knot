// knot/snapshot.hpp — one point-in-time picture of every thread.
// A plain value type: sampling, watch history and bundles are all "several snapshots".
#pragma once

#include "knot/proc.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace knot {

struct SyscallInfo {
    bool known = false;     // false: /syscall unreadable (permission) or thread gone
    bool running = false;   // "running": on a CPU, not in a syscall
    long nr = -1;           // -1: not in a syscall (e.g. stopped between syscalls)
    std::array<std::uint64_t, 6> args{};
};

struct ThreadSnap {
    pid_t tid = 0;                  // host TID (as seen by Knot)
