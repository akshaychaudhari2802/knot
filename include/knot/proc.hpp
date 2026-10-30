// knot/proc.hpp — small, allocation-light helpers over /proc.
// Every function tolerates the target (or one thread) vanishing mid-read:
// ENOENT is normal during analysis and is returned as std::nullopt.
#pragma once

#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace knot {

// Fatal errors for the whole analysis. Per-thread races are NOT errors.
enum class ErrorKind { NoSuchProcess, PermissionDenied, NotSupported, BadCore, Internal };

struct KnotError : std::runtime_error {
    KnotError(ErrorKind k, const std::string& what) : std::runtime_error(what), kind(k) {}
    ErrorKind kind;
};

// Move-only owner of a file descriptor: closed exactly once, on every path,
// including a constructor that throws half-way. (CoreFile's first version kept
// a raw int and leaked it on every "not a core file" error: a destructor never
// runs for an object whose constructor threw.)
class Fd {
public:
    Fd() noexcept = default;
    explicit Fd(int fd) noexcept : fd_(fd) {}
    Fd(Fd&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
    Fd& operator=(Fd&& o) noexcept {
        if (this != &o) { reset(); fd_ = std::exchange(o.fd_, -1); }
        return *this;
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    ~Fd() { reset(); }

    int get() const noexcept { return fd_; }
    explicit operator bool() const noexcept { return fd_ >= 0; }
    void reset() noexcept {
        if (fd_ >= 0) ::close(fd_);   // not retried on EINTR: Linux releases the fd regardless
        fd_ = -1;
    }

private:
    int fd_ = -1;
};

namespace proc {

std::string path(pid_t pid, const std::string& leaf);                     // /proc/<pid>/<leaf>
std::string task_path(pid_t pid, pid_t tid, const std::string& leaf);     // /proc/<pid>/task/<tid>/<leaf>

// Whole small file (stat, status, comm, syscall, maps...). errno_out receives
// errno on failure so callers can tell ENOENT (gone) from EACCES/EPERM (denied).
std::optional<std::string> read_file(const std::string& p, int* errno_out = nullptr);
std::optional<std::string> read_link(const std::string& p);
std::vector<pid_t> list_numeric_dir(const std::string& dir);   // task ids, pids, fds

// /proc/<pid>/task/<tid>/stat. The comm field may contain spaces and ')':
// it is everything between the FIRST '(' and the LAST ')'.
struct Stat {
    std::string comm;
    char state = '?';
    std::uint64_t utime = 0, stime = 0;   // clock ticks
    int rt_priority = 0;                  // field 40
    int policy = 0;                       // field 41: SCHED_OTHER=0, FIFO=1, RR=2
    std::uint64_t starttime = 0;          // field 22, ticks since boot
};
std::optional<Stat> parse_stat(const std::string& content);

// Value of "Key:" in a /proc status file, whitespace-trimmed.
