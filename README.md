# Knot

Live deadlock and hang diagnosis for Linux processes, written in C++17.

When a process stops responding, Knot looks at it while it is still running and explains why it is stuck: which threads are blocked, what each one is waiting on (a lock, a file descriptor or a system call), and whether the waits form a deadlock cycle.

## How it works

- Reads process and thread state from `/proc` and decodes the system call each thread is blocked in
- Works out lock ownership and resolves file descriptors
- Builds a wait-for graph between threads and detects cycles (deadlocks) and long waits (hangs)
- Symbolizes stack traces (ELF symbols; DWARF-based unwinding through elfutils when available)
- Prints a plain-language explanation and a report, with an optional watch mode

The core has no third-party dependencies. Stack unwinding through elfutils `libdwfl` is optional.

## Build

Requirements: Linux, CMake 3.16+, GCC 8+ or Clang (C++17). Optional: `elfutils-devel` and `pkg-config` for full stack traces.

```bash
cmake -S . -B build
cmake --build build -j
```

Options: `-DKNOT_ASAN=ON` (AddressSanitizer + UBSan), `-DKNOT_TESTS=OFF` to skip the tests and the sample deadlocking programs.

## Status

Work in progress: the code is being pushed to this repository in stages.

## License

MIT
