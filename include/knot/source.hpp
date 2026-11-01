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
public:
    virtual ~Source() = default;
    virtual bool read_memory(std::uint64_t addr, void* out, std::size_t len) const = 0;
    virtual Snapshot snapshot() const = 0;
    virtual std::vector<Mapping> mappings() const = 0;
    // Prefix for opening the target's files. Live: "/proc/<pid>/root" (container-
    // correct: the target's own /usr/lib, not the host's). Core: "" (host paths).
    virtual std::string file_root() const = 0;
    virtual bool is_live() const = 0;
    virtual pid_t pid() const = 0;
};

class LiveProcess final : public Source {
public:
    explicit LiveProcess(pid_t pid);   // throws KnotError
    LiveProcess(const LiveProcess&) = delete;
    LiveProcess& operator=(const LiveProcess&) = delete;

    // pread() on /proc/<pid>/mem: one syscall, any length, and the target keeps
    // running. (PTRACE_PEEKDATA would need a stop and move 8 bytes per call.)
    bool read_memory(std::uint64_t addr, void* out, std::size_t len) const override;
    Snapshot snapshot() const override { return take_snapshot(pid_); }
    std::vector<Mapping> mappings() const override;
    std::string file_root() const override { return "/proc/" + std::to_string(pid_) + "/root"; }
    bool is_live() const override { return true; }
    pid_t pid() const override { return pid_; }

