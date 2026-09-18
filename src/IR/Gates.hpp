#pragma once
// Spec 14 §3 — gate library: definitions, matrices, inverse and power rules.
// Matrices follow T02: for a gate written `g q[a], q[b]`, operand 0 is the LEAST significant
// index, i.e. the matrix is in basis |q_b q_a⟩. `num::embed(matrix, operands, n)` uses the same
// rule, so embedding is always `embed(matrixOf(g), wireIndices(g), n)`.
#include "IR/Types.hpp"
#include "Numerics/Types.hpp"
#include "QSim/Types.hpp"
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::ir {

using qsim::GateClass;

struct GateDef {
    std::string_view name;
    int nParams = 0;
    int nQubits = 1;
    GateClass cls = GateClass::Generic;
    bool selfInverse = false;   // U² = I exactly (so it survives `ctrl @`)
    bool diagonal = false;      // matrix is diagonal in the computational basis
    bool rotation = false;      // every parameter is a generator angle: U(p)^k = U(k·p), so
                                // pow(k) scales the parameters and the inverse negates them
    std::string_view inverseName; // named inverse (s→sdg); empty when none
    std::string_view description;
};

namespace gates {

// The whole library: stdgates of spec 13 §4 plus the native gates of spec 09/14 §4.1.
const std::vector<GateDef>& all();
const GateDef* find(std::string_view name);
bool isKnown(std::string_view name);

// Matrix of a named gate with bound parameters, in the convention above.
Result<num::Matrix> matrix(std::string_view name, std::span<const double> params);

// The OpenQASM 3 built-in U(θ,φ,λ) of T02 (1.1), the canonical single-qubit form (no global phase
// factor; the legacy `u3` gate differs from it by e^{-i(φ+λ)/2}).
num::Matrix u3(double theta, double phi, double lambda);

// Named inverse if one exists: returns {name, params}. Rotations negate their angles.
struct GateRewrite { std::string name; std::vector<double> params; };
std::optional<GateRewrite> inverseOf(std::string_view name, std::span<const double> params);
// pow(k) rewrite for rotation-like gates (angle × k); nullopt when the matrix path is needed.
std::optional<GateRewrite> powerOf(std::string_view name, std::span<const double> params, double k);

// Wrap `U` (acting on the low `nTargets` wires) with `nCtrl` controls on the HIGH wires.
// `negative[j] != 0` makes control j active on |0⟩. Result is 2^(nTargets+nCtrl) square.
num::Matrix controlled(num::ConstMatrixView u, std::size_t nCtrl, std::span<const std::uint8_t> negative);

// U^k for a small unitary (spec 14 §4.3). Exact products for integer |k| ≤ 16; otherwise the
// principal branch: every eigenphase of U is taken in (−π, π] and multiplied by k, so X^½ = SX.
Result<num::Matrix> unitaryPower(num::ConstMatrixView u, double k);

// Classification used by the state-vector fast paths (spec 07 §2.2). Computed from the
// definition and parameters, never by inspecting matrix entries at run time. It describes the
// BASE operation on the gate's targets; controls are carried separately, exactly as in
// `qsim::GateOp` (`toGateOp` in Node.hpp), so `ctrl @ x` is PauliX with one control.
GateClass classify(std::string_view name, std::span<const double> params);

// Exact closed forms of integer powers that stay inside the library (T02 §1.2: S² = Z, T² = S,
// SX² = X): the phase family {z, s, sdg, t, tdg, p} and the √X cycle {sx, x, sxdg}. `k` may be
// any real for the phase family. Returns nullopt when no closed form applies; an empty name means
// the power is the identity.
std::optional<GateRewrite> namedPower(std::string_view name, std::span<const double> params, double k);

// Controlled library gates as a base gate plus leading control operands (T02 §3): cx = ctrl @ x,
// ccx = ctrl(2) @ x, cp(λ) = ctrl @ p(λ), cswap = ctrl @ swap, cu(θ,φ,λ,0) = ctrl @ U(θ,φ,λ).
// Build uses the pair so that `ctrl @ x a, b` and `cx a, b` produce the same node (spec 14 §2).
struct ControlledRewrite { GateRewrite base; std::size_t controls = 0; };
std::optional<ControlledRewrite> splitControlled(std::string_view name, std::span<const double> params);
// The library gate equal to `ctrl(controls) @ base(params)` with all controls positive, if any.
std::optional<GateRewrite> joinControlled(std::string_view base, std::span<const double> params,
                                          std::size_t controls);

} // namespace gates
} // namespace qlab::ir
