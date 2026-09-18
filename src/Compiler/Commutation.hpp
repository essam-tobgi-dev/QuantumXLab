#pragma once
// Spec 14 §5.3; T02 §4 — commutation by wire action. A gate acts Z-type on a wire when it commutes
// with Z there (it is block diagonal in that wire's computational basis: rz, the control of cx,
// both wires of cz) and X-type when it commutes with X there (x, sx, rx, the target of cx, both
// wires of rxx). Two gates commute when, on every wire they share, both act Z-type or both X-type:
// they are then block diagonal in a common basis of the shared wires and act on disjoint wires
// inside each block. This covers every rule of §5.3: rz/p/z/s/t through a control, rx/x/sx
// through a target, cx sharing a control, cx sharing a target.
#include "IR/Node.hpp"
#include <cstdint>
#include <optional>

namespace qlab::compiler {

inline constexpr std::uint8_t kActsZ = 1;
inline constexpr std::uint8_t kActsX = 2;

// Action of `g` on wire k of `g.wires()` (targets first, then controls): a mask of kActsZ/kActsX,
// 0 when neither holds. Controls (either polarity) are Z-type. Parametrised single-qubit gates
// (U, u2, u3, explicit matrices) are classified from their 2×2 matrix.
std::uint8_t wireAction(const ir::Gate& g, std::size_t k);

// The wire rule above. Defcal-only gates commute with nothing.
bool commutes(const ir::Gate& a, const ir::Gate& b);

// Gates whose matrix is invariant under exchange of their two operands.
bool operandSymmetric(std::string_view name);
// Same operand tuple: equal targets and controls (with polarity), or the same two targets in
// either order for an operand-symmetric gate.
bool sameOperands(const ir::Gate& a, const ir::Gate& b);

// True when b undoes a: a named inverse pair on the same operands (x·x, s·sdg, rz(a)·rz(−a),
// cx·cx, adjoint flags) or, for gates on at most two wires, matrices with b·a = e^{iφ}·I
// (φ = 0 is required when the gates carry controls).
bool isInverseOf(const ir::Gate& a, const ir::Gate& b);
// The structural half of `isInverseOf`: named inverse pairs and adjoint flags, no matrix is formed.
bool isNamedInverseOf(const ir::Gate& a, const ir::Gate& b);

// Rotation angle of a diagonal single-qubit gate as an rz: δ = arg m11 − arg m00. nullopt when
// the gate is not a diagonal single-qubit gate.
std::optional<double> diagonalAngle(const ir::Gate& g);

} // namespace qlab::compiler
