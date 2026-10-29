// knot/permission.hpp — explain an access failure, with the exact fix.
// gdb says "ptrace: Operation not permitted". Knot says why, and what to run.
#pragma once

#include <sys/types.h>

#include <string>

namespace knot {

struct AccessReport {
    bool can_read_syscalls = false;   // PTRACE_MODE_ATTACH: /proc/<pid>/syscall and /mem
    int yama_scope = -1;              // -1: Yama not present
    bool same_uid = false;
    bool have_cap_sys_ptrace = false;
    bool target_dumpable = true;
    bool in_container = false;
    std::string explanation;          // empty when access is fine
};

AccessReport check_access(pid_t pid);

}  // namespace knot
