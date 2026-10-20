// knot/fd_resolver.hpp — turn "fd 37" into "TCP 10.2.0.7:5432 (postgres port)".
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>

namespace knot {

class FdResolver {
public:
    explicit FdResolver(pid_t pid) : pid_(pid) {}

    // Human description of the target's fd, e.g.
    //   "TCP 127.0.0.1:43210 -> 127.0.0.1:5432 (postgres port)"
    //   "unix socket /run/app.sock"   "pipe"   "eventfd"   "file /var/log/app.log"
    std::string describe(int fd) const;

    // "/proc/net/tcp" address field, e.g. "0100007F:1538" -> "127.0.0.1:5432".
    // IPv4 words are stored in host (little-endian) byte order.
    static std::string decode_ipv4(const std::string& hex_addr_port);
    static std::string decode_ipv6(const std::string& hex_addr_port);
    static const char* well_known_port(unsigned port);

private:
    std::string describe_socket(std::uint64_t inode) const;
    pid_t pid_;
};

}  // namespace knot
