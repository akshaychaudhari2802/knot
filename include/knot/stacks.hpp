// knot/stacks.hpp — call stacks as plain data (Tier 1), and the interface Tier 2 implements.
// Grouping and rendering live here so they are unit-testable without libdw.
#pragma once

#include "knot/source.hpp"

#include <sys/types.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace knot {

struct Frame {
    std::uint64_t pc = 0;
    std::string function;   // demangled; empty when there is no symbol
    std::string file;       // from DWARF line info; empty without -g
    int line = 0;
    std::string module;     // "libc.so.6", "order_pipeline"

    // "pthread_mutex_lock (libc.so.6)"  or  "OrderRouter::route  main.cpp:84"
    std::string label(std::size_t max_function = 90) const;
};
using Stack = std::vector<Frame>;

// Implemented by Tier 2 (tier2/dwfl_session.cpp). Live targets: the thread being
// unwound is ptrace-stopped only for the duration of one stack() call.
class StackProvider {
public:
    virtual ~StackProvider() = default;
    virtual std::optional<Stack> stack(pid_t tid, int max_frames = 48) = 0;
    virtual std::string last_error() const = 0;
    virtual double max_pause_ms() const = 0;   // longest single-thread stop so far (live only)
};

// Only defined when Knot is built with libdw (KNOT_TIER2). Throws KnotError.
std::unique_ptr<StackProvider> make_stack_provider(const Source& src);

// 200 threads, 3 distinct stacks -> 3 groups. Largest group first; ties by lowest TID.
struct StackGroup {
    Stack stack;
    std::vector<pid_t> tids;
};
std::vector<StackGroup> group_identical(const std::map<pid_t, Stack>& stacks);
