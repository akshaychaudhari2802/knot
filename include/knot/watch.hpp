// knot/watch.hpp — watchdog mode.
//
// Possible only because Tier 1 never stops the target: polling it every few
// seconds for hours is safe. Fires once on a real problem, then cools down.
#pragma once

#include <sys/types.h>

#include <chrono>
#include <string>

namespace knot {

class StackProvider;

struct WatchOptions {
    std::chrono::seconds interval{5};
    std::chrono::seconds stall{60};        // lock-blocked with no wake-up for this long -> fire
    int hot_lock_waiters = 0;              // 0 = off; else fire when >= N waiters ...
    std::chrono::seconds hot_lock_for{30}; // ... continuously for this long
    std::string probe;                     // optional health command; fires only WITH internal evidence
    int probe_failures = 3;
    std::string on_hang;                   // command to run; gets KNOT_VERDICT, KNOT_REPORT, KNOT_PID
    bool gcore = false;                    // also save a core with gcore(1)
    std::string out_dir = ".";
    std::chrono::seconds cooldown{600};
    bool once = false;                     // exit after the first trigger (tests, CI)
    StackProvider* stacks = nullptr;       // Tier 2: stacks in the fired report ONLY (polling stays zero-stop)
