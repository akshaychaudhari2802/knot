// knot/explain.hpp — likely cause + fix hint for every finding.
// A rules table, not guesswork: Knot is certain about FACTS (who waits for
// whom) and says "likely" about CAUSES, because causes are inferred.
#pragma once

namespace knot {

struct Report;

// Fills Finding::likely_cause and Finding::fix_hint. Idempotent.
void explain(Report& r);

}  // namespace knot
