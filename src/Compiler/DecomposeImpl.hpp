#pragma once
// Spec 14 §4 — the gate-lowering engine behind `decompose`. Internal to the Compiler module.
#include "Compiler/Decompose.hpp"
#include "Numerics/Types.hpp"
#include <map>
#include <span>
#include <string>

namespace qlab::compiler::detail {

class Decomposer {
  public:
    Decomposer(const Target& target, bool physical) : target_(target), physical_(physical) {}

    // Appends the native form of `g` to `out` (TIME order).
    Status lower(const ir::Gate& g, std::vector<ir::Gate>& out, int depth = 0);

  private:
    using Out = std::vector<ir::Gate>;

    // ---- Decompose.cpp
    Status lower1q(const ir::Gate& g, Out& out);
    Status lowerEntangler(const ir::Gate& g, Out& out); // rxx/ms on ions: wrap and split θ
    Status lowerReversed(const ir::Gate& g, Out& out, int depth); // directed coupler (T02 §4)
    Status lowerByRule(const ir::Gate& g, Out& out, int depth);
    Status lowerAll(const std::vector<ir::Gate>& seq, Out& out, int depth);
    Status emitNamed(std::string_view name, std::vector<ir::Wire> wires, std::vector<double> params,
                     const SourceSpan& span, Out& out, int depth);
    bool wrongDirection(const ir::Gate& g) const;
    Error notDecomposable(const ir::Gate& g) const; // QL4070

    // ---- MultiControl.cpp
    Status lowerControlled(const ir::Gate& g, Out& out, int depth);
    // ctrl(n) @ U for a 2×2 unitary U: ABC for n = 1 (T02 (3.1)), Gray code above (T02 §3.4).
    Status controlledU(const num::Matrix& u, std::span<const ir::Wire> controls, ir::Wire target,
                       const SourceSpan& span, Out& out, int depth);
    // ctrl(n) @ gphase(γ) = ctrl(n−1) @ p(γ) on the last control (spec 13 §3).
    Status controlledPhase(double gamma, std::span<const ir::Wire> controls, const SourceSpan& span,
                           Out& out, int depth);

    const Target& target_;
    bool physical_;
    std::map<std::string, OneQubitSequence, std::less<>>
        named1q_; // synthesis of parameterless gates (h, s, t, …)
};

inline constexpr int kMaxLoweringDepth = 64;

} // namespace qlab::compiler::detail
