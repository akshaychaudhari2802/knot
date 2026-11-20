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
}
}  // namespace

const char* FdResolver::well_known_port(unsigned port) {
    switch (port) {
        case 53: return "dns";            case 80: return "http";
        case 443: return "https";         case 2181: return "zookeeper";
        case 3306: return "mysql";        case 5432: return "postgres port";
        case 5672: return "amqp";         case 6379: return "redis";
        case 8080: return "http-alt";     case 9042: return "cassandra";
        case 9092: return "kafka";        case 9200: return "elasticsearch";
        case 11211: return "memcached";   case 27017: return "mongodb";
        default: return nullptr;
    }
}

std::string FdResolver::decode_ipv4(const std::string& hex) {
    const auto colon = hex.find(':');
    if (colon == std::string::npos) return hex;
    const auto raw = static_cast<std::uint32_t>(std::strtoul(hex.substr(0, colon).c_str(), nullptr, 16));
    const unsigned port = static_cast<unsigned>(std::strtoul(hex.substr(colon + 1).c_str(), nullptr, 16));
    in_addr a{};
    a.s_addr = raw;   // /proc stores the word in host order == network bytes on little-endian
    char buf[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &a, buf, sizeof buf);
    return std::string(buf) + ":" + std::to_string(port);
}

std::string FdResolver::decode_ipv6(const std::string& hex) {
    const auto colon = hex.find(':');
    if (colon == std::string::npos || colon != 32) return hex;
    in6_addr a{};
    for (int w = 0; w < 4; ++w) {   // four 32-bit words, each in host order
        const auto word = static_cast<std::uint32_t>(std::strtoul(hex.substr(static_cast<std::size_t>(w) * 8, 8).c_str(), nullptr, 16));
        std::memcpy(&a.s6_addr[w * 4], &word, 4);
    }
    const unsigned port = static_cast<unsigned>(std::strtoul(hex.substr(colon + 1).c_str(), nullptr, 16));
    char buf[INET6_ADDRSTRLEN];
    ::inet_ntop(AF_INET6, &a, buf, sizeof buf);
    return "[" + std::string(buf) + "]:" + std::to_string(port);
}

std::string FdResolver::describe(int fd) const {
    auto link = proc::read_link(proc::path(pid_, "fd/" + std::to_string(fd)));
    if (!link) return "fd " + std::to_string(fd) + " (closed or unreadable)";
    std::uint64_t ino = 0;
    if (bracket_inode(*link, "socket:", ino)) return describe_socket(ino);
    if (bracket_inode(*link, "pipe:", ino)) return "pipe";
    if (link->compare(0, 11, "anon_inode:") == 0) return link->substr(11);   // [eventfd], [timerfd]...
    return "file " + *link;
}

// Search the TARGET's network namespace (/proc/<pid>/net/*, not /proc/net/*):
// inside a container, the target's sockets are not in Knot's namespace at all.
