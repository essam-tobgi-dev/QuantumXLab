// Spec 14 §8 — routing driver: bidirectional SABRE passes, emission on physical wires, bodies of
// control nodes routed with the layout restored at their end, seeds compared by (swaps, depth).
#include "Compiler/Route.hpp"
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/LayoutPass.hpp"
#include "Compiler/SabreImpl.hpp"
#include <format>

namespace qlab::compiler {
namespace {
using detail::FullLayout;

ir::Circuit physicalShell(const ir::Circuit& like, const CouplingGraph& g) {
    ir::Circuit out;
    out.setQubitCount(g.qubitCount());
    out.setClbitCount(like.clbitCount());
    out.setPhysical(true);
    for (const auto& r : like.bitRegisters()) out.addBitRegister(r);   // qubit registers name virtual wires: dropped
    out.meta() = like.meta();
    return out;
}

void remap(std::vector<ir::Wire>& ws, const FullLayout& l) {
    for (ir::Wire& w : ws) w = ir::Wire{l.l2p[w.index]};
}

Status requireRoutable(const ir::Circuit& c) {
    for (ir::NodeId id : c.topologicalOrder()) {
        const ir::Node& n = c.node(id);
        if (const auto* g = std::get_if<ir::Gate>(&n)) {
            if (g->width() > 2)
                return fail(Error(err::Unsupported, std::format("gate '{}' acts on {} qubits; decompose before routing", g->name, g->width())).withSpan(g->span));
            continue;
        }
        Status st;
        forEachBody(n, [&](const ir::Circuit& body) { if (st) st = requireRoutable(body); });
        QXL_TRY(st);
    }
    return {};
}

struct Router {
    const CouplingGraph& g;
    const RouteOptions& o;
    std::stop_token stop;
    std::uint32_t swaps = 0;

