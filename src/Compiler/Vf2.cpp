// Spec 14 §7 — VF2 subgraph isomorphism of the interaction graph into the coupling graph
// (Cordella et al. 2004, monomorphism variant: program edges must map onto device edges; extra
// device edges are allowed). Vertices are matched in BFS order, so every vertex after the first of
// its component has a mapped neighbour and its candidates are that neighbour's image's neighbours.
#include "Compiler/LayoutPass.hpp"
#include <algorithm>
#include <deque>
#include <functional>

namespace qlab::compiler {
namespace {
constexpr std::uint32_t kNone = 0xFFFFFFFFu;

struct Search {
    const InteractionGraph& ig;
    const CouplingGraph& g;
    std::uint64_t budget;
    std::uint32_t maxEmbeddings;
    std::vector<std::uint32_t> order;        // interacting program qubits, BFS order
    std::vector<std::uint32_t> anchor;       // per order slot: an earlier-matched neighbour, or kNone
    std::vector<std::uint32_t> image;        // program qubit → device qubit
    std::vector<std::uint8_t> taken;         // device qubit in use
    Vf2Result result;
    double bestScore = 0.0;
    bool stop = false;

    // Program qubits without interactions take the free data qubits in ascending order.
    Layout complete() const {
        Layout l;
        l.v2p = image;
        std::vector<std::uint8_t> used = taken;
        std::size_t next = 0;
        const auto& data = g.dataQubits();
        for (std::uint32_t v = 0; v < ig.qubits; ++v) {
            if (l.v2p[v] != kNone) continue;
            while (next < data.size() && used[data[next]]) ++next;
            if (next == data.size()) break;
            l.v2p[v] = data[next];
            used[data[next]] = 1;
        }
        return l;
    }

    bool feasible(std::uint32_t v, std::uint32_t p) const {
        if (taken[p] || g.degree(p) < ig.degree(v)) return false;
        for (const auto& [u, w] : ig.adj[v]) {
            (void)w;
            if (image[u] != kNone && !g.adjacent(p, image[u])) return false;
        }
        return true;
    }

    void search(std::size_t depth) {
        if (depth == order.size()) {
            ++result.embeddings;
            const Layout l = complete();
            const double s = layoutScore(l, ig, g);
            if (!result.layout || s < bestScore) { result.layout = l; bestScore = s; }
            if (result.embeddings >= maxEmbeddings) stop = true;
            return;
        }
        const std::uint32_t v = order[depth];
        const auto& candidates = anchor[depth] == kNone ? g.dataQubits() : g.neighbours(image[anchor[depth]]);
        for (std::uint32_t p : candidates) {
            if (++result.states > budget) { result.budgetExceeded = true; stop = true; return; }
            if (!feasible(v, p)) continue;
            image[v] = p;
            taken[p] = 1;
            search(depth + 1);
            image[v] = kNone;
            taken[p] = 0;
            if (stop) return;
        }
    }
};
} // namespace

Vf2Result vf2Layout(const InteractionGraph& ig, const CouplingGraph& g, std::uint64_t stateBudget, std::uint32_t maxEmbeddings) {
    Search s{ig, g, stateBudget, std::max<std::uint32_t>(maxEmbeddings, 1), {}, {}, {}, {}, {}, 0.0, false};
    if (ig.qubits > g.dataQubits().size()) return s.result;

    // Necessary conditions: enough edges, and the sorted degree sequences must dominate.
    std::vector<std::size_t> want, have;
    for (std::uint32_t v = 0; v < ig.qubits; ++v)
        if (ig.degree(v) > 0) want.push_back(ig.degree(v));
    for (std::uint32_t p : g.dataQubits()) have.push_back(g.degree(p));
    std::sort(want.rbegin(), want.rend());
    std::sort(have.rbegin(), have.rend());
    if (ig.edges.size() > g.edges().size() || want.size() > have.size()) return s.result;
    for (std::size_t i = 0; i < want.size(); ++i)
        if (want[i] > have[i]) return s.result;

    // BFS order over the interaction graph, highest degree first inside and across components.
    std::vector<std::uint32_t> seeds;
    for (std::uint32_t v = 0; v < ig.qubits; ++v)
        if (ig.degree(v) > 0) seeds.push_back(v);
    std::stable_sort(seeds.begin(), seeds.end(), [&](std::uint32_t a, std::uint32_t b) { return ig.degree(a) > ig.degree(b); });
    std::vector<std::uint32_t> slot(ig.qubits, kNone);
    std::deque<std::uint32_t> queue;
    for (std::uint32_t seed : seeds) {
        if (slot[seed] != kNone) continue;
        slot[seed] = static_cast<std::uint32_t>(s.order.size());
        s.order.push_back(seed);
        s.anchor.push_back(kNone);
        queue.push_back(seed);
        while (!queue.empty()) {
            const std::uint32_t u = queue.front();
            queue.pop_front();
            auto nb = ig.adj[u];
            std::stable_sort(nb.begin(), nb.end(), [&](const auto& x, const auto& y) { return ig.degree(x.first) > ig.degree(y.first); });
            for (const auto& [v, w] : nb) {
                (void)w;
                if (slot[v] != kNone) continue;
                slot[v] = static_cast<std::uint32_t>(s.order.size());
                s.order.push_back(v);
                s.anchor.push_back(u);
                queue.push_back(v);
            }
        }
    }
    s.image.assign(ig.qubits, kNone);
    s.taken.assign(g.qubitCount(), 0);
    s.search(0);
    return s.result;
}

} // namespace qlab::compiler
