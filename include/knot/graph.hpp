// knot/graph.hpp — the wait-for graph. Edge W -> H: "W waits for a lock H holds".
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace knot {

struct Edge {
    pid_t waiter = 0;
    pid_t holder = 0;
    std::uint64_t lock_addr = 0;
};

struct Chain {
    pid_t sink = 0;                  // holds a lock others need, waits for no lock itself
    std::vector<pid_t> waiters;      // everyone who (transitively) waits on the sink
    int max_depth = 0;               // longest waiter -> sink path, in edges
};

class WaitGraph {
public:
    void add(const Edge& e);

    // Tarjan's SCC, iterative. Every SCC of size >= 2 is a deadlock cycle,
    // including several independent ones in the same process.
    std::vector<std::vector<pid_t>> cycles() const;
    std::vector<pid_t> self_loops() const;   // a thread waiting on a lock it holds

    // For nodes NOT in a cycle: group every blocked thread under the sink it
    // ultimately waits on. Sorted by waiter count, largest first.
    std::vector<Chain> chains(const std::set<pid_t>& in_cycle) const;

    // Everyone (outside `targets`) who transitively waits on any node in `targets`,
    // e.g. the threads stuck BEHIND a deadlock cycle.
    std::vector<pid_t> waiting_on(const std::set<pid_t>& targets) const;

    const std::vector<Edge>& out(pid_t n) const;
    std::vector<Edge> edges() const;
    bool empty() const noexcept { return out_.empty(); }

private:
    std::map<pid_t, std::vector<Edge>> out_;   // std::map: deterministic output order
    std::map<pid_t, std::vector<pid_t>> in_;
    std::set<pid_t> nodes_;
};

}  // namespace knot
