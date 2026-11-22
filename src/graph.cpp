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
// thousand-thread chain can't overflow Knot's own stack.
std::vector<std::vector<pid_t>> WaitGraph::cycles() const {
    std::map<pid_t, int> index, low;
    std::set<pid_t> on_stack;
    std::vector<pid_t> stack;
    std::vector<std::vector<pid_t>> result;
    int next = 0;

    struct Frame { pid_t node; std::size_t edge; };
    for (pid_t root : nodes_) {
        if (index.count(root)) continue;
        std::vector<Frame> call{{root, 0}};
        index[root] = low[root] = next++;
        stack.push_back(root);
        on_stack.insert(root);

        while (!call.empty()) {
            Frame& f = call.back();
            const auto& es = out(f.node);
            if (f.edge < es.size()) {
                const pid_t w = es[f.edge++].holder;
                if (!index.count(w)) {                       // tree edge: "recurse"
                    index[w] = low[w] = next++;
                    stack.push_back(w);
                    on_stack.insert(w);
                    call.push_back({w, 0});
                } else if (on_stack.count(w)) {
