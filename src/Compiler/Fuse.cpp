// Spec 14 §5.2 (single-qubit fusion) and the gate motion of §5.3.
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/Commutation.hpp"
#include "Compiler/OptimizeImpl.hpp"
#include "Numerics/Matrix.hpp"
#include <cmath>
#include <initializer_list>

namespace qlab::compiler::detail {
namespace {

bool isNativeName(const ir::Gate& g, Basis1q basis) {
    if (g.adjoint || g.custom)
        return false;
    switch (basis) {
    case Basis1q::U:
        return g.name == "U";
    case Basis1q::ZSX:
        return g.name == "rz" || g.name == "sx" || g.name == "x" || g.name == "id";
    case Basis1q::ZYZ:
        return g.name == "rz" || g.name == "rx" || g.name == "ry";
    }
    return false;
}

// True when the run already has the irreducible shape of spec 14 §4.2, so that resynthesis cannot
// make it cheaper and no matrix needs to be formed: rz? · sx · rz? (one pulse), or
// rz? · sx · rz(m) · sx · rz? with m ∉ {0, ±π/2, π} (two pulses); for ions rz? · ry(θ) · rz? away
// from the special angles. Most runs a compiled circuit holds are of this kind (h = rz·sx·rz).
bool alreadyShortest(const WorkList& w, const std::vector<std::uint32_t>& run, Basis1q basis) {
    constexpr double pi = 3.14159265358979323846;
    if (basis == Basis1q::U || run.size() > 5)
        return false;
    auto special = [&](double a, std::initializer_list<double> values) {
        for (double v : values)
            if (std::abs(wrapAngle(a - v)) < 1e-9)
                return true;
        return false;
    };
    std::size_t pulses = 0;
    bool previousRz = false;
    for (std::size_t k = 0; k < run.size(); ++k) {
        const ir::Gate& g = *w.gateAt(run[k]);
        if (g.adjoint || g.custom)
            return false;
        if (g.name == "rz") {
            if (previousRz || special(g.params[0], {0.0}))
                return false;
            const bool middle = k > 0 && k + 1 < run.size();
            if (middle && basis == Basis1q::ZSX && special(g.params[0], {pi / 2, -pi / 2, pi}))
                return false;
            previousRz = true;
            continue;
        }
        if (!previousRz && k > 0)
            return false; // two pulses in a row merge
        previousRz = false;
        ++pulses;
        if (basis == Basis1q::ZSX && g.name != "sx")
            return false;
        if (basis == Basis1q::ZYZ &&
            (g.name != "ry" || special(g.params[0], {0.0, pi}) || run.size() > 2))
            return false;
    }
    return pulses >= 1 && pulses <= (basis == Basis1q::ZSX ? 2u : 1u);
}

// A run of consecutive single-qubit gates on one wire, as node indices in time order.
bool fuseRun(WorkList& w, const std::vector<std::uint32_t>& run, Basis1q basis) {
    if (run.size() < 2)
        return false; // spec 14 §5.2: shorter runs are left alone
    if (alreadyShortest(w, run, basis))
        return false;
    num::Matrix product = num::Matrix::identity(2);
    RunCost before;
    before.gates = run.size();
    for (std::uint32_t idx : run) {
        const ir::Gate& g = *w.gateAt(idx);
        auto m = ir::baseMatrixOf(g);
        if (!m)
            return false;
        product = num::matmul(*m, product);
        if (isNativeName(g, basis))
            before.pulses += isFrameChange(g.name) ? 0u : 1u;
        else
            before.pulses +=
                synthesize1q(m->view(), basis).pulses(); // what the gate costs once lowered
    }
    const OneQubitSequence s = synthesize1q(product.view(), basis);
    // Replace only when strictly cheaper (pulses, then gates): a run already in its shortest form
    // stays byte-identical, which makes the optimizer idempotent (spec 25 §4).
    if (!(RunCost{s.pulses(), s.gates.size()} < before))
        return false;
    const ir::Gate& first = *w.gateAt(run.front());
    std::vector<ir::Node> replacement;
    for (const auto& step : s.gates) {
        auto made = gate(step.name, first.targets, step.params, first.span);
        if (!made)
            return false;
        replacement.emplace_back(std::move(*made));
    }
    // The run is contiguous on its wire, so any slot between its first and last node is a valid
    // place for the fused gates; the first one is used.
    for (ir::Node& n : replacement)
        w.after[run.front()].push_back(std::move(n));
    for (std::uint32_t idx : run)
        w.kill(idx);
    return true;
}
} // namespace

bool sweepFuse(WorkList& w, Basis1q basis) {
    bool changed = false;
    std::vector<std::uint32_t> run;
    for (std::uint32_t wire = 0; wire < w.wireSeq.size(); ++wire) {
        if (!w.dirty[wire])
            continue; // single-qubit runs depend on their own wire only
        run.clear();
        for (std::uint32_t idx : w.wireSeq[wire]) {
            // Nodes spliced in after idx are not indexed yet, so a splice point ends the run.
            const bool spliced = !w.after[idx].empty();
            if (!spliced && !w.alive[idx])
                continue; // removed earlier in this pass: the run continues
            const ir::Gate* g = w.gateAt(idx);
            if (!spliced && g && isFusable(*g)) {
                run.push_back(idx);
                continue;
            }
            changed = fuseRun(w, run, basis) || changed;
            run.clear();
        }
        changed = fuseRun(w, run, basis) || changed;
    }
    return changed;
}

bool sweepCommute(WorkList& w) {
    bool changed = false;
    const auto n = static_cast<std::uint32_t>(w.nodes.size());
    for (std::uint32_t i = 0; i < n; ++i) {
        const ir::Gate* g = w.gateAt(i);
        if (!g || !isFusable(*g) || !w.after[i].empty() || !w.anyDirty(i))
            continue;
        const std::uint8_t action = w.actionOf(i, 0);
        if (action != kActsZ && action != kActsX)
            continue; // `id` commutes with everything: nothing to gain
        const std::uint32_t wire = w.wiresOf(i)[0];
        std::uint32_t skipped = 0;
        std::uint32_t j = w.previousOn(i, 0);
        while (j != WorkList::npos && skipped < kCommuteWindow) {
            const ir::Gate* h = w.gateAt(j);
            if (!h)
                break;
            if (isFusable(*h)) {
                if (skipped > 0) { // g joins the run that ends in h
                    w.touch(i);
                    w.after[j].push_back(std::move(w.nodes[i]));
                    w.alive[i] = 0;
                    changed = true;
                }
                break;
            }
            if (!w.commutes(j, i))
                break;
            ++skipped;
            j = w.previousOn(j, w.slotOf(j, wire));
        }
    }
    return changed;
}

} // namespace qlab::compiler::detail
