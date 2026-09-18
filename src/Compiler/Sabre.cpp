// Spec 14 §8 — the SABRE swap search. Cost of a swap s that turns the mapping π into π_s:
//   H(s) = (1/|F|) Σ_{g∈F} D[π_s g₁][π_s g₂] + W (1/|E|) Σ_{g∈E} D[π_s g₁][π_s g₂] + δ·decay(s)
// Only the gates of F ∪ E that touch the two swapped qubits change, so each candidate is scored by
// its difference to the current mapping.
#include "Compiler/SabreImpl.hpp"
#include <algorithm>
#include <format>

namespace qlab::compiler::detail {
namespace {
constexpr double kFar = 1e6;
struct PairRef { std::uint32_t a, b, node; };   // logical qubits of a blocked or upcoming two-qubit gate
} // namespace

Sabre::Sabre(const CouplingGraph& g, const RouteOptions& o, std::uint64_t seed)
    : g_(g), o_(o), rng_(seed), n_(g.qubitCount()), dist_(static_cast<std::size_t>(n_) * n_, kFar) {
    for (std::uint32_t a : g.dataQubits())
        for (std::uint32_t b : g.dataQubits()) {
            if (g.hops(a, b) == CouplingGraph::kUnreachable) continue;
            dist_[a * n_ + b] = o.noiseAware ? g.reliabilityDistance(a, b) : static_cast<double>(g.hops(a, b));
        }
}

Result<std::uint32_t> Sabre::run(const Dag& dag, bool reversed, FullLayout& layout, const OnNode& onNode,
                                 const OnSwap& onSwap, std::stop_token stop) {
    const auto& nodes = dag.nodes;
    const auto count = static_cast<std::uint32_t>(nodes.size());
    auto next = [&](std::uint32_t i) -> const std::vector<std::uint32_t>& { return reversed ? nodes[i].pred : nodes[i].succ; };
    std::vector<std::uint32_t> remaining(count), front;
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::uint32_t i = reversed ? count - 1 - k : k;
        remaining[i] = static_cast<std::uint32_t>((reversed ? nodes[i].succ : nodes[i].pred).size());
        if (remaining[i] == 0) front.push_back(i);
    }
    const std::uint32_t logical = static_cast<std::uint32_t>(layout.l2p.size());
    std::vector<std::vector<std::uint32_t>> fAdj(logical), eAdj(logical);
    std::vector<PairRef> fPairs, ePairs;
    std::vector<std::uint32_t> visited(count, 0), queue, decay(n_, 0);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates, ties;
    std::vector<std::uint32_t> seenEdge(static_cast<std::size_t>(n_) * n_, 0);   // stamp per qubit pair
    std::uint32_t generation = 0, swaps = 0, sinceProgress = 0, swapRound = 0;
    const std::uint32_t stallLimit = std::max<std::uint32_t>(20, 4 * g_.diameter());
    bool stale = true;

    auto emitSwap = [&](std::uint32_t p, std::uint32_t q, std::uint32_t enabled) -> Status {
        layout.swapPhysical(p, q);
        ++swaps;
        if (onSwap) QXL_TRY(onSwap(p, q, enabled));
        return {};
    };

