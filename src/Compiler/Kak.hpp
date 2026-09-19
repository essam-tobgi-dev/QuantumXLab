#pragma once
// Spec 14 §5.5 (MAY), T02 §6 — two-qubit Cartan (KAK) decomposition and resynthesis:
//   U = e^{iφ} (A₁ ⊗ A₀) · exp(i(a·XX + b·YY + c·ZZ)) · (B₁ ⊗ B₀)
// with single-qubit A, B (index 0 = operand 0 = least significant) and the canonical gate realised
// with the minimal number of cx: 0 (local), 1 (one coordinate ±π/4), 2 (one coordinate 0), else 3
// (Vatan–Williams, Shende–Markov–Bullock). Enabled by `CompileOptions::kak` and at level 2.
#include "Compiler/Target.hpp"
#include "IR/Circuit.hpp"
#include "Numerics/Types.hpp"
#include <stop_token>
#include <vector>

namespace qlab::compiler {

struct KakDecomposition {
    double a = 0, b = 0, c = 0;      // canonical coordinates, each in (−π/4, π/4]
    num::Matrix before0, before1;    // B₀, B₁ (applied first), 2×2 with unit determinant
    num::Matrix after0, after1;      // A₀, A₁ (applied last)
    double phase = 0;                // φ
    num::Matrix reconstruct() const; // the 4×4 product above, for tests
    std::uint32_t cxCount(double eps = 1e-9) const;
};

// exp(i(a·XX + b·YY + c·ZZ)) as a 4×4 matrix.
num::Matrix canonicalGate(double a, double b, double c);

// Decomposes a 4×4 unitary given in the IR convention (operand 0 least significant).
Result<KakDecomposition> kakDecompose(num::ConstMatrixView u);

// A circuit over {U, cx} on wires (q0, q1), in TIME order, equal to `u` up to a global phase and
// using `cxCount()` cx gates. Every gate carries `span`.
Result<std::vector<ir::Gate>> synthesizeTwoQubit(num::ConstMatrixView u, ir::Wire q0, ir::Wire q1,
                                                 const SourceSpan& span = {});

struct KakStats {
    std::uint32_t blocks = 0;   // maximal runs of gates on one qubit pair with ≥ 2 two-qubit gates
    std::uint32_t replaced = 0; // blocks whose resynthesis uses fewer two-qubit gates
    std::uint32_t twoQubitBefore = 0, twoQubitAfter = 0;
};
// §5.5: every run of two-qubit gates on the same pair (with the single-qubit gates between them) is
// resynthesised and lowered to `target`; a block is replaced only when that saves two-qubit gates.
// Bodies of control nodes are processed too; blocks never extend across a non-gate node.
Result<KakStats> resynthesizeTwoQubitBlocks(ir::Circuit& c, const Target& target,
                                            std::stop_token stop = {});

} // namespace qlab::compiler
