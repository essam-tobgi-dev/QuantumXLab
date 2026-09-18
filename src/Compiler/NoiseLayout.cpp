// Spec 14 §7 — the noise score, its local search, and the policy dispatch.
#include "Compiler/LayoutPass.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace qlab::compiler {
namespace {
constexpr std::uint32_t kFree = 0xFFFFFFFFu;
constexpr double kDisconnected = 1e9;

double pairCost(const CouplingGraph& g, std::uint32_t p, std::uint32_t q) {
    if (g.adjacent(p, q)) return g.edgeCost(p, q);
    const double path = g.pathCost(p, q);
    return std::isfinite(path) ? 3.0 * path : kDisconnected;   // a swap is three entanglers per hop
}
double nodeCost(const InteractionGraph& ig, const CouplingGraph& g, std::uint32_t v, std::uint32_t p) {
    if (!ig.used[v]) return 0.0;
    const double tCirc = static_cast<double>(ig.depth) * g.typicalTwoQubitSeconds();
    return (ig.measured[v] ? g.readoutCost(p) : 0.0) + tCirc * g.decayRate(p);
}
// Score terms that involve program qubit a or b (b may be kFree); the a–b edge is counted once.
double localScore(const std::vector<std::uint32_t>& v2p, const InteractionGraph& ig, const CouplingGraph& g,
                  std::uint32_t a, std::uint32_t b) {
    double s = 0.0;
    for (std::uint32_t v : {a, b}) {
        if (v == kFree) continue;
        s += nodeCost(ig, g, v, v2p[v]);
        for (const auto& [u, w] : ig.adj[v]) {
            if (v == b && u == a) continue;
            s += w * pairCost(g, v2p[v], v2p[u]);
        }
    }
    return s;
}
} // namespace

double layoutScore(const Layout& l, const InteractionGraph& ig, const CouplingGraph& g) {
    double s = 0.0;
    for (const auto& e : ig.edges) s += e.weight * pairCost(g, l.v2p[e.a], l.v2p[e.b]);
    for (std::uint32_t v = 0; v < ig.qubits; ++v) s += nodeCost(ig, g, v, l.v2p[v]);
    return s;
}

Layout refineLayout(Layout start, const InteractionGraph& ig, const CouplingGraph& g, std::uint64_t, std::uint32_t iterations) {
    std::vector<std::uint32_t>& v2p = start.v2p;
    std::vector<std::uint32_t> p2v(g.qubitCount(), kFree);
    for (std::uint32_t v = 0; v < v2p.size(); ++v) p2v[v2p[v]] = v;
    const bool allPairs = ig.qubits <= 24;
    for (std::uint32_t it = 0; it < iterations; ++it) {
        double bestDelta = -1e-12;
        std::uint32_t bestV = kFree, bestP = 0;
        auto consider = [&](std::uint32_t v, std::uint32_t target) {   // exchange v with whatever sits on `target`
            const std::uint32_t u = p2v[target], from = v2p[v];
            if (u == v) return;
            const double before = localScore(v2p, ig, g, v, u);
            v2p[v] = target;
            if (u != kFree) v2p[u] = from;
            const double delta = localScore(v2p, ig, g, v, u) - before;
            v2p[v] = from;
            if (u != kFree) v2p[u] = target;
            if (delta < bestDelta) { bestDelta = delta; bestV = v; bestP = target; }
        };
        for (std::uint32_t v = 0; v < ig.qubits; ++v) {
            if (!ig.used[v]) continue;
            for (std::uint32_t p : g.neighbours(v2p[v])) consider(v, p);
            if (allPairs)
                for (std::uint32_t u = v + 1; u < ig.qubits; ++u)
                    if (ig.used[u]) consider(v, v2p[u]);
        }
        if (bestV == kFree) break;   // no exchange improves the score
        const std::uint32_t u = p2v[bestP], from = v2p[bestV];
        v2p[bestV] = bestP;
        p2v[bestP] = bestV;
        p2v[from] = u;
        if (u != kFree) v2p[u] = from;
    }
    return start;
}

Result<LayoutChoice> chooseLayout(const ir::Circuit& c, const CouplingGraph& g, LayoutPolicy policy, std::uint64_t seed,
                                  std::uint64_t vf2StateBudget, std::vector<lang::Diagnostic>* diagnostics) {
    const InteractionGraph ig = interactionGraph(c);
    LayoutChoice choice;
    choice.policy = policy;
    if (policy == LayoutPolicy::Trivial) {
        QXL_TRY_ASSIGN(choice.layout, trivialLayout(ig.qubits, g));
    } else if (policy == LayoutPolicy::Dense) {
        QXL_TRY_ASSIGN(choice.layout, denseLayout(ig, g, seed));
    } else {
        choice.vf2 = vf2Layout(ig, g, vf2StateBudget);
        if (choice.vf2.layout) {
            choice.layout = *choice.vf2.layout;
        } else {
            // Spec 14 §7: out of budget → dense layout with the QL4060 note. A search that ended
            // within budget proved that no embedding exists; routing will insert swaps.
            if (choice.vf2.budgetExceeded && diagnostics) diagnostics->push_back(lang::Diagnostics::make("QL4060", SourceSpan{}));
            QXL_TRY_ASSIGN(choice.layout, denseLayout(ig, g, seed));
            if (policy == LayoutPolicy::Vf2) choice.policy = LayoutPolicy::Dense;
        }
        if (policy == LayoutPolicy::NoiseAware) choice.layout = refineLayout(std::move(choice.layout), ig, g, seed);
    }
    QXL_TRY(validateLayout(choice.layout, ig.qubits, g));
    choice.score = layoutScore(choice.layout, ig, g);
    return choice;
}

} // namespace qlab::compiler
