// knot/syscall.hpp — what is a thread blocked on? (x86-64 syscall numbers)
#pragma once

#include "knot/snapshot.hpp"

#include <cstdint>

namespace knot {

enum class Wait {
    Running,        // on a CPU
    FutexWait,      // mutex, rwlock, condvar, semaphore... decoded later
    FdIo,           // read/write/recv/send/accept/connect on a file descriptor
    Sleep,          // nanosleep / clock_nanosleep
    EventLoopIdle,  // epoll_wait / poll / select: a server at rest looks like this
    Other,          // some other syscall
    Unknown,        // /syscall unreadable (permission) or not in a syscall
};

struct BlockReason {
    Wait kind = Wait::Unknown;
    long nr = -1;
    std::uint64_t futex_addr = 0;   // FutexWait
    int fd = -1;                    // FdIo
};

BlockReason classify(const ThreadSnap& t);
const char* syscall_name(long nr);   // "futex", "read", ... or "syscall_<n>"
const char* to_string(Wait w);

}  // namespace knot
