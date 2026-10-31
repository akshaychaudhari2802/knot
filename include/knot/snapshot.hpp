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
    pid_t ns_tid = 0;               // TID inside the target's PID namespace (what mutexes store)
    std::string name;               // comm
    char state = '?';               // R S D T Z ...
    SyscallInfo sc;
    int policy = 0;                 // SCHED_OTHER / FIFO / RR
    int rt_priority = 0;
    std::uint64_t vol_ctxt = 0;     // voluntary context switches: "has it woken up?"
    std::uint64_t nonvol_ctxt = 0;
    std::uint64_t cpu_ticks = 0;    // utime + stime
};

struct Snapshot {
    pid_t pid = 0;
    std::string process_name;
    std::chrono::steady_clock::time_point taken_at{};
    std::vector<ThreadSnap> threads;
    std::unordered_map<pid_t, pid_t> ns_to_host;   // built from NSpid
    bool in_container = false;                     // NSpid has more than one level
    bool syscalls_denied = false;                  // ATTACH-level access missing (see permission.hpp)

    const ThreadSnap* find(pid_t host_tid) const;
};

// Reads /proc/<pid>. Throws KnotError(NoSuchProcess | PermissionDenied) only
// for whole-process failures; a thread exiting mid-read is skipped silently.
Snapshot take_snapshot(pid_t pid);

// Parses the content of /proc/<pid>/task/<tid>/syscall.
SyscallInfo parse_syscall_file(const std::string& content);

}  // namespace knot
