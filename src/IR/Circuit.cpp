// Spec 14 §3 — Circuit storage, wire indices, and metrics.
#include "IR/Circuit.hpp"
#include <algorithm>
#include <format>

namespace qlab::ir {

void Circuit::setQubitCount(std::uint32_t n) {
    qubits_ = n;
    invalidate();
}

void Circuit::invalidate() const { dirty_ = true; }

NodeId Circuit::add(Node n) {
    const auto id = NodeId{static_cast<std::uint32_t>(nodes_.size())};
    nodes_.push_back(std::move(n));
    alive_.push_back(true);
    order_.push_back(id);
    invalidate();
    return id;
}

NodeId Circuit::insertBefore(NodeId at, Node n) {
    const auto id = NodeId{static_cast<std::uint32_t>(nodes_.size())};
    nodes_.push_back(std::move(n));
    alive_.push_back(true);
    auto it = std::find(order_.begin(), order_.end(), at);
    order_.insert(it, id);
    invalidate();
    return id;
}

NodeId Circuit::insertAfter(NodeId at, Node n) {
    const auto id = NodeId{static_cast<std::uint32_t>(nodes_.size())};
    nodes_.push_back(std::move(n));
    alive_.push_back(true);
    auto it = std::find(order_.begin(), order_.end(), at);
    if (it != order_.end()) ++it;
    order_.insert(it, id);
    invalidate();
    return id;
}

void Circuit::erase(NodeId id) {
    if (id.get() < alive_.size() && alive_[id.get()]) {
        alive_[id.get()] = false;
        invalidate();
    }
}

bool Circuit::alive(NodeId id) const { return id.get() < alive_.size() && alive_[id.get()]; }
std::size_t Circuit::nodeCount() const {
    return static_cast<std::size_t>(std::count(alive_.begin(), alive_.end(), true));
}
const Node& Circuit::node(NodeId id) const { return nodes_[id.get()]; }
Node& Circuit::node(NodeId id) { invalidate(); return nodes_[id.get()]; }

void Circuit::rebuild() const {
    topo_.clear();
    byWire_.assign(qubits_, {});
    byBit_.assign(clbits_, {});
    for (NodeId id : order_) {
        if (!alive_[id.get()]) continue;
        topo_.push_back(id);
        const Node& n = nodes_[id.get()];
        for (Wire w : nodeWiresIn(n, qubits_))
            if (w.index < byWire_.size()) byWire_[w.index].push_back(id);
        for (auto b : nodeWrites(n))
            if (b.index < byBit_.size()) byBit_[b.index].push_back(id);
        for (auto b : nodeReads(n))
            if (b.index < byBit_.size() && (byBit_[b.index].empty() || byBit_[b.index].back() != id))
                byBit_[b.index].push_back(id);
    }
    dirty_ = false;
}

std::span<const NodeId> Circuit::topologicalOrder() const {
    if (dirty_) rebuild();
    return topo_;
}
std::span<const NodeId> Circuit::onWire(Wire w) const {
    if (dirty_) rebuild();
    static const std::vector<NodeId> empty;
    return w.index < byWire_.size() ? std::span<const NodeId>(byWire_[w.index]) : std::span<const NodeId>(empty);
}
std::span<const NodeId> Circuit::onClassicalBit(ClassicalBit b) const {
    if (dirty_) rebuild();
    static const std::vector<NodeId> empty;
    return b.index < byBit_.size() ? std::span<const NodeId>(byBit_[b.index]) : std::span<const NodeId>(empty);
}

std::string Circuit::wireName(Wire w) const {
    if (physical_) return std::format("${}", w.index);
    for (const auto& r : qregs_)
        if (w.index >= r.first && w.index < r.first + r.size)
            return r.scalar ? r.name : std::format("{}[{}]", r.name, w.index - r.first);
    return std::format("q[{}]", w.index);
}
std::string Circuit::bitName(ClassicalBit b) const {
    for (const auto& r : cregs_)
        if (b.index >= r.first && b.index < r.first + r.size)
            return r.scalar ? r.name : std::format("{}[{}]", r.name, b.index - r.first);
    return std::format("c[{}]", b.index);
}

void Circuit::setLayout(std::vector<std::uint32_t> virtualToPhysical) {
    meta_["layout"] = virtualToPhysical;
}
std::vector<std::uint32_t> Circuit::layout() const {
    if (!meta_.contains("layout")) return {};
    return meta_["layout"].get<std::vector<std::uint32_t>>();
}

// ---------------------------------------------------------------- metrics
std::vector<std::vector<NodeId>> Circuit::layers() const {
    std::vector<std::uint32_t> wireLayer(qubits_, 0), bitLayer(clbits_, 0);
    std::vector<std::vector<NodeId>> out;
    for (NodeId id : topologicalOrder()) {
        const Node& n = nodes_[id.get()];
        const auto ws = nodeWiresIn(n, qubits_);
        std::uint32_t lvl = 0;
        for (Wire w : ws)
            if (w.index < wireLayer.size()) lvl = std::max(lvl, wireLayer[w.index]);
        auto touchBits = nodeWrites(n);
        for (auto b : nodeReads(n)) touchBits.push_back(b);
        for (auto b : touchBits)
            if (b.index < bitLayer.size()) lvl = std::max(lvl, bitLayer[b.index]);
        for (Wire w : ws)
            if (w.index < wireLayer.size()) wireLayer[w.index] = lvl + 1;
        for (auto b : touchBits)
            if (b.index < bitLayer.size()) bitLayer[b.index] = lvl + 1;
        if (out.size() <= lvl) out.resize(lvl + 1);
        out[lvl].push_back(id);
    }
    return out;
}

std::size_t Circuit::depth() const {
    // T02 §7: depth = number of moments of an ASAP schedule. Directives (barrier, delay) and
    // wire-less gates are skipped; classical nodes pass bit levels on without adding a moment, so a
    // Branch still lands after the measurement it reads.
    std::vector<std::size_t> wireLevel(qubits_, 0), bitLevel(clbits_, 0);
    std::size_t depth = 0;
    for (NodeId id : topologicalOrder()) {
        const Node& n = nodes_[id.get()];
        if (std::holds_alternative<Barrier>(n) || std::holds_alternative<Delay>(n)) continue;
        const auto ws = nodeWires(n);
        auto bits = nodeWrites(n);
        for (auto b : nodeReads(n)) bits.push_back(b);
        if (ws.empty() && bits.empty()) continue;
        std::size_t lvl = 0;
        for (Wire w : ws) if (w.index < wireLevel.size()) lvl = std::max(lvl, wireLevel[w.index]);
        for (auto b : bits) if (b.index < bitLevel.size()) lvl = std::max(lvl, bitLevel[b.index]);
        const bool moment = !ws.empty() && !std::holds_alternative<ClassicalOp>(n);
        if (moment) ++lvl;
        for (Wire w : ws) if (w.index < wireLevel.size()) wireLevel[w.index] = lvl;
        for (auto b : bits) if (b.index < bitLevel.size()) bitLevel[b.index] = lvl;
        depth = std::max(depth, lvl);
    }
    return depth;
}

std::size_t Circuit::size() const {
    std::size_t n = 0;
    for (NodeId id : topologicalOrder())
        if (isQuantum(nodes_[id.get()])) ++n;
    return n;
}

std::size_t Circuit::twoQubitCount() const {
    std::size_t n = 0;
    for (NodeId id : topologicalOrder())
        if (const auto* g = std::get_if<Gate>(&nodes_[id.get()])) {
            if (g->width() == 2) ++n;
        }
    return n;
}

std::size_t Circuit::tCount() const {
    std::size_t n = 0;
    for (NodeId id : topologicalOrder())
        if (const auto* g = std::get_if<Gate>(&nodes_[id.get()]))
            if (g->name == "t" || g->name == "tdg") ++n;
    return n;
}

std::map<std::string, std::size_t> Circuit::gateCounts() const {
    std::map<std::string, std::size_t> m;
    for (NodeId id : topologicalOrder()) {
        const Node& n = nodes_[id.get()];
        if (const auto* g = std::get_if<Gate>(&n)) ++m[g->name];
        else ++m[std::string(nodeKindName(n))];
    }
    return m;
}

bool Circuit::hasMeasurement() const {
    for (NodeId id : topologicalOrder())
        if (std::holds_alternative<Measure>(nodes_[id.get()])) return true;
    return false;
}
bool Circuit::hasClassicalControl() const {
    for (NodeId id : topologicalOrder()) {
        const Node& n = nodes_[id.get()];
        if (std::holds_alternative<Branch>(n) || std::holds_alternative<Loop>(n) ||
            std::holds_alternative<ClassicalOp>(n))
            return true;
        if (const auto* b = std::get_if<Box>(&n))
            if ((*b->body).hasClassicalControl()) return true;
    }
    return false;
}
bool Circuit::isPureUnitary() const {
    for (NodeId id : topologicalOrder()) {
        const Node& n = nodes_[id.get()];
        if (std::holds_alternative<Measure>(n) || std::holds_alternative<Reset>(n) ||
            std::holds_alternative<Branch>(n) || std::holds_alternative<Loop>(n) ||
            std::holds_alternative<ClassicalOp>(n))
            return false;
        if (const auto* b = std::get_if<Box>(&n))
            if (!(*b->body).isPureUnitary()) return false;
    }
    return true;
}

} // namespace qlab::ir
