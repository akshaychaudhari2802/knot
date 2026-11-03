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
