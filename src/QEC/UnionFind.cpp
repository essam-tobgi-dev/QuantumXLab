// Spec 16 §5.2, T09 §6.3 — union-find decoder (Delfosse–Nickerson): cluster growth by half-edges,
// merging with union by size and path halving, and peeling of a spanning forest of every cluster.
// Every boundary edge ends in its own virtual vertex, so a cluster "touches the boundary" when it
// contains one; an odd cluster is peeled towards those vertices, which absorb its leftover parity.
#include "QEC/Decoder.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace qlab::qec {

UnionFindDecoder::UnionFindDecoder(DecodingGraph graph, UnionFindOptions options)
    : graph_(std::move(graph)), options_(options), real_(graph_.detectors) {
    if (graph_.incident.size() != real_)
        graph_.finalize(false);
    const std::size_t nEdges = graph_.edges.size();
    virtualOf_.assign(nEdges, kNoIndex);
    capacity_.assign(nEdges, 2);
    growth_.assign(nEdges, 0);
    // Integer edge lengths from the weights (spec 16 §5.2), reduced by their common factor.
    double minWeight = 0.0;
    for (const GraphEdge& ge : graph_.edges)
        minWeight = minWeight == 0.0 ? ge.weight : std::min(minWeight, ge.weight);
    const double unit =
        std::max<std::uint32_t>(1u, options_.resolution) / (minWeight > 0.0 ? minWeight : 1.0);
    std::uint32_t common = 0;
    for (std::uint32_t e = 0; e < nEdges && options_.weighted; ++e) {
        capacity_[e] = std::max<std::uint32_t>(
            1u, static_cast<std::uint32_t>(std::lround(unit * graph_.edges[e].weight)));
        common = std::gcd(common, capacity_[e]);
    }
    for (std::uint32_t e = 0; e < nEdges; ++e) {
        capacity_[e] =
            options_.weighted ? 2 * (capacity_[e] / std::max<std::uint32_t>(1u, common)) : 2;
        if (!graph_.edges[e].boundary())
            continue;
        virtualOf_[e] = real_ + static_cast<std::uint32_t>(virtualEdge_.size());
        virtualEdge_.push_back(e);
    }
    const std::size_t total = std::size_t(real_) + virtualEdge_.size();
    parent_.assign(total, 0);
    size_.assign(total, 1);
    for (auto* flags : {&inCluster_, &fired_, &odd_, &atBoundary_, &visited_})
        flags->assign(total, 0);
    frontier_.assign(total, {});
    treeEdge_.assign(total, kNoIndex);
    treeParent_.assign(total, kNoIndex);
}

std::uint32_t UnionFindDecoder::farEnd(std::uint32_t edge, std::uint32_t from) const {
    const GraphEdge& e = graph_.edges[edge];
    if (e.boundary())
        return from == e.a ? virtualOf_[edge] : e.a;
    return from == e.a ? e.b : e.a;
}

void UnionFindDecoder::enter(std::uint32_t v) {
    if (inCluster_[v])
        return;
    inCluster_[v] = 1;
    parent_[v] = v;
    size_[v] = 1;
    odd_[v] = fired_[v];
    atBoundary_[v] = v >= real_ ? 1 : 0;
    frontier_[v].clear();
    if (v < real_)
        frontier_[v].push_back(v); // a virtual vertex has nothing further to grow into
    touchedVertices_.push_back(v);
}

std::uint32_t UnionFindDecoder::find(std::uint32_t v) {
    while (parent_[v] != v) {
        parent_[v] = parent_[parent_[v]]; // path halving
        v = parent_[v];
    }
    return v;
}

void UnionFindDecoder::join(std::uint32_t u, std::uint32_t w) {
    std::uint32_t a = find(u), b = find(w);
    if (a == b)
        return;
    if (size_[a] < size_[b])
        std::swap(a, b);
    parent_[b] = a;
    size_[a] += size_[b];
    odd_[a] ^= odd_[b];
    atBoundary_[a] |= atBoundary_[b];
    frontier_[a].insert(frontier_[a].end(), frontier_[b].begin(), frontier_[b].end());
    frontier_[b].clear();
}

// One growth round: every odd cluster that has not reached the boundary pushes each open edge of
// its frontier by one half-step; edges that fill up fuse the clusters at their ends.
Status UnionFindDecoder::grow() {
    newlyFull_.clear();
    bool grew = false;
    for (std::uint32_t root : active_) {
        keep_.clear();
        for (std::uint32_t v : frontier_[root]) {
            bool open = false;
            for (std::uint32_t e : graph_.incident[v]) {
                if (growth_[e] >= capacity_[e])
                    continue;
                if (growth_[e] == 0)
                    touchedEdges_.push_back(e);
                grew = true;
                if (++growth_[e] >= capacity_[e])
                    newlyFull_.push_back(e);
                else
                    open = true;
            }
            if (open)
                keep_.push_back(v);
        }
        frontier_[root].swap(keep_);
    }
    if (!grew)
        return fail(
            err::DecodeFailed,
            "union-find: an odd cluster can neither grow nor reach a boundary (the detection "
            "events are outside the decoding graph)");
    for (std::uint32_t e : newlyFull_) {
        const std::uint32_t u = graph_.edges[e].a, w = farEnd(e, u);
        enter(u);
        enter(w);
        join(u, w);
    }
    for (std::uint32_t& root : active_)
        root = find(root);
    std::sort(active_.begin(), active_.end());
    active_.erase(std::unique(active_.begin(), active_.end()), active_.end());
    std::erase_if(active_, [&](std::uint32_t r) { return !odd_[r] || atBoundary_[r]; });
    return {};
}

