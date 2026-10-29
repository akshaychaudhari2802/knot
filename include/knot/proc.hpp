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
