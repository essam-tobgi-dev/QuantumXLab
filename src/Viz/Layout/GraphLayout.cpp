// Spec 21 §3.12, §3.8 — coupling-graph and entanglement-graph models (see GraphLayout.hpp).
#include "Viz/Layout/GraphLayout.hpp"
#include "Viz/Math/Color.hpp"
#include "Viz/Math/Format.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace qlab::viz::layout {
namespace {

struct FieldValue {
    double si = 0.0;
    std::string text;
};

std::optional<FieldValue> nodeValue(const hw::QubitCal& q, NodeField f) {
    switch (f) {
    case NodeField::T1:
        return FieldValue{q.t1.value.v, math::formatTime(q.t1.value.v)};
    case NodeField::T2:
        return FieldValue{q.t2echo.value.v, math::formatTime(q.t2echo.value.v)};
    case NodeField::F01:
        return FieldValue{q.f01.value.v, math::formatFrequency(q.f01.value.v)};
    case NodeField::Anharmonicity:
        return FieldValue{q.anharmonicity.value.v, math::formatFrequency(q.anharmonicity.value.v)};
    case NodeField::Error1q:
        return FieldValue{q.gateError1q.value, math::formatSig(q.gateError1q.value)};
    case NodeField::ReadoutError:
        return FieldValue{1.0 - q.readoutFidelity(), math::formatSig(1.0 - q.readoutFidelity())};
    default:
        return std::nullopt;
    }
}

std::optional<FieldValue> edgeValue(const hw::EdgeCal& e, EdgeField f) {
    switch (f) {
    case EdgeField::Error2q:
        return FieldValue{e.gateError2q.value, math::formatSig(e.gateError2q.value)};
    case EdgeField::Duration:
        return FieldValue{e.duration.value.v, math::formatTime(e.duration.value.v)};
    default:
        return std::nullopt;
    }
}

// Fills min/max from the values present and colours each item on the sequential scale.
template <class Item>
void applyScale(std::vector<Item>& items, ScaleLegend& legend, bool skipCouplers) {
    bool any = false;
    for (const Item& it : items) {
        if (!it.value)
            continue;
        if constexpr (std::is_same_v<Item, GraphNode>)
            if (skipCouplers && it.coupler)
                continue;
        legend.min = any ? std::min(legend.min, *it.value) : *it.value;
        legend.max = any ? std::max(legend.max, *it.value) : *it.value;
        if (!any || *it.value <= legend.min)
            legend.minLabel = it.valueText;
        if (!any || *it.value >= legend.max)
            legend.maxLabel = it.valueText;
        any = true;
    }
    legend.valid = any;
    if (!any)
        return;
    for (Item& it : items)
        if (it.value)
            it.color = math::sequentialColor(legend.normalised(*it.value));
}

void computeExtent(DeviceGraph& g) {
    if (g.nodes.empty())
        return;
    g.extent = {g.nodes[0].pos.x, g.nodes[0].pos.y, g.nodes[0].pos.x, g.nodes[0].pos.y};
    for (const GraphNode& n : g.nodes)
        g.extent = g.extent.unite({n.pos.x, n.pos.y, n.pos.x, n.pos.y});
}

} // namespace

std::string_view nodeFieldName(NodeField f) {
    switch (f) {
    case NodeField::None:
        return "none";
    case NodeField::T1:
        return "T1";
    case NodeField::T2:
        return "T2 (echo)";
    case NodeField::F01:
        return "f01";
    case NodeField::Anharmonicity:
        return "anharmonicity";
    case NodeField::Error1q:
        return "1q gate error";
    case NodeField::ReadoutError:
        return "readout error";
    case NodeField::QecRole:
        return "QEC role";
    }
    return "?";
}

std::string_view edgeFieldName(EdgeField f) {
    switch (f) {
    case EdgeField::None:
        return "none";
    case EdgeField::Error2q:
        return "2q gate error";
    case EdgeField::Duration:
        return "2q gate duration";
    }
    return "?";
}

std::vector<glm::dvec2> circlePositions(std::uint32_t n) {
    std::vector<glm::dvec2> out(n);
    const double radius = std::max(
        1.0, static_cast<double>(n) / (2.0 * std::numbers::pi)); // unit arc between neighbours
    for (std::uint32_t k = 0; k < n; ++k) {
        const double a = 2.0 * std::numbers::pi * k / std::max(1u, n) - std::numbers::pi / 2.0;
        out[k] = {radius * std::cos(a), radius * std::sin(a)};
    }
    return out;
}

