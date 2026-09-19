#pragma once
// Spec 14 §4.2, §5.2; T02 §1.4 — Euler angles of a 2×2 unitary and its synthesis in a native
// single-qubit basis. Every identity here is exact, global phase included.
#include "IR/Gates.hpp"
#include "Numerics/Types.hpp"
#include <vector>

namespace qlab::compiler {

// M = e^{i·phase} · U(θ, φ, λ) with U of T02 (1.1) and θ ∈ [0, π].
struct EulerAngles {
    double theta = 0, phi = 0, lambda = 0, phase = 0;
};
// Spec 14 §5.2: θ = 2·atan2(|m10|, |m00|), φ = arg m10 − arg m00, λ = arg(−m01) − arg m00. When
// |m00| = 1 only φ + λ is determined (φ is set to 0); when |m00| = 0 only φ − λ is (λ is set to 0).
EulerAngles eulerAngles(num::ConstMatrixView m);

// Single-qubit target bases of spec 14 §4.1.
enum class Basis1q : std::uint8_t {
    U,   // no device: one U(θ,φ,λ)
    ZSX, // transmons: rz · sx · rz · sx · rz, `x` when the gate is a π flip
    ZYZ, // ions: rz · ry · rz, `rx` when the gate is an X rotation
};

// Gates in TIME order (first element acts first) with M = e^{i·phase} · g_k ⋯ g_1.
struct OneQubitSequence {
    std::vector<ir::gates::GateRewrite> gates;
    double phase = 0;
    // Cost used by the fusion pass (spec 14 §5.2): physical pulses first, then gates. `rz` is a
    // frame change and costs no pulse (§5.4).
    std::size_t pulses() const;
};

// Angles within `kAngleEps` of a special value take the shorter form (spec 14 §4.2): θ = 0 → one
// rz; θ = π/2 → one sx; θ = π → one x; rotations by a multiple of 2π are dropped.
inline constexpr double kAngleEps = 1e-12;
OneQubitSequence synthesize1q(const EulerAngles& a, Basis1q basis);
OneQubitSequence synthesize1q(num::ConstMatrixView m, Basis1q basis);

// Pulses of an existing run of single-qubit gates (every gate but rz/p-like frame changes).
bool isFrameChange(std::string_view gateName);

} // namespace qlab::compiler
