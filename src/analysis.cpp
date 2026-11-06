// src/analysis.cpp
#include "knot/analysis.hpp"

#include "knot/explain.hpp"
#include "knot/fd_resolver.hpp"
#include "knot/symbols.hpp"

#include <sched.h>

#include <algorithm>
#include <sstream>
#include <thread>

namespace knot {

int Report::index_of(pid_t tid) const {
