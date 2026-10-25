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

