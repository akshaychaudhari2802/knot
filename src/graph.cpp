// src/graph.cpp
#include "knot/graph.hpp"

#include <algorithm>
#include <functional>
#include <queue>

namespace knot {

void WaitGraph::add(const Edge& e) {
