// Spec 14 §5.1 (inverse cancellation), §5.3 (commutation), §5.6 (diagonal merging). For a gate g
// the sweep walks back along g's first wire over gates that commute with g. On meeting a gate h on
// the same operands it checks that everything between h and g on g's other wires commutes with g as
// well, so that h · M · g = (h·g) · M, and then either removes the pair (h·g = I) or merges the
// two into one gate at h's place.
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/Commutation.hpp"
#include "Compiler/OptimizeImpl.hpp"
#include "Numerics/Matrix.hpp"
#include <cmath>
#include <numbers>

namespace qlab::compiler::detail {
namespace {
constexpr double kPi = std::numbers::pi;

// Rotations whose angles add on the same operands (T02 §4 "rotation merge"); the value is the
// period after which the gate is the identity (up to a global phase for the uncontrolled 2π ones).
double additivePeriod(std::string_view n) {
    if (n == "rx" || n == "ry" || n == "rz" || n == "p" || n == "phase" || n == "u1" || n == "cp" ||
        n == "rxx" || n == "ryy" || n == "rzz" || n == "ms")
        return 2 * kPi;
    if (n == "crx" || n == "cry" || n == "crz")
        return 4 * kPi;
    return 0.0;
}
double reduce(double a, double period) {
    return a - period * std::ceil((a - period / 2) / period);
}

// Every live node strictly between j and i on the wires of g other than its first commutes with g.
bool clearBetween(const WorkList& w, std::uint32_t j, std::uint32_t i) {
    const auto wires = w.wiresOf(i);
    for (std::uint32_t slot = 1; slot < wires.size(); ++slot) {
        std::uint32_t k = w.previousOn(i, slot);
        for (std::uint32_t steps = 0; k != j; ++steps) {
            if (k == WorkList::npos || steps > kCommuteWindow || !w.gateAt(k) || !w.commutes(k, i))
                return false;
            k = w.previousOn(k, w.slotOf(k, wires[slot]));
        }
    }
    return true;
}

ir::Gate diagonalGate(Basis1q basis, double angle, const ir::Gate& like) {
    ir::Gate d = like;
    d.adjoint = false;
    d.custom.reset();
    d.matrixCache.reset();
    d.name = basis == Basis1q::U ? "U" : "rz";
    d.params =
        basis == Basis1q::U ? std::vector<double>{0.0, 0.0, angle} : std::vector<double>{angle};
    d.cls = ir::gates::classify(d.name, d.params);
    return d;
}

std::size_t pulsesOf(const ir::Gate& g, Basis1q basis) {
    const bool native = !g.adjoint && !g.custom &&
                        (basis == Basis1q::U ? g.name == "U"
                         : basis == Basis1q::ZSX
                             ? (g.name == "rz" || g.name == "sx" || g.name == "x" || g.name == "id")
                             : (g.name == "rz" || g.name == "rx" || g.name == "ry"));
    if (native)
        return isFrameChange(g.name) ? 0u : 1u;
    auto m = ir::baseMatrixOf(g);
    return m ? synthesize1q(m->view(), basis).pulses() : 1u;
}

// h (node j) and g (node i) sit on the same operands with only commuting gates between them.
bool combine(WorkList& w, std::uint32_t j, std::uint32_t i, const MergeRules& rules) {
    ir::Gate& h = *w.gateAt(j);
    const ir::Gate& g = *w.gateAt(i);
    const auto removeBoth = [&] {
        w.kill(j);
        w.kill(i);
        return true;
    };
    if (isNamedInverseOf(h, g))
        return removeBoth();
    const bool single = isFusable(h) && isFusable(g);
    // Matrices decide only for multi-qubit pairs: adjacent single-qubit gates are the fusion
    // sweep's business, and the same-axis ones are merged below.
    if (!single)
        return isInverseOf(h, g) ? removeBoth() : false;

    const bool bothDiagonal = (w.actionOf(j, 0) & w.actionOf(i, 0) & kActsZ) != 0;
    const auto dh = bothDiagonal ? diagonalAngle(h) : std::nullopt,
               dg = bothDiagonal ? diagonalAngle(g) : std::nullopt;
    if (dh && dg) { // §5.6: two frame changes are one
        const double total = wrapAngle(*dh + *dg);
        w.kill(i);
        if (std::abs(total) < kAngleEps)
            w.kill(j);
        else {
            h = diagonalGate(rules.basis, total, h);
            w.touch(j);
        }
        return true;
    }
    // Two X-type gates (x, sx, rx): one resynthesised gate when that saves pulses.
    if (rules.resynthesize && (w.actionOf(j, 0) & w.actionOf(i, 0) & kActsX) != 0) {
        auto mh = ir::baseMatrixOf(h), mg = ir::baseMatrixOf(g);
        if (!mh || !mg)
            return false;
        const OneQubitSequence s = synthesize1q(num::matmul(*mg, *mh).view(), rules.basis);
        const RunCost before{pulsesOf(h, rules.basis) + pulsesOf(g, rules.basis), 2};
        if (!(RunCost{s.pulses(), s.gates.size()} < before))
            return false;
        for (const auto& step : s.gates) {
            auto made = gate(step.name, h.targets, step.params, h.span);
            if (!made)
                return false;
            w.after[j].emplace_back(std::move(*made));
        }
        return removeBoth();
    }
    return false;
}

// Same-name rotations on the same operands: the angles add (rz·rz, cp·cp, rzz·rzz, ms·ms, …).
bool addAngles(WorkList& w, std::uint32_t j, std::uint32_t i, const MergeRules& rules) {
    ir::Gate& h = *w.gateAt(j);
    const ir::Gate& g = *w.gateAt(i);
    if (h.params.size() != 1 || h.name != g.name || h.adjoint || g.adjoint || h.custom ||
        g.custom || !h.controls.empty())
        return false;
    const double period = additivePeriod(h.name);
    if (period == 0.0)
        return false;
    const double total = reduce(h.params[0] + g.params[0], period);
    // One native entangler pulse covers |θ| ≤ maxEntanglerAngle (T06 §6): larger sums stay two
    // gates.
    if (rules.maxEntanglerAngle > 0.0 && h.targets.size() == 2 &&
        std::abs(total) > rules.maxEntanglerAngle + kAngleEps)
        return false;
    w.kill(i);
    if (std::abs(total) < kAngleEps) {
        w.kill(j);
        return true;
    }
    h.params[0] = total;
    h.matrixCache.reset();
    h.cls = ir::gates::classify(h.name, h.params);
    w.touch(j);
    return true;
}
} // namespace

bool sweepCancelMerge(WorkList& w, const MergeRules& rules) {
    bool changed = false;
    const auto n = static_cast<std::uint32_t>(w.nodes.size());
    for (std::uint32_t i = 0; i < n; ++i) {
        const ir::Gate* g = w.gateAt(i);
        if (!g || g->opaque || w.wiresOf(i).empty() || !w.anyDirty(i))
            continue;
        const std::uint32_t wire = w.wiresOf(i)[0];
        std::uint32_t j = w.previousOn(i, 0);
        for (std::uint32_t steps = 0; j != WorkList::npos && steps < kCommuteWindow; ++steps) {
            const ir::Gate* h = w.gateAt(j);
            if (!h)
                break; // measure, reset, barrier, delay or a control node: nothing passes
            if (sameOperands(*h, *g) && clearBetween(w, j, i) &&
                (addAngles(w, j, i, rules) || combine(w, j, i, rules))) {
                changed = true;
                break;
            }
            if (!rules.lookThrough || !w.commutes(j, i))
                break;
            j = w.previousOn(j, w.slotOf(j, wire));
        }
    }
    return changed;
}

} // namespace qlab::compiler::detail