    // Routes one level from `layout` (updated) into a physical circuit; `swapLog` receives the
    // swaps of this level so that a body can undo them.
    Result<ir::Circuit> level(const ir::Circuit& c, FullLayout& layout, std::uint64_t seed,
                              std::vector<std::pair<std::uint32_t, std::uint32_t>>* swapLog) {
        const detail::Dag dag = detail::buildDag(c);
        detail::Sabre sabre(g, o, seed);
        std::vector<const ir::Node*> source;
        for (ir::NodeId id : c.topologicalOrder()) source.push_back(&c.node(id));
        ir::Circuit out = physicalShell(c, g);
        auto onNode = [&](std::uint32_t i, const FullLayout& l) -> Status {
            ir::Node n = *source[i];
            if (auto* gt = std::get_if<ir::Gate>(&n)) { remap(gt->targets, l); remap(gt->controls, l); }
            else if (auto* m = std::get_if<ir::Measure>(&n)) m->qubit = ir::Wire{l.l2p[m->qubit.index]};
            else if (auto* r = std::get_if<ir::Reset>(&n)) r->qubit = ir::Wire{l.l2p[r->qubit.index]};
            else if (auto* b = std::get_if<ir::Barrier>(&n)) remap(b->wires, l);
            else if (auto* d = std::get_if<ir::Delay>(&n)) remap(d->wires, l);
            else
                QXL_TRY(forEachBody(n, [&](ir::Circuit& body) -> Status {
                    FullLayout inner = l;   // every arm starts from, and returns to, the layout at the node
                    std::vector<std::pair<std::uint32_t, std::uint32_t>> log;
                    QXL_TRY_ASSIGN(ir::Circuit routed, level(body, inner, seed + 0x9E3779B97F4A7C15ull, &log));
                    for (auto it = log.rbegin(); it != log.rend(); ++it) {
                        QXL_TRY_ASSIGN(ir::Gate undo, gate("swap", {ir::Wire{it->first}, ir::Wire{it->second}}, {}, ir::nodeSpan(n)));
                        routed.add(std::move(undo));
                        ++swaps;
                    }
                    body = std::move(routed);
                    return {};
                }));
            out.add(std::move(n));
            return {};
        };
        auto onSwap = [&](std::uint32_t p, std::uint32_t q, std::uint32_t enabled) -> Status {
            QXL_TRY_ASSIGN(ir::Gate s, gate("swap", {ir::Wire{p}, ir::Wire{q}}, {}, ir::nodeSpan(*source[enabled])));
            out.add(std::move(s));
            if (swapLog) swapLog->emplace_back(p, q);
            ++swaps;
            return {};
        };
        QXL_TRY(sabre.run(dag, false, layout, onNode, onSwap, stop));
        return out;
    }
};

RoutingResult finish(ir::Circuit circuit, Layout initial, Layout last, std::uint32_t swaps) {
    circuit.setLayout(initial.v2p);
    circuit.meta()["final_layout"] = last.v2p;
    circuit.meta()["swap_count"] = swaps;
    return RoutingResult{std::move(circuit), std::move(initial), std::move(last), swaps};
}
} // namespace

Result<RoutingResult> route(const ir::Circuit& c, const Layout& initial, const CouplingGraph& g, const RouteOptions& o,
                            std::stop_token stop) {
    if (c.isPhysical()) return fail(ErrorCode::InvalidArgument, "route takes a circuit on virtual qubits");
    QXL_TRY(validateLayout(initial, c.qubitCount(), g));
    QXL_TRY(requireRoutable(c));
    std::optional<RoutingResult> best;
    std::size_t bestDepth = 0;
    for (std::uint32_t trial = 0; trial < std::max<std::uint32_t>(o.trials, 1); ++trial) {
        Router r{g, o, stop};
        FullLayout layout = FullLayout::from(initial, g);
        const std::uint64_t seed = o.seed + 0xD1B54A32D192ED03ull * trial;
        FullLayout start = layout;
        if (o.bidirectional) {   // forward, then backward from the layout it ends in (spec 14 §8);
                                 // the refined layout is what the emitting pass starts from
            const detail::Dag dag = detail::buildDag(c);
            detail::Sabre sabre(g, o, seed);
            QXL_TRY(sabre.run(dag, false, start, {}, {}, stop));
            QXL_TRY(sabre.run(dag, true, start, {}, {}, stop));
        }
        layout = start;
        QXL_TRY_ASSIGN(ir::Circuit routed, r.level(c, layout, seed, nullptr));
        const std::size_t depth = routed.depth();
        if (!best || r.swaps < best->swaps || (r.swaps == best->swaps && depth < bestDepth)) {
            best = finish(std::move(routed), start.program(c.qubitCount()), layout.program(c.qubitCount()), r.swaps);
            bestDepth = depth;
        }
    }
    return std::move(*best);
}

Status checkCoupling(const ir::Circuit& physical, const CouplingGraph& g) {
    for (ir::NodeId id : physical.topologicalOrder()) {
        const ir::Node& n = physical.node(id);
        for (ir::Wire w : ir::nodeWires(n))
            if (!g.isData(w.index))
                return fail(Error(err::BadLayout, std::format("${} is not a data qubit of the device", w.index)).withSpan(ir::nodeSpan(n)));
        if (const auto* gate2 = std::get_if<ir::Gate>(&n)) {
            if (gate2->opaque) continue;   // a defcal names its own physical qubits (spec 13 §5)
            if (gate2->width() > 2)
                return fail(Error(err::Unsupported, std::format("gate '{}' acts on {} qubits; decompose before checking the coupling map", gate2->name, gate2->width())).withSpan(gate2->span));
            const auto ws = gate2->wires();
            if (ws.size() == 2 && !g.adjacent(ws[0].index, ws[1].index)) {
                lang::Diagnostic d = lang::Diagnostics::make("QL4030", gate2->span);
                d.error.withNote(std::format("'{}' acts on ${} and ${}, which are not coupled", gate2->name, ws[0].index, ws[1].index));
                d.error.withNote("help: enable routing with 'pragma qlab.routing sabre' or place the gate on a coupled pair");
                return fail(std::move(d.error));
            }
            continue;
        }
        Status st;
        forEachBody(n, [&](const ir::Circuit& body) { if (st) st = checkCoupling(body, g); });
        QXL_TRY(st);
    }
    return {};
}

namespace {
// Renames wires by `layout` on every level; the circuit becomes physical with the device's width.
Result<ir::Circuit> renamed(const ir::Circuit& c, const FullLayout& layout, const CouplingGraph& g) {
    ir::Circuit out = physicalShell(c, g);
    for (ir::NodeId id : c.topologicalOrder()) {
        ir::Node n = c.node(id);
        for (ir::Wire w : ir::nodeWires(n))
            if (w.index >= layout.l2p.size())
                return fail(Error(err::BadLayout, std::format("qubit {} does not exist on the {}-qubit device", w.index, g.qubitCount())).withSpan(ir::nodeSpan(n)));
        if (auto* gt = std::get_if<ir::Gate>(&n)) { remap(gt->targets, layout); remap(gt->controls, layout); }
        else if (auto* m = std::get_if<ir::Measure>(&n)) m->qubit = ir::Wire{layout.l2p[m->qubit.index]};
        else if (auto* r = std::get_if<ir::Reset>(&n)) r->qubit = ir::Wire{layout.l2p[r->qubit.index]};
        else if (auto* b = std::get_if<ir::Barrier>(&n)) remap(b->wires, layout);
        else if (auto* d = std::get_if<ir::Delay>(&n)) remap(d->wires, layout);
        else
            QXL_TRY(forEachBody(n, [&](ir::Circuit& body) -> Status {
                QXL_TRY_ASSIGN(ir::Circuit inner, renamed(body, layout, g));
                body = std::move(inner);
                return {};
            }));
        out.add(std::move(n));
    }
    return out;
}
} // namespace

Result<RoutingResult> applyLayout(const ir::Circuit& c, const Layout& layout, const CouplingGraph& g) {
    QXL_TRY(validateLayout(layout, c.qubitCount(), g));
    QXL_TRY_ASSIGN(ir::Circuit out, renamed(c, FullLayout::from(layout, g), g));
    QXL_TRY(checkCoupling(out, g));
    return finish(std::move(out), layout, layout, 0);
}

Result<RoutingResult> adoptPhysical(const ir::Circuit& physical, const CouplingGraph& g) {
    if (physical.qubitCount() > g.qubitCount())
        return fail(err::BadLayout, std::format("the program addresses ${} but the device has {} qubits", physical.qubitCount() - 1, g.qubitCount()));
    FullLayout identity;
    identity.l2p.resize(g.qubitCount());
    for (std::uint32_t q = 0; q < g.qubitCount(); ++q) identity.l2p[q] = q;
    identity.p2l = identity.l2p;
    QXL_TRY_ASSIGN(ir::Circuit out, renamed(physical, identity, g));
    QXL_TRY(checkCoupling(out, g));
    const Layout same = Layout::identity(physical.qubitCount());
    return finish(std::move(out), same, same, 0);
}

} // namespace qlab::compiler
