// src/analysis.cpp
#include "knot/analysis.hpp"

#include "knot/explain.hpp"
#include "knot/fd_resolver.hpp"
#include "knot/symbols.hpp"

#include <sched.h>

#include <algorithm>
#include <sstream>
#include <thread>

namespace knot {

int Report::index_of(pid_t tid) const {
    for (std::size_t i = 0; i < snap.threads.size(); ++i)
        if (snap.threads[i].tid == tid) return static_cast<int>(i);
    return -1;
}

std::string pool_key(const std::string& name) {
    auto end = name.find_last_not_of("0123456789");
    if (end == std::string::npos) return name;
    std::string key = name.substr(0, end + 1);
    while (!key.empty() && (key.back() == '-' || key.back() == '_' || key.back() == '#' || key.back() == '/'))
        key.pop_back();
    return key.empty() ? name : key;
}

namespace {

std::string hex(std::uint64_t v) {
    std::ostringstream os;
    os << "0x" << std::hex << v;
    return os.str();
}

// ---------------------------------------------------------------- stage 1 --
// Decode every futex a thread is blocked on, resolve owners, build edges.
void decode_and_build(const Source& src, Report& r, SymbolResolver* symbols) {
    LockDecoder decoder(src);
    Identity identity(r.snap);
