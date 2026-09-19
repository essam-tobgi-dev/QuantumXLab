// Spec 14 §3, §8 — dependency DAG of one circuit level and the full logical↔physical bijection.
#include "Compiler/SabreImpl.hpp"
#include <algorithm>

namespace qlab::compiler::detail {

Dag buildDag(const ir::Circuit& c) {
    constexpr std::uint32_t none = 0xFFFFFFFFu;
    Dag dag;
    dag.nodes.reserve(c.nodeCount());
    std::vector<std::uint32_t> lastOnWire(c.qubitCount(), none), lastOnBit(c.clbitCount(), none);
    std::uint32_t lastEvent = none; // previous measurement, reset, classical or control node
    auto link = [&](std::uint32_t from, std::uint32_t to) {
        if (from == none)
            return;
        auto& succ = dag.nodes[from].succ;
        if (!succ.empty() && succ.back() == to)
            return; // edges of one node are added back to back
        succ.push_back(to);
        dag.nodes[to].pred.push_back(from);
    };
    for (ir::NodeId id : c.topologicalOrder()) {
        const ir::Node& n = c.node(id);
        const auto index = static_cast<std::uint32_t>(dag.nodes.size());
        DagNode d;
        if (const auto* g = std::get_if<ir::Gate>(&n)) {
            for (ir::Wire w : g->wires())
                d.wires.push_back(w.index);
            d.twoQubit = d.wires.size() == 2;
        } else {
            for (ir::Wire w : ir::nodeWires(n))
                d.wires.push_back(w.index);
        }
        dag.nodes.push_back(std::move(d));
        // "No operands" on a barrier or delay means every wire (spec 13 §3): it orders everything.
        for (ir::Wire w : ir::nodeWiresIn(n, c.qubitCount()))
            if (w.index < lastOnWire.size()) {
                link(lastOnWire[w.index], index);
                lastOnWire[w.index] = index;
            }
        auto bits = ir::nodeWrites(n);
        for (auto b : ir::nodeReads(n))
            bits.push_back(b);
        for (auto b : bits)
            if (b.index < lastOnBit.size()) {
                link(lastOnBit[b.index], index);
                lastOnBit[b.index] = index;
            }
        // Non-unitary nodes keep their source order even on disjoint qubits: the classical event
        // sequence of a shot (which measurement, reset or branch comes first) is part of the
        // program's observable behaviour, and the equivalence checker relies on it (spec 14 §10).
        const bool event = !std::holds_alternative<ir::Gate>(n) &&
                           !std::holds_alternative<ir::Barrier>(n) &&
                           !std::holds_alternative<ir::Delay>(n);
        if (event) {
            link(lastEvent, index);
            lastEvent = index;
        }
    }
    // `link` only removes consecutive duplicates; a node reached through a wire and a bit of the
    // same predecessor with another link in between is deduplicated here.
    for (auto& nd : dag.nodes) {
        for (auto* list : {&nd.succ, &nd.pred}) {
            std::sort(list->begin(), list->end());
            list->erase(std::unique(list->begin(), list->end()), list->end());
        }
    }
    return dag;
}

FullLayout FullLayout::from(const Layout& program, const CouplingGraph& g) {
    constexpr std::uint32_t none = 0xFFFFFFFFu;
    FullLayout f;
    const std::uint32_t n = g.qubitCount();
    f.l2p.assign(program.v2p.begin(), program.v2p.end());
    f.p2l.assign(n, none);
    for (std::uint32_t l = 0; l < f.l2p.size(); ++l)
        f.p2l[f.l2p[l]] = l;
    // Unused data qubits first (they take part in swaps), then couplers, both ascending.
    for (int pass = 0; pass < 2; ++pass)
        for (std::uint32_t p = 0; p < n; ++p)
            if (f.p2l[p] == none && g.isData(p) == (pass == 0)) {
                f.p2l[p] = static_cast<std::uint32_t>(f.l2p.size());
                f.l2p.push_back(p);
            }
    return f;
}

Layout FullLayout::program(std::uint32_t programQubits) const {
    Layout l;
    l.v2p.assign(l2p.begin(), l2p.begin() + programQubits);
    return l;
}

void FullLayout::swapPhysical(std::uint32_t p, std::uint32_t q) {
    const std::uint32_t a = p2l[p], b = p2l[q];
    p2l[p] = b;
    p2l[q] = a;
    l2p[a] = q;
    l2p[b] = p;
}

} // namespace qlab::compiler::detail
