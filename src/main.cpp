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

// A non-negative integer that is the WHOLE string. (atoi read "12abc" as pid 12
// and "--samples x" as 0, silently.)
int number(const std::string& s) {
    int v = -1;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || end != s.data() + s.size() || v < 0) {
        std::fprintf(stderr, "knot: expected a non-negative number, got '%s'\n", s.c_str());
        std::exit(4);
    }
    return v;
}

int cmd_show(const std::string& file) {
    std::ifstream in(file);
    if (!in) { std::fprintf(stderr, "knot: cannot open %s\n", file.c_str()); return 4; }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string j = ss.str();
    // The bundle carries its rendered text: find and unescape "report_text".
    const auto k = j.find("\"report_text\": \"");
    if (k == std::string::npos) { std::fputs("knot: not a knot bundle\n", stderr); return 4; }
    std::string out;
    for (std::size_t i = k + 16; i < j.size() && j[i] != '"'; ++i) {
        if (j[i] != '\\') { out += j[i]; continue; }
        const char e = j[++i];
        if (e == 'u' && i + 4 < j.size()) {   // json_escape writes other control characters as \u00XX
            out += static_cast<char>(std::strtol(j.substr(i + 1, 4).c_str(), nullptr, 16));
