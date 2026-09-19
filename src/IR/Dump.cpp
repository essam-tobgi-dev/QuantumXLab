// Spec 14 §3 — deterministic text dump of a circuit (tests, diffs, the Compiler panel's IR view).
// Wires print as q[i] and bits as c[i] in the flat index spaces; spans and metadata are omitted.
#include "IR/Circuit.hpp"
#include "IR/Emit.hpp"
#include <format>

namespace qlab::ir {
namespace {

const char* kindName(RegKind k) {
    switch (k) {
    case RegKind::Bit:
        return "bit";
    case RegKind::Bool:
        return "bool";
    case RegKind::Int:
        return "int";
    case RegKind::Uint:
        return "uint";
    }
    return "?";
}

std::string durationText(const Duration& d) {
    if (d.dt == 0)
        return std::format("{}ps", d.ps.get());
    if (d.ps.get() == 0)
        return std::format("{}dt", d.dt);
    return std::format("{}ps{:+}dt", d.ps.get(), d.dt);
}

std::string wireList(const std::vector<Wire>& ws) {
    if (ws.empty())
        return "*";
    std::string s;
    for (std::size_t i = 0; i < ws.size(); ++i)
        s += std::format("{}q[{}]", i ? ", " : "", ws[i].index);
    return s;
}

std::string gateText(const Gate& g) {
    std::string s;
    for (std::size_t j = 0; j < g.controls.size(); ++j)
        s += std::format("{}(q[{}]) ", g.isNegControl(j) ? "negctrl" : "ctrl", g.controls[j].index);
    if (g.adjoint)
        s += "inv ";
    if (g.opaque)
        s += "opaque ";
    s += g.name;
    if (!g.params.empty()) {
        s += "(";
        for (std::size_t k = 0; k < g.params.size(); ++k)
            s += (k ? ", " : "") + detail::formatReal(g.params[k]);
        s += ")";
    }
    if (g.custom) {
        s += "[";
        for (std::size_t r = 0; r < g.custom->rows; ++r)
            for (std::size_t c = 0; c < g.custom->cols; ++c) {
                const auto z = (*g.custom)(r, c);
                s += std::format("{}{}{:+}i", c ? " " : (r ? "; " : ""),
                                 detail::formatReal(z.real()), z.imag());
            }
        s += "]";
    }
    if (!g.targets.empty())
        s += " " + wireList(g.targets);
    return s;
}

std::string timed(const std::optional<Picoseconds>& d) {
    return d ? std::format(" @{}ps", d->get()) : std::string();
}

void dumpBody(const Circuit& c, int depth, std::string& out) {
    const std::string pad(static_cast<std::size_t>(2 * depth), ' ');
    for (NodeId id : c.topologicalOrder()) {
        const Node& n = c.node(id);
        out += pad;
        if (const auto* g = std::get_if<Gate>(&n)) {
            out += gateText(*g) + timed(g->duration);
        } else if (const auto* m = std::get_if<Measure>(&n)) {
            out += std::format("measure q[{}] -> {}", m->qubit.index,
                               m->discards() ? "discard" : std::format("c[{}]", m->bit.index));
            out += timed(m->duration);
        } else if (const auto* r = std::get_if<Reset>(&n)) {
            out += std::format("reset q[{}]", r->qubit.index) + timed(r->duration);
        } else if (const auto* b = std::get_if<Barrier>(&n)) {
            out += "barrier " + wireList(b->wires);
        } else if (const auto* d = std::get_if<Delay>(&n)) {
            out += std::format("delay[{}] {}", durationText(d->duration), wireList(d->wires));
        } else if (const auto* co = std::get_if<ClassicalOp>(&n)) {
            out += std::format("{} = {}", co->dst.text(), co->expr.text());
        } else if (const auto* br = std::get_if<Branch>(&n)) {
            out += std::format("if {} {{\n", br->cond.text());
            dumpBody(*br->thenBody, depth + 1, out);
            if ((*br->elseBody).nodeCount() > 0) {
                out += pad + "} else {\n";
                dumpBody(*br->elseBody, depth + 1, out);
            }
            out += pad + "}";
        } else if (const auto* l = std::get_if<Loop>(&n)) {
            out += std::format("while {} max={} {{\n", l->cond.text(), l->maxIterations);
            dumpBody(*l->body, depth + 1, out);
            out += pad + "}";
        } else if (const auto* bx = std::get_if<Box>(&n)) {
            out += bx->duration ? std::format("box[{}] {{\n", durationText(*bx->duration))
                                : std::string("box {\n");
            dumpBody(*bx->body, depth + 1, out);
            out += pad + "}";
        }
        out += "\n";
    }
}
} // namespace

std::string dump(const Circuit& c) {
    std::string out = std::format("circuit qubits={} clbits={} {}\n", c.qubitCount(),
                                  c.clbitCount(), c.isPhysical() ? "physical" : "virtual");
    for (const auto& r : c.qubitRegisters())
        out +=
            std::format("qreg {}[{}] @{}{}\n", r.name, r.size, r.first, r.scalar ? " scalar" : "");
    for (const auto& r : c.bitRegisters())
        out += std::format("creg {}[{}] @{} {}{}\n", r.name, r.size, r.first, kindName(r.kind),
                           r.scalar ? " scalar" : "");
    dumpBody(c, 0, out);
    return out;
}

} // namespace qlab::ir
