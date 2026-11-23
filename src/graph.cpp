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
                    low[f.node] = std::min(low[f.node], index[w]);
                }
                continue;
            }
            // All edges done: f.node is finished.
            const pid_t v = f.node;
            call.pop_back();
            if (!call.empty()) low[call.back().node] = std::min(low[call.back().node], low[v]);
            if (low[v] == index[v]) {                          // v roots an SCC
                std::vector<pid_t> scc;
                pid_t w;
                do {
                    w = stack.back();
                    stack.pop_back();
                    on_stack.erase(w);
                    scc.push_back(w);
                } while (w != v);
                if (scc.size() >= 2) {
                    std::reverse(scc.begin(), scc.end());
                    result.push_back(std::move(scc));
                }
            }
        }
    }
    return result;
}

std::vector<pid_t> WaitGraph::waiting_on(const std::set<pid_t>& targets) const {
    std::vector<pid_t> result;
    std::set<pid_t> seen(targets.begin(), targets.end());
    std::queue<pid_t> q;
    for (pid_t t : targets) q.push(t);
    while (!q.empty()) {
        const pid_t v = q.front();
        q.pop();
        auto it = in_.find(v);
        if (it == in_.end()) continue;
        for (pid_t w : it->second)
            if (seen.insert(w).second) { result.push_back(w); q.push(w); }
    }
    return result;
}

std::vector<Chain> WaitGraph::chains(const std::set<pid_t>& in_cycle) const {
    std::vector<Chain> result;
    for (pid_t n : nodes_) {
        auto in_it = in_.find(n);
        const bool has_waiters = in_it != in_.end() && !in_it->second.empty();
        const bool waits_itself = out_.count(n) && !out(n).empty();
        if (!has_waiters || waits_itself || in_cycle.count(n)) continue;   // not a sink

        // Reverse BFS from the sink: everyone who eventually waits on it.
        Chain c;
        c.sink = n;
        std::map<pid_t, int> depth{{n, 0}};
        std::queue<pid_t> q;
        q.push(n);
        while (!q.empty()) {
            const pid_t v = q.front();
            q.pop();
            auto it = in_.find(v);
            if (it == in_.end()) continue;
            for (pid_t w : it->second) {
                if (depth.count(w) || in_cycle.count(w)) continue;
                depth[w] = depth[v] + 1;
                c.max_depth = std::max(c.max_depth, depth[w]);
                c.waiters.push_back(w);
                q.push(w);
            }
        }
        result.push_back(std::move(c));
    }
    std::sort(result.begin(), result.end(),
              [](const Chain& a, const Chain& b) { return a.waiters.size() > b.waiters.size(); });
    return result;
}

}  // namespace knot
