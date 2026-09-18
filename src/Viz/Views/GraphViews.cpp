// Spec 21 §3.12 coupling graph and §3.8 entanglement graph (see GraphView.hpp).
#include "Viz/Views/GraphView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Math/Format.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace qlab::viz {

namespace {

// Physical qubits a top-level node acts on, mapped through the circuit's wires. The routed circuit
// is already in physical indices; the source circuit is mapped by `layout` (virtual → physical).
std::vector<QubitIndex> physicalQubitsOf(const ir::Node& node, std::span<const std::uint32_t> map) {
    std::vector<ir::Wire> wires;
    if (const auto* g = std::get_if<ir::Gate>(&node)) {
        wires.insert(wires.end(), g->controls.begin(), g->controls.end());
        wires.insert(wires.end(), g->targets.begin(), g->targets.end());
    } else if (const auto* m = std::get_if<ir::Measure>(&node)) {
        wires.push_back(m->qubit);
    } else if (const auto* r = std::get_if<ir::Reset>(&node)) {
        wires.push_back(r->qubit);
    }
    std::vector<QubitIndex> out;
    for (ir::Wire w : wires) {
        const std::uint32_t phys = w.index < map.size() ? map[w.index] : w.index;
        out.emplace_back(phys);
    }
    return out;
}

bool isSwap(const ir::Node& node) {
    const auto* g = std::get_if<ir::Gate>(&node);
    return g && g->name == "swap";
}

} // namespace

// ---------------------------------------------------------------- coupling graph (spec 21 §3.12)

data::FidelityClass CouplingView::fidelity(const ViewInput& in) const {
    // Calibration numbers are fitted measurements of the device: Model class (spec 00 §5).
    return in.calibration ? data::FidelityClass::Model : data::FidelityClass::Exact;
}

void CouplingView::setNodeField(layout::NodeField f) {
    if (options_.nodeField == f) return;
    options_.nodeField = f;
    markDirty();
}

void CouplingView::setEdgeField(layout::EdgeField f) {
    if (options_.edgeField == f) return;
    options_.edgeField = f;
    markDirty();
}

void CouplingView::rebuild(const ViewInput& in) {
    graph_ = {};
    active_.clear();
    swaps_.clear();
    note_.clear();
    if (!in.device) {
        note_ = "Load a device to see its coupling graph";
        return;
    }
    layoutMap_ = in.circuits.layout;
    layout::CouplingOptions o = options_;
    o.layout = layoutMap_;
    o.code = in.code.get();
    graph_ = layout::buildCouplingGraph(*in.device, in.calibration.get(), o);

    // Overlays of spec 21 §3.12: the routing SWAPs, and the gates active at the playhead.
    const std::shared_ptr<const ir::Circuit> routed =
        in.circuits.at(CircuitStage::Scheduled) ? in.circuits.at(CircuitStage::Scheduled) : in.circuits.at(CircuitStage::Routed);
    if (!routed) return;
    const std::span<const ir::NodeId> order = routed->topologicalOrder();
    // A routed circuit is already physical: the identity map.
    static const std::vector<std::uint32_t> kIdentity;
    for (std::size_t k = 0; k < order.size(); ++k) {
        const ir::Node& node = routed->node(order[k]);
        const std::vector<QubitIndex> qs = physicalQubitsOf(node, kIdentity);
        if (qs.size() != 2) continue;
        const auto edge = std::pair<QubitIndex, QubitIndex>{std::min(qs[0], qs[1]), std::max(qs[0], qs[1])};
        if (isSwap(node)) swaps_.push_back(edge);
        if (in.hasPlayhead && k == in.playheadGate) active_.push_back(edge);
    }
}

void CouplingView::drawOverlay(gfx::Renderer& r, GlBackend& gl, const VizTheme& theme, float pxScale) const {
    (void)gl;
    const Rect& e = graph_.extent;
    const glm::dvec2 center{e.cx(), e.cy()};
    const auto world = [&](glm::dvec2 p) { return glm::dvec3(p.x - center.x, center.y - p.y, 0.02); };
    const auto nodeAt = [&](QubitIndex q) { return q.get() < graph_.nodes.size() ? graph_.nodes[q.get()].pos : glm::dvec2{}; };
    // Inserted SWAPs are drawn as a dashed overlay on their edge; the ones the playhead is on now
    // are drawn solid and bright (the "animated pulse" of the spec, without motion when the user
    // asked for reduced motion — this view carries no animation state of its own).
    for (const auto& [a, b] : swaps_)
        r.linesNoDepth().segment(world(nodeAt(a)), world(nodeAt(b)), GlBackend::exact(glm::vec3(theme.warn), 0.85f),
                                 3.0f * pxScale, 6.0f * pxScale);
    for (const auto& [a, b] : active_)
        r.linesNoDepth().segment(world(nodeAt(a)), world(nodeAt(b)), GlBackend::exact(glm::vec3(theme.accent), 1.0f),
                                 4.5f * pxScale);
}

