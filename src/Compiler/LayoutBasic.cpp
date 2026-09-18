// Spec 14 §7 — interaction graph, trivial and dense layouts, layout validation.
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/LayoutPass.hpp"
#include "Core/Random.hpp"
#include <algorithm>
#include <deque>
#include <format>
#include <map>

namespace qlab::compiler {
namespace {
using PairWeights = std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t>;

void collect(const ir::Circuit& c, InteractionGraph& ig, PairWeights& weights) {
    for (ir::NodeId id : c.topologicalOrder()) {
        const ir::Node& n = c.node(id);
        for (ir::Wire w : ir::nodeWires(n))
            if (w.index < ig.qubits) ig.used[w.index] = 1;
        if (const auto* g = std::get_if<ir::Gate>(&n)) {
            const auto ws = g->wires();
            for (std::size_t i = 0; i < ws.size(); ++i)
                for (std::size_t j = i + 1; j < ws.size(); ++j)
                    if (ws[i].index < ig.qubits && ws[j].index < ig.qubits && ws[i] != ws[j])
                        ++weights[{std::min(ws[i].index, ws[j].index), std::max(ws[i].index, ws[j].index)}];
        } else if (const auto* m = std::get_if<ir::Measure>(&n)) {
            if (m->qubit.index < ig.qubits) ig.measured[m->qubit.index] = 1;
        } else {
            forEachBody(n, [&](const ir::Circuit& body) { collect(body, ig, weights); });
        }
    }
}

// BFS order of the vertices of an adjacency structure, components in the order of `seeds`.
template <class Neighbours>
std::vector<std::uint32_t> bfsOrder(const std::vector<std::uint32_t>& seeds, std::uint32_t space, Neighbours&& next) {
    std::vector<std::uint8_t> seen(space, 0);
    std::vector<std::uint32_t> order;
    std::deque<std::uint32_t> queue;
    for (std::uint32_t s : seeds) {
        if (seen[s]) continue;
        seen[s] = 1;
        queue.push_back(s);
        while (!queue.empty()) {
            const std::uint32_t u = queue.front();
            queue.pop_front();
            order.push_back(u);
            for (std::uint32_t v : next(u))
                if (!seen[v]) { seen[v] = 1; queue.push_back(v); }
        }
    }
    return order;
}
} // namespace

InteractionGraph interactionGraph(const ir::Circuit& c) {
    InteractionGraph ig;
    ig.qubits = c.qubitCount();
    ig.used.assign(ig.qubits, 0);
    ig.measured.assign(ig.qubits, 0);
    ig.adj.assign(ig.qubits, {});
    ig.depth = static_cast<std::uint32_t>(c.depth());
    PairWeights weights;
    collect(c, ig, weights);
    for (const auto& [pair, w] : weights) {
        ig.edges.push_back({pair.first, pair.second, w});
        ig.adj[pair.first].emplace_back(pair.second, w);
        ig.adj[pair.second].emplace_back(pair.first, w);
    }
    return ig;
}

Status validateLayout(const Layout& l, std::uint32_t programQubits, const CouplingGraph& g) {
    if (l.size() != programQubits)
        return fail(err::BadLayout, std::format("layout maps {} qubit(s) but the program has {}", l.size(), programQubits));
    if (!l.injective()) return fail(err::BadLayout, "layout maps two program qubits to one physical qubit");
    for (std::uint32_t v = 0; v < l.size(); ++v)
        if (!g.isData(l.v2p[v]))
            return fail(err::BadLayout, std::format("layout maps q{} to ${}, which is not a data qubit of the device", v, l.v2p[v]));
    return {};
}

Result<Layout> trivialLayout(std::uint32_t programQubits, const CouplingGraph& g) {
    if (programQubits > g.dataQubits().size())
        return fail(err::BadLayout, std::format("the program uses {} qubits but the device has {} data qubits", programQubits, g.dataQubits().size()));
    Layout l;
    l.v2p.assign(g.dataQubits().begin(), g.dataQubits().begin() + programQubits);
    return l;
}

Result<Layout> denseLayout(const InteractionGraph& ig, const CouplingGraph& g, std::uint64_t seed) {
    const auto& data = g.dataQubits();
    const std::uint32_t k = ig.qubits;
    if (k > data.size())
        return fail(err::BadLayout, std::format("the program uses {} qubits but the device has {} data qubits", k, data.size()));
    if (k == 0) return Layout{};
    core::Random rng(seed);
    std::vector<std::uint32_t> best;
    std::size_t bestEdges = 0;
    const std::uint32_t hub = *std::max_element(data.begin(), data.end(), [&](std::uint32_t a, std::uint32_t b) {
        return g.degree(a) != g.degree(b) ? g.degree(a) < g.degree(b) : a > b;
    });
    for (int restart = 0; restart < 20; ++restart) {
        const std::uint32_t start = restart == 0 ? hub : data[rng.uniformInt(data.size())];
        std::vector<std::uint8_t> in(g.qubitCount(), 0);
        std::vector<std::uint32_t> set{start};
        in[start] = 1;
        std::size_t internal = 0;
        std::vector<std::uint32_t> ties;
        while (set.size() < k) {
            std::size_t bestInto = 0;
            ties.clear();
            for (std::uint32_t p : data) {
                if (in[p]) continue;
                std::size_t into = 0;
                for (std::uint32_t q : g.neighbours(p)) into += in[q];
                if (into > bestInto) { bestInto = into; ties.assign(1, p); }
                else if (into == bestInto) ties.push_back(p);
            }
            if (bestInto == 0) {   // component exhausted: continue from the closest free qubit
                std::stable_sort(ties.begin(), ties.end(), [&](std::uint32_t a, std::uint32_t b) { return g.hops(set.front(), a) < g.hops(set.front(), b); });
                ties.resize(1);
            }
            const std::uint32_t pick = restart == 0 ? ties.front() : ties[rng.uniformInt(ties.size())];
            in[pick] = 1;
            set.push_back(pick);
            internal += bestInto;
        }
        if (best.empty() || internal > bestEdges) { best = set; bestEdges = internal; }
    }
    // Map by BFS order: most interacting program qubit ↔ best connected qubit of the subgraph.
    std::vector<std::uint32_t> vSeeds(k);
    for (std::uint32_t v = 0; v < k; ++v) vSeeds[v] = v;
    auto strength = [&](std::uint32_t v) { std::uint64_t s = 0; for (auto [u, w] : ig.adj[v]) { (void)u; s += w; } return s; };
    std::stable_sort(vSeeds.begin(), vSeeds.end(), [&](std::uint32_t a, std::uint32_t b) { return strength(a) > strength(b); });
    const auto vOrder = bfsOrder(vSeeds, k, [&](std::uint32_t v) {
        auto nb = ig.adj[v];
        std::stable_sort(nb.begin(), nb.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
        std::vector<std::uint32_t> out;
        for (const auto& e : nb) out.push_back(e.first);
        return out;
    });
    std::vector<std::uint8_t> inBest(g.qubitCount(), 0);
    for (std::uint32_t p : best) inBest[p] = 1;
    auto internalDegree = [&](std::uint32_t p) { std::size_t d = 0; for (std::uint32_t q : g.neighbours(p)) d += inBest[q]; return d; };
    std::vector<std::uint32_t> pSeeds = best;
    std::stable_sort(pSeeds.begin(), pSeeds.end(), [&](std::uint32_t a, std::uint32_t b) { return internalDegree(a) > internalDegree(b); });
    const auto pOrder = bfsOrder(pSeeds, g.qubitCount(), [&](std::uint32_t p) {
        std::vector<std::uint32_t> out;
        for (std::uint32_t q : g.neighbours(p))
            if (inBest[q]) out.push_back(q);
        return out;
    });
    Layout l;
    l.v2p.assign(k, 0);
    for (std::uint32_t i = 0; i < k; ++i) l.v2p[vOrder[i]] = pOrder[i];
    return l;
}

} // namespace qlab::compiler
