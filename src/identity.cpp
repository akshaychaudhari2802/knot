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
