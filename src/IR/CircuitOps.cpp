// Spec 14 §3, §10 — inversion, structural equality, and the full unitary of a pure circuit.
#include "IR/Circuit.hpp"
#include "Numerics/Matrix.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::ir {

Result<Circuit> Circuit::inverse() const {
    if (!isPureUnitary())
        return fail(
            err::NotPure,
            "only a circuit without measurement, reset or classical control can be inverted");
    Circuit out;
    out.setQubitCount(qubits_);
    out.setClbitCount(clbits_);
    out.setPhysical(physical_);
    out.qregs_ = qregs_;
    out.cregs_ = cregs_;
    out.meta_ = meta_;
    auto topo = topologicalOrder();
    for (auto it = topo.rbegin(); it != topo.rend(); ++it) {
        const Node& n = nodes_[it->get()];
        if (const auto* g = std::get_if<Gate>(&n)) {
            Gate inv = *g;
            inv.matrixCache.reset();
            // T02 §7: named inverse or negated rotation angles where the library has one.
            std::optional<gates::GateRewrite> rw;
            if (!g->opaque && !g->custom && !g->adjoint)
                rw = gates::inverseOf(g->name, g->params);
            if (rw) {
                inv.name = rw->name;
                inv.params = rw->params;
            } else {
                inv.adjoint = !g->adjoint;
            }
            const bool library = !inv.adjoint && !inv.opaque && !inv.custom;
            inv.cls = library ? gates::classify(inv.name, inv.params) : GateClass::Generic;
            out.add(std::move(inv));
        } else if (const auto* b = std::get_if<Barrier>(&n)) {
            out.add(*b);
        } else if (const auto* d = std::get_if<Delay>(&n)) {
            out.add(*d);
        } else if (const auto* bx = std::get_if<Box>(&n)) {
            QXL_TRY_ASSIGN(Circuit body, (*bx->body).inverse());
            out.add(Box{bx->duration, SubCircuit(std::move(body)), bx->span});
        }
    }
    return out;
}

namespace {
bool closeParams(const std::vector<double>& a, const std::vector<double>& b, double tol) {
    if (a.size() != b.size())
        return false;
    for (std::size_t k = 0; k < a.size(); ++k)
        if (!(std::abs(a[k] - b[k]) <= tol))
            return false;
    return true;
}
bool closeMatrix(const std::optional<num::Matrix>& a, const std::optional<num::Matrix>& b,
                 double tol) {
    if (a.has_value() != b.has_value())
        return false;
    return !a || num::approxEqual(a->view(), b->view(), tol);
}
// An absent body and a present-but-empty one are the same circuit: `if (c) {…} else { }` and
// `if (c) {…}` execute identically, and the emitter drops an empty `else`. The const dereference
// of an absent SubCircuit yields a shared 0q/0c circuit, so compare presence first.
bool sameBody(const SubCircuit& x, const SubCircuit& y, double tol) {
    if (x.present() && y.present())
        return (*x).structurallyEqual(*y, tol);
    if (!x.present() && !y.present())
        return true;
    const Circuit& only = x.present() ? *x : *y;
    return only.nodeCount() == 0;
}

bool sameNode(const Node& x, const Node& y, double tol) {
    if (x.index() != y.index())
        return false;
    if (const auto* gx = std::get_if<Gate>(&x)) {
        const auto& gy = std::get<Gate>(y);
        return gx->name == gy.name && gx->targets == gy.targets && gx->controls == gy.controls &&
               gx->negControl == gy.negControl && gx->adjoint == gy.adjoint &&
               gx->opaque == gy.opaque && closeParams(gx->params, gy.params, tol) &&
               closeMatrix(gx->custom, gy.custom, tol);
    }
    if (const auto* m = std::get_if<Measure>(&x))
        return m->qubit == std::get<Measure>(y).qubit && m->bit == std::get<Measure>(y).bit;
    if (const auto* r = std::get_if<Reset>(&x))
        return r->qubit == std::get<Reset>(y).qubit;
    if (const auto* b = std::get_if<Barrier>(&x))
        return b->wires == std::get<Barrier>(y).wires;
    if (const auto* d = std::get_if<Delay>(&x))
        return d->duration == std::get<Delay>(y).duration && d->wires == std::get<Delay>(y).wires;
    if (const auto* c = std::get_if<ClassicalOp>(&x))
        return c->expr == std::get<ClassicalOp>(y).expr && c->dst == std::get<ClassicalOp>(y).dst;
    if (const auto* b = std::get_if<Branch>(&x)) {
        const auto& o = std::get<Branch>(y);
        return b->cond == o.cond && sameBody(b->thenBody, o.thenBody, tol) &&
               sameBody(b->elseBody, o.elseBody, tol);
    }
    if (const auto* l = std::get_if<Loop>(&x)) {
        const auto& o = std::get<Loop>(y);
        return l->cond == o.cond && l->maxIterations == o.maxIterations &&
               sameBody(l->body, o.body, tol);
    }
    const auto& bx = std::get<Box>(x);
    const auto& by = std::get<Box>(y);
    return bx.duration == by.duration && sameBody(bx.body, by.body, tol);
}
} // namespace

