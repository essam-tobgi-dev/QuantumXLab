// Spec 14 §1, §3 — circuit plumbing shared by the passes, and the per-pass metrics.
#include "Compiler/CircuitUtil.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <set>

namespace qlab::compiler {

std::string_view layoutPolicyName(LayoutPolicy p) {
    switch (p) {
    case LayoutPolicy::Trivial:
        return "trivial";
    case LayoutPolicy::Dense:
        return "dense";
    case LayoutPolicy::Vf2:
        return "vf2";
    case LayoutPolicy::NoiseAware:
        return "noise_aware";
    }
    return "?";
}
std::string_view routingPolicyName(RoutingPolicy p) {
    return p == RoutingPolicy::Sabre ? "sabre" : "none";
}
std::string_view schedulePolicyName(SchedulePolicy p) {
    return p == SchedulePolicy::Asap ? "asap" : "alap";
}

// ---------------------------------------------------------------- Layout
std::optional<std::uint32_t> Layout::virtualOf(std::uint32_t p) const {
    for (std::uint32_t v = 0; v < v2p.size(); ++v)
        if (v2p[v] == p)
            return v;
    return std::nullopt;
}
bool Layout::injective() const {
    std::set<std::uint32_t> seen(v2p.begin(), v2p.end());
    return seen.size() == v2p.size();
}
Layout Layout::identity(std::uint32_t n) {
    Layout l;
    l.v2p.resize(n);
    for (std::uint32_t i = 0; i < n; ++i)
        l.v2p[i] = i;
    return l;
}
std::string Layout::text() const {
    std::string s;
    for (std::size_t v = 0; v < v2p.size(); ++v)
        s += std::format("{}q{}->${}", v ? ", " : "", v, v2p[v]);
    return s;
}

CompileContext CompileContext::from(const CompileOptions& o, const hw::Device* dev,
                                    const hw::Calibration* cal) {
    CompileContext c;
    c.device = dev;
    c.calibration = cal;
    c.pulses = o.pulses;
    if (o.level)
        c.level = *o.level;
    if (o.layout)
        c.layout = *o.layout;
    if (o.routing)
        c.routing = *o.routing;
    if (o.pulseLevel)
        c.pulseLevel = *o.pulseLevel;
    if (o.seed)
        c.seed = *o.seed;
    c.schedule = o.schedule;
    c.loopUnrollBound = o.loopUnrollBound;
    c.twoQubitBasis = o.twoQubitBasis;
    c.kak = o.kak;
    c.verifyEquivalence = o.verifyEquivalence;
    c.equivalenceWorkLimit = o.equivalenceWorkLimit;
    c.enforcePulseQubitCap = o.enforcePulseQubitCap;
    c.pulseQubitCap = o.pulseQubitCap;
    c.vf2StateBudget = o.vf2StateBudget;
    return c;
}

// ---------------------------------------------------------------- node lists
ir::Circuit shellLike(const ir::Circuit& c) {
    ir::Circuit out;
    out.setQubitCount(c.qubitCount());
    out.setClbitCount(c.clbitCount());
    out.setPhysical(c.isPhysical());
    for (const auto& r : c.qubitRegisters())
        out.addQubitRegister(r);
    for (const auto& r : c.bitRegisters())
        out.addBitRegister(r);
    out.meta() = c.meta();
    return out;
}

std::vector<ir::Node> takeNodes(ir::Circuit& c) {
    const auto topo = c.topologicalOrder();
    const std::vector<ir::NodeId> order(topo.begin(),
                                        topo.end()); // the span dies with the first mutation
    std::vector<ir::Node> nodes;
    nodes.reserve(order.size());
    for (ir::NodeId id : order)
        nodes.push_back(std::move(c.node(id)));
    return nodes;
}

std::vector<ir::Node> copyNodes(const ir::Circuit& c) {
    std::vector<ir::Node> nodes;
    const auto topo = c.topologicalOrder();
    nodes.reserve(topo.size());
    for (ir::NodeId id : topo)
        nodes.push_back(c.node(id));
    return nodes;
}

void setNodes(ir::Circuit& c, std::vector<ir::Node> nodes) {
    ir::Circuit out = shellLike(c);
    for (auto& n : nodes)
        out.add(std::move(n));
    c = std::move(out);
}

void normalizeBodies(ir::Circuit& c) {
    bool nested = false;
    for (ir::NodeId id : std::as_const(c).topologicalOrder())
        nested = nested || isControlNode(std::as_const(c).node(id));
    if (!nested)
        return;
    std::vector<ir::Node> nodes = takeNodes(c);
    for (ir::Node& n : nodes)
        (void)forEachBody(n, [&](ir::Circuit& body) -> Status {
            body.setQubitCount(c.qubitCount());
            body.setClbitCount(c.clbitCount());
            body.setPhysical(c.isPhysical());
            normalizeBodies(body);
            return {};
        });
    setNodes(c, std::move(nodes));
}

Result<ir::Gate> gate(std::string_view name, std::vector<ir::Wire> targets,
                      std::vector<double> params, const SourceSpan& span) {
    return ir::makeGate(name, std::move(targets), std::move(params), span);
}

// ---------------------------------------------------------------- metrics
namespace {
void count(const ir::Circuit& c, PassMetrics& m) {
    for (ir::NodeId id : c.topologicalOrder()) {
        const ir::Node& n = c.node(id);
        if (const auto* g = std::get_if<ir::Gate>(&n)) {
            ++m.gateCount;
            if (g->width() == 2)
                ++m.twoQubitCount;
            if (g->name == "t" || g->name == "tdg")
                ++m.tCount;
        } else {
            forEachBody(n, [&](const ir::Circuit& body) { count(body, m); });
        }
    }
}
void collectWires(const ir::Circuit& c, std::set<std::uint32_t>& out) {
    for (ir::NodeId id : c.topologicalOrder())
        for (ir::Wire w : ir::nodeWires(c.node(id)))
            out.insert(w.index);
}
} // namespace

PassMetrics measureCircuit(const ir::Circuit& c) {
    PassMetrics m;
    count(c, m);
    m.depth = static_cast<std::uint32_t>(c.depth());
    return m;
}

std::vector<std::uint32_t> usedWires(const ir::Circuit& c) {
    std::set<std::uint32_t> s;
    collectWires(c, s); // nodeWires already descends into Branch/Loop/Box bodies
    return {s.begin(), s.end()};
}

bool hasOpaqueGate(const ir::Circuit& c) {
    for (ir::NodeId id : c.topologicalOrder()) {
        const ir::Node& n = c.node(id);
        if (const auto* g = std::get_if<ir::Gate>(&n)) {
            if (g->opaque)
                return true;
            continue;
        }
        bool found = false;
        forEachBody(n, [&](const ir::Circuit& body) { found = found || hasOpaqueGate(body); });
        if (found)
            return true;
    }
    return false;
}

std::optional<std::vector<std::uint32_t>> metaIndices(const ir::Circuit& c, std::string_view key) {
    const auto& meta = c.meta();
    const auto it = meta.find(key);
    if (it == meta.end() || !it->is_array())
        return std::nullopt;
    std::vector<std::uint32_t> out;
    for (const auto& v : *it) {
        if (!v.is_number_unsigned())
            return std::nullopt;
        out.push_back(v.get<std::uint32_t>());
    }
    return out;
}

double wrapAngle(double a, long* turns) {
    constexpr double twoPi = 2.0 * std::numbers::pi;
    // k = ceil((a − π)/2π) puts a − 2πk in (−π, π].
    const double k = std::ceil((a - std::numbers::pi) / twoPi);
    if (turns)
        *turns = static_cast<long>(k);
    return a - twoPi * k;
}

} // namespace qlab::compiler
