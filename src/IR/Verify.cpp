// Spec 14 §1.1 `Verify`, §3 — structural verification of a circuit and its nested bodies.
#include "IR/Circuit.hpp"
#include "Numerics/Checks.hpp"
#include <algorithm>
#include <format>
#include <set>

namespace qlab::ir {
namespace {

// Registers live on the top-level circuit and name the bits of every nested body.
struct Context {
    const Circuit& root;
    bool inDeclaredRegister(ClassicalBit b) const {
        const auto& regs = root.bitRegisters();
        if (regs.empty()) return true;   // a register-less circuit (built through the API) has a flat bit space
        return std::any_of(regs.begin(), regs.end(),
                           [&](const BitRegister& r) { return b.index >= r.first && b.index < r.first + r.size; });
    }
};

Status checkWires(std::span<const Wire> ws, const Circuit& c, std::string_view what) {
    std::set<std::uint32_t> seen;
    for (Wire w : ws) {
        if (w.index >= c.qubitCount())
            return fail(err::BadWire, std::format("{} uses wire {} outside the circuit ({} qubits)", what, w.index, c.qubitCount()));
        if (!seen.insert(w.index).second) return fail(err::BadWire, std::format("{} lists wire {} twice", what, w.index));
    }
    return {};
}

Status checkBit(ClassicalBit b, const Circuit& c, const Context& ctx, std::string_view what) {
    if (b.index >= c.clbitCount())
        return fail(err::BadBit, std::format("{} uses bit {} outside the circuit ({} bits)", what, b.index, c.clbitCount()));
    if (!ctx.inDeclaredRegister(b))
        return fail(err::BadBit, std::format("{} uses bit {}, which belongs to no declared register", what, b.index));
    return {};
}

Status checkMatrix(const num::Matrix& m, std::size_t width, std::string_view what) {
    const std::size_t dim = std::size_t{1} << width;
    if (m.rows != dim || m.cols != dim)
        return fail(err::BadArity, std::format("{} carries a {}x{} matrix, expected {}x{}", what, m.rows, m.cols, dim, dim));
    if (!num::isUnitary(m.view(), num::tol::kUnitaryTol))
        return fail(err::NotUnitary, std::format("{} carries a matrix that is not unitary to {}", what, num::tol::kUnitaryTol));
    return {};
}

Status checkGate(const Gate& g, const Circuit& c) {
    const std::string what = std::format("gate '{}'", g.name);
    if (g.opaque && g.custom) return fail(err::BadNode, what + " is both defcal-only and matrix-defined");
    if (g.custom) {
        QXL_TRY(checkMatrix(*g.custom, g.targets.size(), what));
    } else if (!g.opaque) {
        const GateDef* d = gates::find(g.name);
        if (!d || g.name == "unitary") return fail(err::UnknownGate, std::format("unknown gate '{}'", g.name));
        if (static_cast<int>(g.params.size()) != d->nParams)
            return fail(err::BadArity, std::format("{} takes {} parameter(s), got {}", what, d->nParams, g.params.size()));
        if (static_cast<int>(g.targets.size()) != d->nQubits)
            return fail(err::BadArity, std::format("{} acts on {} qubit(s), got {}", what, d->nQubits, g.targets.size()));
    }
    if (g.negControl.size() > g.controls.size())
        return fail(err::BadArity, what + " has more negative-control flags than controls");
    for (Wire t : g.targets)
        if (std::find(g.controls.begin(), g.controls.end(), t) != g.controls.end())
            return fail(err::BadWire, std::format("{} uses wire {} as control and target (they must be disjoint)", what, t.index));
    const auto ws = g.wires();
    QXL_TRY(checkWires(ws, c, what));
    if (g.matrixCache) QXL_TRY(checkMatrix(*g.matrixCache, g.width(), what + " (cached matrix)"));
    return {};
}

Status verifyIn(const Circuit& c, const Context& ctx);

Status checkBody(const Circuit& body, const Circuit& parent, const Context& ctx, std::string_view what) {
    if (body.qubitCount() != parent.qubitCount() || body.clbitCount() != parent.clbitCount() ||
        body.isPhysical() != parent.isPhysical()) {
        // Name whichever attribute actually differs: reporting "2q/1c but its parent is 2q/1c"
        // hides a physical-flag mismatch, which is the common cause.
        std::string detail;
        if (body.qubitCount() != parent.qubitCount() || body.clbitCount() != parent.clbitCount())
            detail = std::format("shaped {}q/{}c but its parent is {}q/{}c", body.qubitCount(),
                                 body.clbitCount(), parent.qubitCount(), parent.clbitCount());
        else
            detail = std::format("{} but its parent is {}", body.isPhysical() ? "physical" : "virtual",
                                 parent.isPhysical() ? "physical" : "virtual");
        return fail(err::BadNode, std::format("{} body is {}", what, detail));
    }
    return verifyIn(body, ctx);
}

Status checkCondition(const ClassicalExpr& e, const Circuit& c, const Context& ctx, std::string_view what) {
    for (auto b : e.reads()) QXL_TRY(checkBit(b, c, ctx, what));
    return {};
}

Status verifyIn(const Circuit& c, const Context& ctx) {
    for (NodeId id : c.topologicalOrder()) {
        const Node& n = c.node(id);
        if (const auto* g = std::get_if<Gate>(&n)) {
            QXL_TRY(checkGate(*g, c));
        } else if (const auto* m = std::get_if<Measure>(&n)) {
            QXL_TRY(checkWires(std::span<const Wire>(&m->qubit, 1), c, "measure"));
            if (!m->discards()) QXL_TRY(checkBit(m->bit, c, ctx, "measure"));
        } else if (const auto* r = std::get_if<Reset>(&n)) {
            QXL_TRY(checkWires(std::span<const Wire>(&r->qubit, 1), c, "reset"));
        } else if (const auto* b = std::get_if<Barrier>(&n)) {
            QXL_TRY(checkWires(b->wires, c, "barrier"));
        } else if (const auto* d = std::get_if<Delay>(&n)) {
            QXL_TRY(checkWires(d->wires, c, "delay"));
        } else if (const auto* co = std::get_if<ClassicalOp>(&n)) {
            QXL_TRY(checkCondition(co->expr, c, ctx, "classical assignment"));
            if (co->dst.reg.size == 0 || (co->dst.element && *co->dst.element >= co->dst.reg.size))
                return fail(err::BadBit, "classical assignment targets an empty span or an element outside it");
            for (auto bit : nodeWrites(n)) QXL_TRY(checkBit(bit, c, ctx, "classical assignment"));
        } else if (const auto* br = std::get_if<Branch>(&n)) {
            QXL_TRY(checkCondition(br->cond, c, ctx, "branch condition"));
            // An absent body is legal (an `if` with no `else`); `operator*() const` would hand
            // back a shared empty circuit whose shape never matches the parent.
            if (br->thenBody.present()) QXL_TRY(checkBody(*br->thenBody, c, ctx, "branch"));
            if (br->elseBody.present()) QXL_TRY(checkBody(*br->elseBody, c, ctx, "else"));
        } else if (const auto* l = std::get_if<Loop>(&n)) {
            QXL_TRY(checkCondition(l->cond, c, ctx, "loop condition"));
            if (l->maxIterations == 0) return fail(err::BadNode, "loop has no iteration bound");
            if (l->body.present()) QXL_TRY(checkBody(*l->body, c, ctx, "loop"));
        } else if (const auto* bx = std::get_if<Box>(&n)) {
            if (bx->body.present()) QXL_TRY(checkBody(*bx->body, c, ctx, "box"));
        }
    }
    // The per-wire chains must list only nodes that touch the wire (spec 14 §3 implied edges).
    for (std::uint32_t q = 0; q < c.qubitCount(); ++q)
        for (NodeId id : c.onWire(Wire{q})) {
            const auto ws = nodeWiresIn(c.node(id), c.qubitCount());
            if (std::find(ws.begin(), ws.end(), Wire{q}) == ws.end())
                return fail(err::DanglingWire, std::format("node {} is listed on wire {} but does not touch it", id.get(), q));
        }
    return {};
}

template <class R>
Status checkRegisters(const std::vector<R>& regs, std::uint32_t space, std::string_view kind) {
    std::vector<std::uint8_t> used(space, 0);
    for (const auto& r : regs) {
        if (r.size == 0 || r.first + r.size > space)
            return fail(err::BadNode, std::format("{} register '{}' does not fit the {} space of size {}", kind, r.name, kind, space));
        for (std::uint32_t i = r.first; i < r.first + r.size; ++i)
            if (used[i]++) return fail(err::BadNode, std::format("{} register '{}' overlaps another register", kind, r.name));
    }
    return {};
}

} // namespace

Status verify(const Circuit& c) {
    QXL_TRY(checkRegisters(c.qubitRegisters(), c.qubitCount(), "qubit"));
    QXL_TRY(checkRegisters(c.bitRegisters(), c.clbitCount(), "bit"));
    return verifyIn(c, Context{c});
}

} // namespace qlab::ir
