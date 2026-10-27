// knot/identity.hpp — who is a lock's owner, really?
//
// A mutex stores its owner's TID AS THE TARGET SEES IT. Inside a container
// that is the container's numbering (12), not the host's (48223). Every owner
// resolves to exactly one of four outcomes.
#pragma once

#include "knot/snapshot.hpp"

#include <string>

namespace knot {

enum class OwnerStatus {
    Unlocked,        // owner field 0
    InThisProcess,   // normal wait-for edge
    InOtherProcess,  // process-shared lock in shared memory, held elsewhere
    Gone,            // ORPHANED: the owner thread no longer exists anywhere
};

struct Owner {
    OwnerStatus status = OwnerStatus::Unlocked;
    pid_t host_tid = 0;          // InThisProcess / InOtherProcess
    pid_t other_pid = 0;         // InOtherProcess
    std::string other_name;      // InOtherProcess: comm of that process
    std::string other_thread;    // InOtherProcess: comm of that thread
};

class Identity {
public:
    explicit Identity(const Snapshot& s) : snap_(s) {}
    // `process_shared`: only a PTHREAD_PROCESS_SHARED lock can be held by another
    // process. For a private lock, an owner missing from this process is GONE,
    // even if its TID now exists elsewhere (the kernel reuses TIDs).
    Owner resolve(pid_t owner_ns_tid, bool process_shared = true) const;

private:
    const Snapshot& snap_;
};

const char* to_string(OwnerStatus s);

}  // namespace knot
