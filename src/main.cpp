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

