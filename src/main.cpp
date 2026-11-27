// src/main.cpp — the knot command.
#include "knot/analysis.hpp"
#include "knot/stacks.hpp"
#include "knot/permission.hpp"
#include "knot/report.hpp"
#include "knot/symbols.hpp"
#include "knot/watch.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace knot;

namespace {

void usage() {
    std::fputs(
        "knot - find out why a process is stuck\n\n"
        "  knot <pid> | --name NAME           diagnose (3 samples over 1 s)\n"
        "  knot --core FILE                   post-mortem on a core dump\n"
        "  knot watch <pid> | --name NAME     watchdog\n"
        "  knot show BUNDLE.knot.json         print a saved report\n\n"
        "ANALYSIS   --once  --samples N  --interval MS  --all-threads\n"
        "           --stacks  (Tier 2: call stacks; pauses each thread briefly, one at a time)\n"
        "OUTPUT     --json FILE (bundle)  -q (verdict line only)\n"
        "WATCH      --interval S  --stall S  --hot-lock N:S  --probe CMD  --on-hang CMD\n"
        "           --gcore  --out DIR  --cooldown S  --watch-once\n\n"
        "EXIT  0 healthy  1 deadlock  2 other finding  3 indeterminate  4 error\n",
        stderr);
}

pid_t pid_by_name(const std::string& name) {
    for (pid_t p : proc::list_numeric_dir("/proc"))
        if (auto c = proc::read_file(proc::path(p, "comm")); c && proc::trim(*c) == name) return p;
    return 0;
}

std::string arg_value(int& i, int argc, char** argv) {
    if (i + 1 >= argc) { usage(); std::exit(4); }
    return argv[++i];
}

