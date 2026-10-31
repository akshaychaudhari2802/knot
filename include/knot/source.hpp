// knot/source.hpp — where bytes and threads come from: a live process or a core file.
// Everything above this interface (lock decoding, graph, analysis, report) is
// written ONCE and runs on either.
#pragma once

#include "knot/snapshot.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace knot {

struct Mapping {
    std::uint64_t start = 0, end = 0;   // [start, end)
    std::uint64_t offset = 0;           // file offset of `start`
    std::string path;                   // empty for anonymous mappings
    bool writable = false;
    bool shared = false;                // MAP_SHARED ('s' in /proc/<pid>/maps): where process-shared locks live
};

class Source {
