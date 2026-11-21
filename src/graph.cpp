// src/graph.cpp
#include "knot/graph.hpp"

#include <algorithm>
#include <functional>
#include <queue>

namespace knot {

void WaitGraph::add(const Edge& e) {
    out_[e.waiter].push_back(e);
    in_[e.holder].push_back(e.waiter);
    nodes_.insert(e.waiter);
    nodes_.insert(e.holder);
}

const std::vector<Edge>& WaitGraph::out(pid_t n) const {
    static const std::vector<Edge> none;
    auto it = out_.find(n);
    return it == out_.end() ? none : it->second;
}

std::vector<Edge> WaitGraph::edges() const {
    std::vector<Edge> all;
    for (const auto& [n, es] : out_) all.insert(all.end(), es.begin(), es.end());
    return all;
}

std::vector<pid_t> WaitGraph::self_loops() const {
    std::vector<pid_t> out;
    for (const auto& [n, es] : out_)
        for (const auto& e : es)
            if (e.holder == n) { out.push_back(n); break; }
    return out;
}

// Iterative Tarjan: an explicit stack instead of recursion, so a pathological
