// knot/report.hpp — render a Report as text (for people) or JSON (for tools).
#pragma once

#include <iosfwd>
#include <string>

namespace knot {

struct Report;

struct RenderOptions {
    bool all_threads = false;   // don't collapse idle threads / pools
};

void render_text(const Report& r, std::ostream& os, const RenderOptions& opt = {});

// A self-contained "bundle": structured data plus the rendered text, small
// enough to attach to a ticket. Contains NO memory contents except lock words.
void render_json(const Report& r, std::ostream& os);