void CouplingView::fillNodeReadout(HitResult& h, const layout::GraphNode& n) const {
    if (n.value) h.readout.push_back({std::string(layout::nodeFieldName(options_.nodeField)), n.valueText, fidelity(input())});
    if (n.role) h.readout.push_back({"QEC role", n.syndromeFired ? "syndrome 1" : "syndrome 0", data::FidelityClass::Numerical});
}

void CouplingView::fillEdgeReadout(HitResult& h, const layout::GraphEdge& e) const {
    if (e.value) h.readout.push_back({std::string(layout::edgeFieldName(options_.edgeField)), e.valueText, fidelity(input())});
    const auto key = std::pair<QubitIndex, QubitIndex>{e.a, e.b};
    if (std::find(swaps_.begin(), swaps_.end(), key) != swaps_.end())
        h.readout.push_back({"routing", "SWAP inserted here", data::FidelityClass::Exact});
}

std::string CouplingView::statusLine() const {
    if (graph_.nodes.empty()) return {};
    std::string s = std::to_string(graph_.dataNodes) + " qubits   " + std::to_string(graph_.edges.size()) + " couplings";
    if (!swaps_.empty()) s += "   " + std::to_string(swaps_.size()) + " routing SWAPs";
    if (!active_.empty()) s += "   gate active";
    return s;
}

// ------------------------------------------------------------- entanglement graph (spec 21 §3.8)

ReductionRequest EntanglementView::wants(const ViewInput& in) const {
    ReductionRequest r;
    const std::uint32_t n = in.qubitCount();
    const std::vector<QubitIndex> subset = shownQubits(n);
    // Spec 21 §3.8: all C(n,2) pair reductions only while this view is open and n ≤ 20; above that
    // the user must pick a subset of at most 20 qubits.
    if (n > 0 && (n <= kPairReductionMaxQubits || subset.size() <= kPairReductionMaxQubits)) {
        r.singles = true;
        r.pairs = true;
        if (n > kPairReductionMaxQubits) r.qubits = subset;
        else r.qubits.assign(qubitSubset().begin(), qubitSubset().end());
    }
    return r;
}

void EntanglementView::rebuild(const ViewInput& in) {
    graph_ = {};
    note_.clear();
    totalMutualInformation_ = 0.0;
    nQubits_ = in.qubitCount();
    if (nQubits_ == 0) {
        note_ = "Run a program to see the entanglement structure";
        return;
    }
    if (!in.reductions || in.reductions->pairs.empty()) {
        note_ = nQubits_ > kPairReductionMaxQubits
                    ? "More than 20 qubits: pick a subset of at most 20 (spec 21 §3.8)"
                    : "Waiting for the pair reductions from the run";
        setStale(static_cast<bool>(in.snapshot));
        return;
    }
    graph_ = layout::buildEntanglementGraph(in.device.get(), nQubits_, *in.reductions);
    for (const layout::GraphEdge& e : graph_.edges)
        if (e.value) totalMutualInformation_ += *e.value;
}

void EntanglementView::drawOverlay(gfx::Renderer&, GlBackend&, const VizTheme&, float) const {}

void EntanglementView::fillNodeReadout(HitResult& h, const layout::GraphNode& n) const {
    if (n.value) h.readout.push_back({"S(ρ)", math::formatSig(*n.value) + " bits", input().stateClass()});
}

void EntanglementView::fillEdgeReadout(HitResult& h, const layout::GraphEdge& e) const {
    const data::FidelityClass cls = input().stateClass();
    const Reductions* red = input().reductions.get();
    if (red)
        if (const PairReduction* p = red->pair(e.a, e.b)) {
            h.readout.push_back({"S_i", math::formatSig(p->measures.entropyI) + " bits", cls});
            h.readout.push_back({"S_j", math::formatSig(p->measures.entropyJ) + " bits", cls});
            h.readout.push_back({"S_ij", math::formatSig(p->measures.entropyIJ) + " bits", cls});
        }
    if (e.value) h.readout.push_back({"I(i:j)", math::formatSig(*e.value) + " bits", cls});
    if (e.concurrence) h.readout.push_back({"C", math::formatSig(*e.concurrence), cls});
}

std::string EntanglementView::statusLine() const {
    if (graph_.nodes.empty()) return StateView::statusLine();
    std::string s = StateView::statusLine();
    if (!s.empty()) s += "   ";
    return s + std::to_string(graph_.edges.size()) + " pairs   ΣI = " + math::formatSig(totalMutualInformation_) + " bits";
}

} // namespace qlab::viz
