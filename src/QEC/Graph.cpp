// Spec 16 §5.1 — decoding-graph bookkeeping (weights, incidence) and the
// code-capacity graph read directly from a matchable code.
#include "QEC/Graph.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qec {

std::uint32_t DecodingGraph::findEdge(std::uint32_t a, std::uint32_t b) const {
    if (a != kBoundary && b != kBoundary && a > b)
        std::swap(a, b);
    if (a == kBoundary)
        std::swap(a, b);
    if (a >= incident.size())
        return kNoIndex;
    for (std::uint32_t e : incident[a])
        if (edges[e].a == a && edges[e].b == b)
            return e;
    return kNoIndex;
}

void DecodingGraph::finalize(bool unitWeights) {
    incident.assign(detectors, {});
    for (std::uint32_t i = 0; i < edges.size(); ++i) {
        GraphEdge& e = edges[i];
        if (unitWeights || e.probability <= 0.0) {
            e.weight = 1.0;
        } else {
            // T09 §6.2: w = ln((1 − p)/p). Classes above 45 % are clamped there (w = 0.2): far
            // above any threshold, and it keeps the integer edge lengths of the union-find decoder
            // bounded.
            const double p = std::min(e.probability, 0.45);
            e.weight = std::log((1.0 - p) / p);
        }
        incident[e.a].push_back(i);
        if (!e.boundary())
            incident[e.b].push_back(i);
    }
}

Result<DecodingGraph> buildCodeCapacityGraph(const StabilizerCode& code) {
    if (!code.isMatchable())
        return fail(
            err::NotMatchable,
            std::format("code '{}' is not matching-type: some single-qubit X or Z error flips "
                        "more than two checks, or the code is not CSS (spec 16 §5)",
                        code.id));
    DecodingGraph g;
    g.nData = code.n;
    g.detectors = code.checkCount();
    g.incident.assign(g.detectors, {});
    for (std::uint32_t q = 0; q < code.n; ++q)
        for (char letter : {'X', 'Z'}) {
            const auto s = syndromeOf(code.stabilizers, PauliString::single(code.n, q, letter));
            std::vector<std::uint32_t> flipped;
            for (std::uint32_t j = 0; j < s.size(); ++j)
                if (s[j])
                    flipped.push_back(j);
            if (flipped.empty())
                continue; // undetectable error type (repetition codes)
            GraphEdge e;
            e.a = flipped[0];
            e.b = flipped.size() == 2 ? flipped[1] : kBoundary;
            // Degenerate errors (same checks, product a stabilizer) share one edge: keep the first.
            bool known = false;
            for (const GraphEdge& other : g.edges)
                known = known || (other.a == e.a && other.b == e.b);
            if (known)
                continue;
            e.correction = {{q, letter}};
            g.edges.push_back(std::move(e));
            ++g.faults;
        }
    g.finalize(true);
    return g;
}

} // namespace qlab::qec
