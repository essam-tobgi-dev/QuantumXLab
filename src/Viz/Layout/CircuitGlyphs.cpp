// Spec 21 §3.13 — what each IR node looks like: glyph kind, label, parameter text and the rows it
// occupies. Positions are assigned by CircuitLayout.cpp.
#include "Viz/Layout/CircuitLayoutImpl.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Math/Phase.hpp"
#include <algorithm>

namespace qlab::viz::layout {

std::string gateParamText(const ir::Gate& g) {
    std::string s;
    for (std::size_t k = 0; k < g.params.size(); ++k) {
        if (k) s += ", ";
        std::string angle = math::formatAngle(g.params[k]); // multiples of π up to q = 16, else 4 s.f. radians
        if (const auto pos = angle.find(" rad"); pos != std::string::npos) angle.erase(pos); // boxes print bare radians
        s += angle;
    }
    return s;
}

std::vector<NodeTiming> scheduleTimes(const ir::Circuit& c) {
    std::vector<NodeTiming> out;
    const auto& meta = c.meta();
    const auto it = meta.find("schedule");
    if (it == meta.end() || !it->is_object()) return out;
    const auto start = it->find("start_ps"), length = it->find("length_ps");
    if (start == it->end() || length == it->end() || !start->is_array() || !length->is_array()) return out;
    const std::size_t n = std::min(start->size(), length->size());
    if (n != c.topologicalOrder().size()) return {}; // stale record: the circuit changed after scheduling
    out.reserve(n);
    for (std::size_t k = 0; k < n; ++k) {
        if (!(*start)[k].is_number() || !(*length)[k].is_number()) return {};
        out.push_back({(*start)[k].get<double>() * 1e-3, (*length)[k].get<double>() * 1e-3}); // ps → ns
    }
    return out;
}

std::size_t countSwaps(const ir::Circuit& c) {
    std::size_t n = 0;
    for (ir::NodeId id : c.topologicalOrder()) {
        const ir::Node& node = c.node(id);
        if (const auto* g = std::get_if<ir::Gate>(&node)) n += g->name == "swap" ? 1 : 0;
        for (const auto& body : detail::bodiesOf(node)) n += countSwaps(*body.circuit);
    }
    return n;
}

namespace detail {

std::optional<std::uint32_t> classicalRowOf(const ir::Circuit& top, std::uint32_t bit) {
    const auto& regs = top.bitRegisters();
    for (std::size_t r = 0; r < regs.size(); ++r)
        if (bit >= regs[r].first && bit < regs[r].first + regs[r].size) return top.qubitCount() + static_cast<std::uint32_t>(r);
    return std::nullopt;
}

std::vector<Body> bodiesOf(const ir::Node& node) {
    std::vector<Body> out;
    if (const auto* b = std::get_if<ir::Branch>(&node)) {
        if (b->thenBody.present()) out.push_back({&*b->thenBody, ""});
        if (b->elseBody.present() && b->elseBody->nodeCount() > 0) out.push_back({&*b->elseBody, "else"});
    } else if (const auto* l = std::get_if<ir::Loop>(&node)) {
        if (l->body.present()) out.push_back({&*l->body, ""});
    } else if (const auto* x = std::get_if<ir::Box>(&node)) {
        if (x->body.present()) out.push_back({&*x->body, ""});
    }
    return out;
}

namespace {

std::string durationText(const ir::Duration& d) {
    std::string s;
    if (d.ps.get() != 0 || d.dt == 0) s = math::formatTime(static_cast<double>(d.ps.get()) * 1e-12);
    if (d.dt != 0) s += (s.empty() ? "" : " + ") + std::to_string(d.dt) + " dt";
    return s;
}

void setRows(Glyph& g, std::uint32_t extraMax = 0) {
    std::uint32_t lo = 0xFFFFFFFFu, hi = 0;
    for (auto r : g.targetRows) lo = std::min(lo, r), hi = std::max(hi, r);
    for (auto r : g.controlRows) lo = std::min(lo, r), hi = std::max(hi, r);
    g.rowMin = lo == 0xFFFFFFFFu ? 0 : lo;
    g.rowMax = std::max(hi, extraMax);
}

Glyph gateGlyph(const ir::Gate& gate) {
    Glyph g;
    // A named controlled gate is its base gate plus leading control operands (T02 §3): cx = ctrl @ x.
    std::string base = gate.name;
    std::size_t leading = 0;
    if (const auto split = ir::gates::splitControlled(gate.name, gate.params)) {
        base = split->base.name;
        leading = std::min(split->controls, gate.targets.size());
    }
    for (std::size_t k = 0; k < gate.targets.size(); ++k)
        (k < leading ? g.controlRows : g.targetRows).push_back(gate.targets[k].index);
    g.negControl.assign(g.controlRows.size(), 0);
    for (std::size_t j = 0; j < gate.controls.size(); ++j) {
        g.controlRows.push_back(gate.controls[j].index);
        g.negControl.push_back(gate.isNegControl(j) ? 1 : 0);
    }
    const bool controlled = !g.controlRows.empty();
    if (base == "x" && controlled) g.kind = GlyphKind::Cx;
    else if (base == "z" && controlled) g.kind = GlyphKind::Cz;
    else if (base == "swap") g.kind = GlyphKind::Swap;
    else g.kind = GlyphKind::Box;
    g.label = base;
    if (gate.adjoint) g.label += "\xE2\x80\xA0"; // †
    // The base gate keeps its own parameters (cp(λ) → p(λ)); a split that changes them (cu) keeps the node's.
    g.params = gateParamText(gate);
    setRows(g);
    return g;
}

} // namespace

std::optional<Glyph> describeNode(const ir::Circuit& top, const ir::Node& node, std::uint32_t qubitRows) {
    Glyph g;
    if (const auto* gate = std::get_if<ir::Gate>(&node)) {
        if (gate->targets.empty() && gate->controls.empty()) return std::nullopt; // gphase: nothing to draw on a wire
        return gateGlyph(*gate);
    }
    if (const auto* m = std::get_if<ir::Measure>(&node)) {
        g.kind = GlyphKind::Measure;
        g.label = "measure";
        g.targetRows = {m->qubit.index};
        std::uint32_t reach = 0;
        if (!m->discards()) {
            g.clbit = m->bit.index;
            g.classicalRow = classicalRowOf(top, m->bit.index);
            g.params = top.bitName(m->bit);
            // The double line runs down to its register: every wire below is crossed (occupied).
            reach = g.classicalRow.value_or(qubitRows > 0 ? qubitRows - 1 : 0);
        }
        setRows(g, reach);
        return g;
    }
    if (const auto* r = std::get_if<ir::Reset>(&node)) {
        g.kind = GlyphKind::Reset;
        g.label = "|0>";
        g.targetRows = {r->qubit.index};
        setRows(g);
        return g;
    }
    if (std::holds_alternative<ir::Barrier>(node)) {
        g.kind = GlyphKind::Barrier;
        for (ir::Wire w : ir::nodeWiresIn(node, qubitRows)) g.targetRows.push_back(w.index); // empty = every wire
        if (g.targetRows.empty()) return std::nullopt;
        setRows(g);
        return g;
    }
    if (const auto* d = std::get_if<ir::Delay>(&node)) {
        g.kind = GlyphKind::Delay;
        g.label = "delay";
        g.params = durationText(d->duration);
        for (ir::Wire w : ir::nodeWiresIn(node, qubitRows)) g.targetRows.push_back(w.index);
        if (g.targetRows.empty()) return std::nullopt;
        setRows(g);
        return g;
    }
    if (const auto* c = std::get_if<ir::ClassicalOp>(&node)) {
        g.kind = GlyphKind::Classical;
        g.label = c->dst.text() + " = " + c->expr.text();
        g.classicalRow = classicalRowOf(top, c->dst.reg.first);
        if (!g.classicalRow) return std::nullopt;
        g.rowMin = g.rowMax = *g.classicalRow;
        return g;
    }
    // Control nodes: a bracketed region over the wires of their bodies (spec 21 §3.13).
    g.kind = GlyphKind::Region;
    if (const auto* br = std::get_if<ir::Branch>(&node)) g.label = "if (" + br->cond.text() + ")";
    else if (const auto* lp = std::get_if<ir::Loop>(&node)) g.label = "while (" + lp->cond.text() + ")";
    else if (const auto* bx = std::get_if<ir::Box>(&node)) g.label = bx->duration ? "box[" + durationText(*bx->duration) + "]" : "box";
    for (ir::Wire w : ir::nodeWires(node)) g.targetRows.push_back(w.index);
    if (g.targetRows.empty()) return std::nullopt;
    setRows(g);
    return g;
}

double glyphWidth(const Glyph& g) {
    switch (g.kind) {
    case GlyphKind::Cx:
    case GlyphKind::Cz:
    case GlyphKind::Swap: return 0.6;
    case GlyphKind::Barrier: return 0.3;
    case GlyphKind::Region: return 0.0; // sized by its contents
    default: break;
    }
    // ~0.13 units per character at the diagram's font size, one line for the name, one for parameters.
    const std::size_t chars = std::max(g.label.size(), g.params.size());
    return std::clamp(0.5 + 0.13 * static_cast<double>(chars), 0.7, 4.0);
}

} // namespace detail
} // namespace qlab::viz::layout
