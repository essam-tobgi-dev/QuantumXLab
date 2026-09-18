// Spec 14 §4 — lowering driver: native acceptance, single-qubit synthesis, rule application,
// entangler angle range on ions, and reversal on directed couplers.
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/DecomposeImpl.hpp"
#include "Compiler/Kak.hpp"
#include "Lang/Diagnostics.hpp"
#include <cmath>
#include <format>

namespace qlab::compiler {
namespace detail {

Error Decomposer::notDecomposable(const ir::Gate& g) const {
    return lang::Diagnostics::make("QL4070", g.span, g.name, target_.name).error;
}

bool Decomposer::wrongDirection(const ir::Gate& g) const {
    return physical_ && g.controls.empty() && !g.custom && !target_.directionOk(g);
}

Status Decomposer::lower(const ir::Gate& in, Out& out, int depth) {
    if (depth > kMaxLoweringDepth)
        return fail(ErrorCode::Internal, std::format("decomposition of '{}' does not terminate", in.name));
    if (in.opaque || (target_.accepts(in) && !wrongDirection(in))) {
        out.push_back(in);
        return {};
    }
    ir::Gate g = in;
    g.matrixCache.reset();
    if (g.adjoint && !g.custom)   // a named inverse keeps the gate inside the rule table
        if (auto rw = ir::gates::inverseOf(g.name, g.params)) {
            g.name = rw->name;
            g.params = rw->params;
            g.adjoint = false;
            g.cls = ir::gates::classify(g.name, g.params);
            if (target_.accepts(g) && !wrongDirection(g)) {
                out.push_back(std::move(g));
                return {};
            }
        }
    if (!g.controls.empty()) return lowerControlled(g, out, depth);
    if (g.name == "gphase") return {};   // a global phase is unobservable outside `ctrl @` (spec 14 §4.2)
    if (g.targets.size() == 1) return lower1q(g, out);
    if (wrongDirection(g)) return lowerReversed(g, out, depth);
    if (target_.family == "rxx" && (g.name == "rxx" || g.name == "ms") && !g.adjoint && !g.custom)
        return lowerEntangler(g, out);
    if (g.custom) {   // an explicit matrix: two qubits go through the Cartan decomposition (§5.5, T02 §6)
        if (g.targets.size() != 2) return fail(notDecomposable(g));
        auto m = ir::baseMatrixOf(g);
        if (!m) return fail(notDecomposable(g));
        auto seq = synthesizeTwoQubit(m->view(), g.targets[0], g.targets[1], g.span);
        if (!seq) return fail(notDecomposable(g));
        return lowerAll(*seq, out, depth + 1);
    }
    return lowerByRule(g, out, depth);
}

Status Decomposer::lowerAll(const std::vector<ir::Gate>& seq, Out& out, int depth) {
    for (const ir::Gate& s : seq) QXL_TRY(lower(s, out, depth));
    return {};
}

Status Decomposer::emitNamed(std::string_view name, std::vector<ir::Wire> wires, std::vector<double> params,
                             const SourceSpan& span, Out& out, int depth) {
    QXL_TRY_ASSIGN(ir::Gate made, gate(name, std::move(wires), std::move(params), span));
    return lower(made, out, depth);
}

Status Decomposer::lower1q(const ir::Gate& g, Out& out) {
    const bool named = g.params.empty() && !g.custom && !g.adjoint;
    auto cached = named ? named1q_.find(g.name) : named1q_.end();
    if (cached == named1q_.end()) {
        auto m = ir::baseMatrixOf(g);
        if (!m) return fail(notDecomposable(g));
        OneQubitSequence fresh = synthesize1q(m->view(), target_.basis);
        if (!named) {
            for (const auto& s : fresh.gates) {
                QXL_TRY_ASSIGN(ir::Gate made, gate(s.name, {g.targets[0]}, s.params, g.span));
                out.push_back(std::move(made));
            }
            return {};
        }
        cached = named1q_.emplace(g.name, std::move(fresh)).first;
    }
    const OneQubitSequence& seq = cached->second;
    for (const auto& s : seq.gates) {
        QXL_TRY_ASSIGN(ir::Gate made, gate(s.name, {g.targets[0]}, s.params, g.span));
        out.push_back(std::move(made));
    }
    return {};
}

// T06 §6, spec 10 §6.6: one MS pulse realises |θ| ≤ π/2; rxx(θ + 2π) = −rxx(θ).
Status Decomposer::lowerEntangler(const ir::Gate& g, Out& out) {
    if (g.params.size() != 1) return fail(notDecomposable(g));
    const double theta = wrapAngle(g.params[0]);
    if (std::abs(theta) < kAngleEps) return {};
    const bool split = target_.maxEntanglerAngle > 0.0 && std::abs(theta) > target_.maxEntanglerAngle + kAngleEps;
    const int pieces = split ? 2 : 1;
    for (int k = 0; k < pieces; ++k) {
        QXL_TRY_ASSIGN(ir::Gate made, gate(target_.entangler, g.targets, {theta / pieces}, g.span));
        out.push_back(std::move(made));
    }
    return {};
}

// T02 §4 "CNOT reversal": CX_{t,c} = (H⊗H) CX_{c,t} (H⊗H). An ecr goes through its cx form.
Status Decomposer::lowerReversed(const ir::Gate& g, Out& out, int depth) {
    const ir::Wire a = g.targets[0], b = g.targets[1];
    if (g.name == "cx") {
        QXL_TRY(emitNamed("h", {a}, {}, g.span, out, depth + 1));
        QXL_TRY(emitNamed("h", {b}, {}, g.span, out, depth + 1));
        QXL_TRY(emitNamed("cx", {b, a}, {}, g.span, out, depth + 1));
        QXL_TRY(emitNamed("h", {a}, {}, g.span, out, depth + 1));
        return emitNamed("h", {b}, {}, g.span, out, depth + 1);
    }
    const Rule* rule = findGenericRule(g.name);
    if (!rule) return fail(notDecomposable(g));
    QXL_TRY_ASSIGN(RuleExpansion e, applyRule(*rule, g));
    return lowerAll(e.gates, out, depth + 1);
}

Status Decomposer::lowerByRule(const ir::Gate& g, Out& out, int depth) {
    ir::Gate base = g;
    base.adjoint = false;
    // A swap is symmetric: on a directed coupler start with the native direction, so that only the
    // middle cx of the three needs the Hadamard reversal (4 h instead of 8).
    if (physical_ && target_.device && base.name == "swap" && base.targets.size() == 2 &&
        !target_.device->nativeDirection(base.targets[0].index, base.targets[1].index) &&
        target_.device->nativeDirection(base.targets[1].index, base.targets[0].index))
        std::swap(base.targets[0], base.targets[1]);
    const Rule* rule = findRule(base.name, target_.family);
    if (!rule) return fail(notDecomposable(g));
    QXL_TRY_ASSIGN(RuleExpansion e, applyRule(*rule, base));
    if (g.adjoint) e.gates = invertSequence(std::move(e.gates));
    return lowerAll(e.gates, out, depth + 1);
}

} // namespace detail

std::vector<ir::Gate> invertSequence(std::vector<ir::Gate> seq) {
    std::vector<ir::Gate> out;
    out.reserve(seq.size());
    for (auto it = seq.rbegin(); it != seq.rend(); ++it) {
        ir::Gate g = std::move(*it);
        g.matrixCache.reset();
        std::optional<ir::gates::GateRewrite> rw;
        if (!g.opaque && !g.custom && !g.adjoint) rw = ir::gates::inverseOf(g.name, g.params);
        if (rw) {
            g.name = rw->name;
            g.params = rw->params;
            g.cls = ir::gates::classify(g.name, g.params);
        } else {
            g.adjoint = !g.adjoint;
            g.cls = ir::GateClass::Generic;
        }
        out.push_back(std::move(g));
    }
    return out;
}

namespace {
Status expandInto(const ir::Gate& g, RuleExpansion& out, int depth) {
    if (depth > detail::kMaxLoweringDepth) return fail(ErrorCode::Internal, "rule expansion does not terminate");
    if (g.targets.size() <= 1 || (g.name == "cx" && !g.adjoint && !g.custom)) {
        out.gates.push_back(g);
        return {};
    }
    const Rule* rule = g.custom || g.opaque ? nullptr : findGenericRule(g.name);
    if (!rule) return fail(err::NotDecomposable, std::format("gate '{}' has no expansion into single-qubit gates and cx", g.name));
    ir::Gate base = g;
    base.adjoint = false;
    QXL_TRY_ASSIGN(RuleExpansion step, applyRule(*rule, base));
    RuleExpansion sub;
    sub.phase = step.phase;
    for (const ir::Gate& s : step.gates) QXL_TRY(expandInto(s, sub, depth + 1));
    if (g.adjoint) {
        sub.gates = invertSequence(std::move(sub.gates));
        sub.phase = -sub.phase;
    }
    out.phase += sub.phase;
    for (auto& s : sub.gates) out.gates.push_back(std::move(s));
    return {};
}
} // namespace

Result<RuleExpansion> expandToCx(const ir::Gate& g) {
    if (!g.controls.empty()) return fail(ErrorCode::InvalidArgument, "expandToCx takes an uncontrolled gate");
    RuleExpansion out;
    QXL_TRY(expandInto(g, out, 0));
    return out;
}

Status decomposeGate(const ir::Gate& g, const Target& target, bool physical, std::vector<ir::Gate>& out) {
    detail::Decomposer d(target, physical);
    return d.lower(g, out);
}

Status decompose(ir::Circuit& c, const Target& target, std::stop_token stop) {
    detail::Decomposer d(target, c.isPhysical());
    // `c` is only read until the new node list is complete, so a failure leaves it untouched.
    const ir::Circuit& src = c;
    std::vector<ir::Node> result;
    result.reserve(src.nodeCount());
    std::vector<ir::Gate> lowered;
    std::size_t tick = 0;
    for (ir::NodeId id : src.topologicalOrder()) {
        if ((++tick & 0xFF) == 0 && stop.stop_requested()) return fail(ErrorCode::Cancelled, "compile cancelled");
        const ir::Node& n = src.node(id);
        if (const auto* g = std::get_if<ir::Gate>(&n)) {
            lowered.clear();
            QXL_TRY(d.lower(*g, lowered));
            for (ir::Gate& l : lowered) result.emplace_back(std::move(l));
            continue;
        }
        ir::Node copy = n;
        QXL_TRY(forEachBody(copy, [&](ir::Circuit& body) { return decompose(body, target, stop); }));
        result.push_back(std::move(copy));
    }
    setNodes(c, std::move(result));
    return {};
}

} // namespace qlab::compiler