bool Circuit::structurallyEqual(const Circuit& o, double tol) const {
    if (qubits_ != o.qubits_ || clbits_ != o.clbits_ || physical_ != o.physical_)
        return false;
    auto a = topologicalOrder(), b = o.topologicalOrder();
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!sameNode(nodes_[a[i].get()], o.nodes_[b[i].get()], tol))
            return false;
    return true;
}

// ---------------------------------------------------------------- toUnitary
namespace {
// acc ← G·acc for a k-qubit matrix `u` on `targets` (targets[0] least significant), acting only on
// basis states whose control bits equal `pattern` (T11 §2.3 index permutation, no 2^n × 2^n
// product).
void applyLeft(num::Matrix& acc, const num::Matrix& u, std::span<const std::size_t> targets,
               std::size_t ctrlMask, std::size_t pattern) {
    const std::size_t dim = acc.rows, k = u.rows;
    std::size_t targetMask = 0;
    std::vector<std::size_t> offset(k, 0);
    for (std::size_t t = 0; t < targets.size(); ++t)
        targetMask |= std::size_t{1} << targets[t];
    for (std::size_t m = 0; m < k; ++m)
        for (std::size_t t = 0; t < targets.size(); ++t)
            if ((m >> t) & 1u)
                offset[m] |= std::size_t{1} << targets[t];
    std::vector<num::Complex> in(k);
    for (std::size_t base = 0; base < dim; ++base) {
        if ((base & targetMask) != 0 || (base & ctrlMask) != pattern)
            continue;
        for (std::size_t col = 0; col < dim; ++col) {
            for (std::size_t m = 0; m < k; ++m)
                in[m] = acc(base + offset[m], col);
            for (std::size_t r = 0; r < k; ++r) {
                num::Complex s{};
                for (std::size_t m = 0; m < k; ++m)
                    s += u(r, m) * in[m];
                acc(base + offset[r], col) = s;
            }
        }
    }
}

Status applyCircuit(num::Matrix& acc, const Circuit& c, std::uint32_t n) {
    for (NodeId id : c.topologicalOrder()) {
        const Node& node = c.node(id);
        if (const auto* g = std::get_if<Gate>(&node)) {
            std::vector<std::size_t> targets;
            std::size_t used = 0, mask = 0, pattern = 0;
            for (Wire w : g->wires()) {
                if (w.index >= n || (used >> w.index) & 1u)
                    return fail(
                        err::BadWire,
                        std::format("gate '{}' uses wire {} outside the {}-qubit space or twice",
                                    g->name, w.index, n));
                used |= std::size_t{1} << w.index;
            }
            for (Wire w : g->targets)
                targets.push_back(w.index);
            for (std::size_t j = 0; j < g->controls.size(); ++j) {
                const std::size_t bit = std::size_t{1} << g->controls[j].index;
                mask |= bit;
                if (!g->isNegControl(j))
                    pattern |= bit;
            }
            QXL_TRY_ASSIGN(num::Matrix base, baseMatrixOf(*g));
            if (base.rows != (std::size_t{1} << targets.size()) || !base.square())
                return fail(err::BadArity,
                            std::format("gate '{}' matrix does not match its {} target(s)", g->name,
                                        targets.size()));
            applyLeft(acc, base, targets, mask, pattern);
        } else if (const auto* bx = std::get_if<Box>(&node)) {
            QXL_TRY(applyCircuit(acc, *bx->body, n));
        } else if (!std::holds_alternative<Barrier>(node) && !std::holds_alternative<Delay>(node)) {
            return fail(err::NotPure,
                        std::format("toUnitary needs a circuit without measurement, reset or "
                                    "classical control; found a {} node",
                                    nodeKindName(node)));
        }
    }
    return {};
}
} // namespace

Result<num::Matrix> toUnitary(const Circuit& c, std::uint32_t nQubits) {
    const std::uint32_t n = std::max(nQubits, c.qubitCount());
    if (n > 12)
        return fail(err::TooLarge, std::format("toUnitary is limited to 12 qubits, got {}", n));
    if (!c.isPureUnitary())
        return fail(err::NotPure,
                    "toUnitary needs a circuit without measurement, reset or classical control");
    num::Matrix acc = num::Matrix::identity(std::size_t{1} << n);
    QXL_TRY(applyCircuit(acc, c, n));
    return acc;
}

} // namespace qlab::ir
