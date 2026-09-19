// Spec 14 §4.3; T02 §3 — controlled gates. One control: the ABC construction (the `cu` rule), with
// the cheaper forms for diagonal U and for X. n ≥ 2 controls: Gray code (Barenco et al. 1995,
// Lemma 7.1) with V = U^{1/2^{n−1}}: 2^n − 1 controlled-V/V† and 2^n − 2 cx between the controls,
// no ancilla. Negative controls are conjugated by x. A controlled multi-qubit gate is expanded
// exactly into single-qubit gates and cx first, and every piece (and the phase) is controlled.
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/DecomposeImpl.hpp"
#include "Lang/Diagnostics.hpp"
#include "Numerics/Matrix.hpp"
#include <bit>
#include <cmath>

namespace qlab::compiler::detail {
namespace {
constexpr double kEps = 1e-12;

num::Matrix pauliX() {
    num::Matrix x(2, 2);
    x(0, 1) = 1.0;
    x(1, 0) = 1.0;
    return x;
}
num::Matrix phaseGate(double lambda) {
    num::Matrix p(2, 2);
    p(0, 0) = 1.0;
    p(1, 1) = std::polar(1.0, lambda);
    return p;
}
bool isDiagonal(const num::Matrix& u) {
    return std::abs(u(0, 1)) < kEps && std::abs(u(1, 0)) < kEps;
}
// γ with U = e^{iγ} X, if U has that form.
std::optional<double> phaseIfX(const num::Matrix& u) {
    if (std::abs(u(0, 0)) > kEps || std::abs(u(1, 1)) > kEps || std::abs(u(0, 1) - u(1, 0)) > kEps)
        return std::nullopt;
    return std::arg(u(0, 1));
}
bool negligible(double angle) {
    return std::abs(wrapAngle(angle)) < kEps;
}
} // namespace

Status Decomposer::lowerControlled(const ir::Gate& g, Out& out, int depth) {
    const std::size_t n = g.controls.size();
    if (n > kMaxControls)
        return fail(lang::Diagnostics::make("QL4050", g.span, g.name, n).error);
    if (g.opaque)
        return fail(notDecomposable(g));

    std::vector<ir::Wire> negative; // spec 14 §4.3: negctrl = ctrl conjugated by x
    for (std::size_t j = 0; j < n; ++j)
        if (g.isNegControl(j))
            negative.push_back(g.controls[j]);
    for (ir::Wire w : negative)
        QXL_TRY(emitNamed("x", {w}, {}, g.span, out, depth + 1));

    ir::Gate base = g;
    base.controls.clear();
    base.negControl.clear();
    base.matrixCache.reset();
    const std::span<const ir::Wire> controls(g.controls);

    std::optional<ir::gates::GateRewrite> joined;
    if (!base.adjoint && !base.custom)
        joined = ir::gates::joinControlled(base.name, base.params, n);
    if (joined) { // ctrl @ x = cx, ctrl(2) @ x = ccx, ctrl @ swap = cswap, … (T02 §3)
        std::vector<ir::Wire> wires(g.controls);
        wires.insert(wires.end(), base.targets.begin(), base.targets.end());
        QXL_TRY(emitNamed(joined->name, std::move(wires), joined->params, g.span, out, depth + 1));
    } else if (base.targets.empty()) {
        if (base.name != "gphase" || base.params.size() != 1)
            return fail(notDecomposable(g));
        QXL_TRY(controlledPhase(base.adjoint ? -base.params[0] : base.params[0], controls, g.span,
                                out, depth + 1));
    } else if (base.targets.size() == 1) {
        auto u = ir::baseMatrixOf(base);
        if (!u)
            return fail(notDecomposable(g));
        QXL_TRY(controlledU(*u, controls, base.targets[0], g.span, out, depth + 1));
    } else {
        auto expansion = expandToCx(base);
        if (!expansion)
            return fail(notDecomposable(g));
        for (const ir::Gate& e : expansion->gates) {
            if (e.targets.size() == 2) { // cx a, b under n controls = x on b under n + 1 controls
                std::vector<ir::Wire> more(g.controls);
                more.push_back(e.targets[0]);
                QXL_TRY(controlledU(pauliX(), more, e.targets[1], g.span, out, depth + 1));
            } else {
                auto u = ir::baseMatrixOf(e);
                if (!u)
                    return fail(notDecomposable(g));
                QXL_TRY(controlledU(*u, controls, e.targets[0], g.span, out, depth + 1));
            }
        }
        QXL_TRY(controlledPhase(expansion->phase, controls, g.span, out, depth + 1));
    }
    for (ir::Wire w : negative)
        QXL_TRY(emitNamed("x", {w}, {}, g.span, out, depth + 1));
    return {};
}

Status Decomposer::controlledPhase(double gamma, std::span<const ir::Wire> controls,
                                   const SourceSpan& span, Out& out, int depth) {
    if (controls.empty() || negligible(gamma))
        return {};
    if (controls.size() == 1)
        return emitNamed("p", {controls[0]}, {wrapAngle(gamma)}, span, out, depth);
    return controlledU(phaseGate(gamma), controls.first(controls.size() - 1), controls.back(), span,
                       out, depth);
}

Status Decomposer::controlledU(const num::Matrix& u, std::span<const ir::Wire> controls,
                               ir::Wire target, const SourceSpan& span, Out& out, int depth) {
    if (depth > kMaxLoweringDepth)
        return fail(ErrorCode::Internal, "controlled-gate lowering does not terminate");
    const std::size_t n = controls.size();
    if (n == 0) {
        const OneQubitSequence seq = synthesize1q(u.view(), target_.basis);
        for (const auto& s : seq.gates) {
            QXL_TRY_ASSIGN(ir::Gate made, gate(s.name, {target}, s.params, span));
            out.push_back(std::move(made));
        }
        return {};
    }
    const std::optional<double> xPhase = phaseIfX(u);
    if (n == 1) {
        const ir::Wire c = controls[0];
        if (isDiagonal(u)) { // ctrl @ diag(e^{iα}, e^{iβ}) = p(α)_c · cp(β − α)
            const double alpha = std::arg(u(0, 0)), delta = std::arg(u(1, 1)) - alpha;
            if (!negligible(delta))
                QXL_TRY(emitNamed("cp", {c, target}, {wrapAngle(delta)}, span, out, depth + 1));
            return controlledPhase(alpha, controls, span, out, depth + 1);
        }
        if (xPhase) {
            QXL_TRY(emitNamed("cx", {c, target}, {}, span, out, depth + 1));
            return controlledPhase(*xPhase, controls, span, out, depth + 1);
        }
        const EulerAngles e = eulerAngles(u.view()); // T02 (3.1)–(3.2)
        return emitNamed("cu", {c, target}, {e.theta, e.phi, e.lambda, e.phase}, span, out,
                         depth + 1);
    }
    if (xPhase && n == 2) { // Toffoli with 6 cx (T02 §3.2) plus the phase on the controls
        QXL_TRY(emitNamed("ccx", {controls[0], controls[1], target}, {}, span, out, depth + 1));
        return controlledPhase(*xPhase, controls, span, out, depth + 1);
    }
    // Gray code over the control patterns. The controlled-V sits on the most significant set bit
    // `lm`, whose wire carries the parity of the pattern's set bits; V on odd parity, V† on even.
    auto root =
        ir::gates::unitaryPower(u.view(), 1.0 / static_cast<double>(std::size_t{1} << (n - 1)));
    if (!root)
        return fail(root.error());
    const num::Matrix v = std::move(*root), vDagger = num::adjoint(v.view());
    std::uint32_t last = 0;
    for (std::uint32_t k = 1; k < (1u << n); ++k) {
        const std::uint32_t pattern = k ^ (k >> 1);
        if (last == 0)
            last = pattern;
        const auto lm = static_cast<std::uint32_t>(std::bit_width(pattern) - 1);
        if (const std::uint32_t diff = pattern ^ last; diff != 0) {
            const auto pos = static_cast<std::uint32_t>(std::countr_zero(diff));
            if (pos != lm) {
                QXL_TRY(emitNamed("cx", {controls[pos], controls[lm]}, {}, span, out, depth + 1));
            } else {
                for (std::uint32_t idx = 0; idx < lm; ++idx)
                    if ((pattern >> idx) & 1u)
                        QXL_TRY(emitNamed("cx", {controls[idx], controls[lm]}, {}, span, out,
                                          depth + 1));
            }
        }
        const bool odd = (std::popcount(pattern) & 1) != 0;
        QXL_TRY(
            controlledU(odd ? v : vDagger, controls.subspan(lm, 1), target, span, out, depth + 1));
        last = pattern;
    }
    return {};
}

} // namespace qlab::compiler::detail
