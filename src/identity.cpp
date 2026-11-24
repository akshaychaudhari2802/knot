// src/identity.cpp
#include "knot/identity.hpp"

#include <sstream>

namespace knot {

namespace {
// The innermost NSpid entry of /proc/<pid>/task/<tid>/status.
pid_t innermost_nspid(const std::string& status) {
    auto ns = proc::status_field(status, "NSpid");
    if (!ns) return 0;
    std::istringstream in(*ns);
    pid_t last = 0;
    for (long v; in >> v;) last = static_cast<pid_t>(v);
    return last;
}
}  // namespace

Owner Identity::resolve(pid_t owner_ns_tid, bool process_shared) const {
    Owner o;
    if (owner_ns_tid <= 0) return o;

    if (auto it = snap_.ns_to_host.find(owner_ns_tid); it != snap_.ns_to_host.end()) {
        o.status = OwnerStatus::InThisProcess;
        o.host_tid = it->second;
        return o;
    }
    // A private lock lives in this process's memory: no other process can own it.
    // Searching anyway would blame an unrelated process that happens to have been
    // given the dead owner's TID (found while documenting the design).
    if (!process_shared) {
        o.status = OwnerStatus::Gone;
        return o;
    }

    // Not one of ours. Does that TID exist in some OTHER process of the same
    // PID namespace? (A process-shared mutex in shared memory, e.g. Linkway SHM.)
    const auto our_ns = proc::read_link(proc::path(snap_.pid, "ns/pid"));
    if (!snap_.in_container) {
        // Same namespace as Knot: the TID is directly visible as /proc/<tid>.
        if (auto st = proc::read_file("/proc/" + std::to_string(owner_ns_tid) + "/status")) {
            o.status = OwnerStatus::InOtherProcess;
            o.host_tid = owner_ns_tid;
            if (auto tg = proc::status_field(*st, "Tgid")) o.other_pid = static_cast<pid_t>(std::stol(*tg));
            if (auto nm = proc::status_field(*st, "Name")) o.other_thread = *nm;
        }
    } else {
        // Different namespace: scan processes in the target's namespace and match
        // on the innermost NSpid. Expensive, but only runs for suspect locks.
        for (pid_t p : proc::list_numeric_dir("/proc")) {
            if (p == snap_.pid || proc::read_link(proc::path(p, "ns/pid")) != our_ns) continue;
            for (pid_t t : proc::list_numeric_dir(proc::path(p, "task"))) {
                auto st = proc::read_file(proc::task_path(p, t, "status"));
                if (!st || innermost_nspid(*st) != owner_ns_tid) continue;
                o.status = OwnerStatus::InOtherProcess;
                o.host_tid = t;
