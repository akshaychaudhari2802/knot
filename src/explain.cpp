// src/explain.cpp
#include "knot/explain.hpp"

#include "knot/analysis.hpp"

#include <string>

namespace knot {

namespace {
struct Text { const char* cause; const char* fix; };

Text chain_text(SinkKind k) {
    switch (k) {
        case SinkKind::Io:
            return {"Likely: an I/O call is made while holding a lock other threads need.",
                    "Do the I/O outside the critical section: copy what you need, unlock, then write."};
        case SinkKind::Sleep:
            return {"Likely: a sleep, retry or back-off inside a critical section.",
                    "Never sleep while holding a lock: release, sleep, re-acquire."};
