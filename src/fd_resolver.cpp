// src/fd_resolver.cpp
#include "knot/fd_resolver.hpp"

#include "knot/proc.hpp"

#include <arpa/inet.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace knot {

namespace {
// "socket:[2176]" -> 2176
bool bracket_inode(const std::string& link, const char* prefix, std::uint64_t& ino) {
    const std::string p(prefix);
    if (link.compare(0, p.size(), p) != 0 || link.size() < p.size() + 3) return false;
    ino = std::strtoull(link.c_str() + p.size() + 1, nullptr, 10);
    return true;