// Spanning forest over the fully grown edges, then peeling from the leaves: a fired vertex selects
// the edge to its parent and hands its parity up (T09 §6.3). Seeding follows the cluster parity:
//  odd  — the cluster reached the boundary: breadth-first from all of its boundary vertices at
//         once, so the unpaired event leaves through the nearest one;
//  even — the events pair among themselves: the tree starts at a fired vertex and ignores boundary
//         edges, even fully grown ones. (A fault whose two events sit next to opposite boundaries
//         fills its own edge and both boundary edges in the same round; pairing each event with
//         its boundary instead would turn that single fault into a logical error.)
Status UnionFindDecoder::peel(Correction& out) {
    order_.clear();
    auto seed = [&](std::uint32_t v) {
        visited_[v] = 1;
        treeEdge_[v] = kNoIndex;
        order_.push_back(v);
    };
    std::size_t head = 0;
    auto sweep = [&](bool useBoundary) {
        for (; head < order_.size(); ++head) {
            const std::uint32_t v = order_[head];
            auto visit = [&](std::uint32_t e) {
                if (growth_[e] < capacity_[e] || (!useBoundary && graph_.edges[e].boundary()))
                    return;
                const std::uint32_t w = farEnd(e, v);
                if (visited_[w])
                    return;
                visited_[w] = 1;
                treeEdge_[w] = e;
                treeParent_[w] = v;
                order_.push_back(w);
            };
            if (v >= real_)
                visit(virtualEdge_[v - real_]);
            else
                for (std::uint32_t e : graph_.incident[v])
                    visit(e);
        }
    };
    for (std::uint32_t v : touchedVertices_)
        if (v >= real_ && odd_[find(v)])
            seed(v);
    sweep(true);
    for (std::uint32_t v : firedList_)
        if (!visited_[v]) {
            seed(v);
            sweep(false);
        }

    std::vector<std::uint8_t> x(graph_.nData, 0), z(graph_.nData, 0);
    for (std::size_t i = order_.size(); i-- > 0;) {
        const std::uint32_t v = order_[i];
        if (treeEdge_[v] == kNoIndex) {
            if (v < real_ && fired_[v])
                return fail(err::DecodeFailed,
                            "union-find: a cluster without boundary kept odd parity");
            continue;
        }
        if (!fired_[v])
            continue;
        const GraphEdge& e = graph_.edges[treeEdge_[v]];
        fired_[v] = 0;
        if (treeParent_[v] < real_)
            fired_[treeParent_[v]] ^= 1u;
        out.edges.push_back({e.a, e.b});
        out.observableMask ^= e.observableMask;
        for (const auto& [q, letter] : e.correction) {
            if (q >= graph_.nData)
                continue;
            if (letter == 'X' || letter == 'Y')
                x[q] ^= 1u;
            if (letter == 'Z' || letter == 'Y')
                z[q] ^= 1u;
        }
    }
    out.pauli = PauliString::identity(graph_.nData);
    out.pauli.x = std::move(x);
    out.pauli.z = std::move(z);
    return {};
}

void UnionFindDecoder::reset() {
    for (std::uint32_t v : touchedVertices_) {
        inCluster_[v] = fired_[v] = odd_[v] = atBoundary_[v] = visited_[v] = 0;
        frontier_[v].clear();
        treeEdge_[v] = treeParent_[v] = kNoIndex;
    }
    for (std::uint32_t e : touchedEdges_)
        growth_[e] = 0;
    touchedVertices_.clear();
    touchedEdges_.clear();
    active_.clear();
    firedList_.clear();
}

Result<Correction> UnionFindDecoder::decode(const SyndromeLattice& lattice) {
    if (lattice.events.size() != real_)
        return fail(err::BadSyndrome,
                    std::format("union-find decoder expects {} detection events, got {}", real_,
                                lattice.events.size()));
    reset();
    growthRounds_ = 0;
    for (std::uint32_t v = 0; v < real_; ++v)
        if (lattice.events[v] & 1u) {
            fired_[v] = 1;
            enter(v);
            firedList_.push_back(v);
            active_.push_back(v);
        }
    Correction out;
    while (!active_.empty()) {
        ++growthRounds_;
        if (auto ok = grow(); !ok) {
            reset();
            return std::unexpected(std::move(ok.error()));
        }
    }
    Status peeled = peel(out);
    reset();
    if (!peeled)
        return std::unexpected(std::move(peeled.error()));
    return out;
}

Result<std::unique_ptr<IDecoder>> makeCodeCapacityDecoder(std::string_view name,
                                                          const StabilizerCode& code) {
    if (name == "lookup") {
        QXL_TRY_ASSIGN(LookupDecoder d, LookupDecoder::create(code));
        return std::unique_ptr<IDecoder>(std::make_unique<LookupDecoder>(std::move(d)));
    }
    if (name == "union_find") {
        QXL_TRY_ASSIGN(DecodingGraph g, buildCodeCapacityGraph(code));
        return std::unique_ptr<IDecoder>(std::make_unique<UnionFindDecoder>(std::move(g)));
    }
    return fail(
        ErrorCode::Unsupported,
        std::format("decoder '{}' is not available (spec 16 §5: lookup, union_find; mwpm is MAY)",
                    name));
}

} // namespace qlab::qec