    while (!front.empty()) {
        if (stop.stop_requested()) return fail(ErrorCode::Cancelled, "compile cancelled");
        bool progressed = false;
        for (std::size_t idx = 0; idx < front.size();) {
            const std::uint32_t i = front[idx];
            const DagNode& nd = nodes[i];
            if (nd.twoQubit && !g_.adjacent(layout.l2p[nd.wires[0]], layout.l2p[nd.wires[1]])) { ++idx; continue; }
            if (onNode) QXL_TRY(onNode(i, layout));
            front[idx] = front.back();
            front.pop_back();
            for (std::uint32_t s : next(i))
                if (--remaining[s] == 0) front.push_back(s);
            progressed = true;
        }
        if (progressed) {
            sinceProgress = 0;
            stale = true;
            std::fill(decay.begin(), decay.end(), 0u);
            continue;
        }
        // Every node left in the front layer is a two-qubit gate on an uncoupled pair.
        if (stale) {
            for (const PairRef& p : fPairs) { fAdj[p.a].clear(); fAdj[p.b].clear(); }
            for (const PairRef& p : ePairs) { eAdj[p.a].clear(); eAdj[p.b].clear(); }
            fPairs.clear();
            ePairs.clear();
            ++generation;
            queue.assign(front.begin(), front.end());
            for (std::uint32_t i : front) {
                visited[i] = generation;
                fPairs.push_back({nodes[i].wires[0], nodes[i].wires[1], i});
            }
            for (std::size_t head = 0; head < queue.size() && ePairs.size() < o_.lookahead; ++head)
                for (std::uint32_t s : next(queue[head])) {
                    if (visited[s] == generation) continue;
                    visited[s] = generation;
                    queue.push_back(s);
                    if (nodes[s].twoQubit && ePairs.size() < o_.lookahead) ePairs.push_back({nodes[s].wires[0], nodes[s].wires[1], s});
                }
            for (const PairRef& p : fPairs) { fAdj[p.a].push_back(p.b); fAdj[p.b].push_back(p.a); }
            for (const PairRef& p : ePairs) { eAdj[p.a].push_back(p.b); eAdj[p.b].push_back(p.a); }
            stale = false;
        }
        if (sinceProgress > stallLimit) {   // release valve: walk the closest blocked gate together
            const PairRef* best = &fPairs.front();
            for (const PairRef& p : fPairs)
                if (g_.hops(layout.l2p[p.a], layout.l2p[p.b]) < g_.hops(layout.l2p[best->a], layout.l2p[best->b])) best = &p;
            while (!g_.adjacent(layout.l2p[best->a], layout.l2p[best->b])) {
                const std::uint32_t p = layout.l2p[best->a], target = layout.l2p[best->b];
                if (g_.hops(p, target) == CouplingGraph::kUnreachable)
                    return fail(err::BadLayout, std::format("${} and ${} are in different components of the coupling graph", p, target));
                std::uint32_t step = p;
                for (std::uint32_t q : g_.neighbours(p))
                    if (g_.hops(q, target) < g_.hops(step, target)) step = q;
                QXL_TRY(emitSwap(p, step, best->node));
            }
            sinceProgress = 0;
            continue;
        }
        candidates.clear();   // every edge that touches a qubit of the front layer, once
        ++swapRound;
        for (const PairRef& pr : fPairs)
            for (std::uint32_t l : {pr.a, pr.b}) {
                const std::uint32_t p = layout.l2p[l];
                for (std::uint32_t q : g_.neighbours(p)) {
                    const std::uint32_t lo = std::min(p, q), hi = std::max(p, q);
                    if (seenEdge[lo * n_ + hi] == swapRound) continue;
                    seenEdge[lo * n_ + hi] = swapRound;
                    candidates.emplace_back(lo, hi);
                }
            }
        if (candidates.empty()) return fail(err::BadLayout, "a program qubit sits on a device qubit without couplings");

        auto delta = [&](const std::vector<std::vector<std::uint32_t>>& adj, std::uint32_t a, std::uint32_t b, std::uint32_t p,
                         std::uint32_t q) {   // logical a on p moves to q, logical b on q moves to p
            double d = 0.0;
            for (std::uint32_t o : adj[a]) if (o != b) d += dist(q, layout.l2p[o]) - dist(p, layout.l2p[o]);
            for (std::uint32_t o : adj[b]) if (o != a) d += dist(p, layout.l2p[o]) - dist(q, layout.l2p[o]);
            return d;
        };
        double bestScore = 0.0;
        ties.clear();
        for (const auto& [p, q] : candidates) {
            const std::uint32_t a = layout.p2l[p], b = layout.p2l[q];
            double h = delta(fAdj, a, b, p, q) / static_cast<double>(fPairs.size());
            if (!ePairs.empty()) h += o_.lookaheadWeight * delta(eAdj, a, b, p, q) / static_cast<double>(ePairs.size());
            h += o_.decay * static_cast<double>(decay[p] + decay[q]);
            if (ties.empty() || h < bestScore - 1e-12) { bestScore = h; ties.assign(1, {p, q}); }
            else if (h <= bestScore + 1e-12) ties.emplace_back(p, q);
        }
        const auto [p, q] = ties[rng_.uniformInt(ties.size())];
        std::uint32_t enabled = fPairs.front().node;   // the blocked gate this swap works for
        for (const PairRef& pr : fPairs)
            if (pr.a == layout.p2l[p] || pr.b == layout.p2l[p] || pr.a == layout.p2l[q] || pr.b == layout.p2l[q]) { enabled = pr.node; break; }
        QXL_TRY(emitSwap(p, q, enabled));
        ++sinceProgress;
        ++decay[p];
        ++decay[q];
        if (o_.decayReset > 0 && swaps % o_.decayReset == 0) std::fill(decay.begin(), decay.end(), 0u);
    }
    return swaps;
}

} // namespace qlab::compiler::detail