DeviceGraph buildCouplingGraph(const hw::Device& dev, const hw::Calibration* cal,
                               const CouplingOptions& o) {
    DeviceGraph g;
    g.nodeLegend.title = std::string(nodeFieldName(o.nodeField));
    g.edgeLegend.title = std::string(edgeFieldName(o.edgeField));
    for (const hw::QubitInfo& q : dev.qubits) {
        GraphNode n;
        n.qubit = QubitIndex{q.index};
        n.pos = {q.pos[0], q.pos[1]};
        n.coupler = q.kind == hw::QubitKind::Coupler;
        if (!n.coupler)
            ++g.dataNodes;
        if (cal)
            if (const hw::QubitCal* qc = cal->qubit(q.index))
                if (const auto v = nodeValue(*qc, o.nodeField)) {
                    n.value = v->si;
                    n.valueText = v->text;
                }
        for (std::size_t v = 0; v < o.layout.size(); ++v)
            if (o.layout[v] == q.index)
                n.virtualQubit = static_cast<std::uint32_t>(v);
        g.nodes.push_back(std::move(n));
    }
    const auto addEdge = [&](std::uint32_t a, std::uint32_t b, bool directed,
                             std::optional<std::uint32_t> coupler) {
        GraphEdge e;
        e.a = QubitIndex{a};
        e.b = QubitIndex{b};
        e.directed = directed;
        if (coupler)
            e.coupler = QubitIndex{*coupler};
        if (cal)
            if (const hw::EdgeCal* ec = cal->edge(a, b))
                if (const auto v = edgeValue(*ec, o.edgeField)) {
                    e.value = v->si;
                    e.valueText = v->text;
                }
        g.edges.push_back(std::move(e));
    };
    if (dev.allToAll) { // ion chain: one "*-*" entry stands for every pair
        const auto data = dev.dataQubits();
        for (std::size_t i = 0; i < data.size(); ++i)
            for (std::size_t j = i + 1; j < data.size(); ++j)
                addEdge(data[i], data[j], false, std::nullopt);
    } else {
        for (const hw::EdgeInfo& e : dev.edges) {
            if (e.kind == hw::EdgeKind::AllToAll)
                continue;
            addEdge(e.directed ? e.a : std::min(e.a, e.b), e.directed ? e.b : std::max(e.a, e.b),
                    e.directed, e.coupler);
        }
    }
    applyScale(g.nodes, g.nodeLegend, true);
    applyScale(g.edges, g.edgeLegend, false);
    if (o.code) { // QEC overlay: roles and the live syndrome, for qubits bound to the chip
        for (const qec::RoleEntry& r : o.code->qubits) {
            if (!r.physical || r.physical->get() >= g.nodes.size())
                continue;
            GraphNode& n = g.nodes[r.physical->get()];
            n.role =
                r.role; // the view colours roles from the theme's qubit palette (paletteIndexOf)
            if (r.role != qec::QubitRole::Data && r.check < o.code->syndrome.size())
                n.syndromeFired = o.code->syndrome[r.check] != 0;
        }
        if (o.nodeField == NodeField::QecRole)
            g.nodeLegend.title = "QEC role (" + o.code->codeId + ")";
    }
    computeExtent(g);
    return g;
}

DeviceGraph buildEntanglementGraph(const hw::Device* dev, std::uint32_t nQubits,
                                   const Reductions& red) {
    DeviceGraph g;
    g.nodeLegend = {"S(rho_i)", "0 bit", "1 bit", 0.0, 1.0, true};
    g.edgeLegend = {"I(i:j)", "0 bit", "2 bit", 0.0, 2.0, true};
    const bool useDevice = dev && dev->qubitCount() >= nQubits;
    const auto fallback = circlePositions(nQubits);
    for (std::uint32_t q = 0; q < nQubits; ++q) {
        GraphNode n;
        n.qubit = QubitIndex{q};
        n.pos = useDevice ? glm::dvec2(dev->qubits[q].pos[0], dev->qubits[q].pos[1]) : fallback[q];
        if (const SingleReduction* s = red.single(QubitIndex{q})) {
            n.value = s->entropyBits; // 0 → neutral, 1 bit → accent: the view maps value → colour
            n.valueText = math::formatWithUnit(s->entropyBits, "bit");
            n.color = math::sequentialColor(std::clamp(s->entropyBits, 0.0, 1.0));
        }
        g.nodes.push_back(std::move(n));
    }
    g.dataNodes = nQubits;
    for (const PairReduction& p : red.pairs) {
        if (p.i.get() >= nQubits || p.j.get() >= nQubits)
            continue;
        GraphEdge e;
        e.a = p.i;
        e.b = p.j;
        e.value = p.measures.mutualInformation;
        e.valueText = math::formatWithUnit(p.measures.mutualInformation, "bit");
        e.weight =
            std::clamp(p.measures.mutualInformation / 2.0, 0.0, 1.0); // width and opacity ∝ I/2
        if (p.measures.mutualInformation > 0.01)
            e.concurrence = p.measures.concurrence; // labelled only when I > 0.01
        e.color = math::sequentialColor(e.weight);
        g.edges.push_back(std::move(e));
    }
    computeExtent(g);
    return g;
}

std::optional<std::size_t> DeviceGraph::nodeAt(glm::dvec2 p, double radius) const {
    std::optional<std::size_t> best;
    double bestD = radius;
    for (std::size_t k = 0; k < nodes.size(); ++k) {
        const double d = glm::length(nodes[k].pos - p);
        if (d <= bestD) {
            bestD = d;
            best = k;
        }
    }
    return best;
}

std::optional<std::size_t> DeviceGraph::edgeAt(glm::dvec2 p, double tolerance) const {
    std::optional<std::size_t> best;
    double bestD = tolerance;
    for (std::size_t k = 0; k < edges.size(); ++k) {
        if (edges[k].a.get() >= nodes.size() || edges[k].b.get() >= nodes.size())
            continue;
        const glm::dvec2 a = nodes[edges[k].a.get()].pos, b = nodes[edges[k].b.get()].pos;
        const glm::dvec2 ab = b - a;
        const double len2 = glm::dot(ab, ab);
        const double t = len2 > 0.0 ? std::clamp(glm::dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
        const double d = glm::length(p - (a + t * ab));
        if (d <= bestD) {
            bestD = d;
            best = k;
        }
    }
    return best;
}

} // namespace qlab::viz::layout
