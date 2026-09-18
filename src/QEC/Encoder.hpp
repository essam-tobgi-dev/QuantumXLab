#pragma once
// Spec 16 §3 — explicit encoders and transversal logical gates, both derived from the code
// definition (T09 §1.3 conjugation rules) and checked against it before they are returned.
#include "QEC/Extraction.hpp"
#include <span>

namespace qlab::qec {

// U P U† for the Clifford U given by schedule gates applied in order (h, s, sdg, x, y, z, cx, cy,
// cz, swap; markers are skipped). Fails on reset and measure.
Result<PauliString> conjugated(const PauliString& p, std::span<const Op> gates);

// Unitary encoder on the n data qubits: qubit l < k carries the state of logical qubit l, the other
// n − k qubits start in |0⟩. It maps Z_l → Z̄_l, X_l → X̄_l and Z_q (q ≥ k) into the stabilizer
// group, so every generator reads +1 on the output. Synthesized by Gaussian elimination over the
// code's tableau, hence available for every valid code (repetition, Shor, Steane, [[5,1,3]] and the
// surface codes alike); for the repetition codes it is the textbook pair of CNOTs.
Result<Schedule> planEncoder(const StabilizerCode& code);
Result<ir::Circuit> buildEncoder(const StabilizerCode& code);

enum class LogicalGate : std::uint8_t { X, Z, H, S, CNOT };
std::string_view logicalGateName(LogicalGate g);

// Transversal logical gate on logical qubit 0, where the code has one (spec 16 §3, T09 §8.1):
//  X, Z   the letters of X̄ / Z̄ as single-qubit Paulis (every code);
//  H      h on every qubit, if that preserves the stabilizer group and exchanges X̄ and Z̄;
//  S      s or sdg on every qubit, whichever acts as S̄ (Steane: sdg, T09 §4.4);
//  CNOT   two blocks (control block on wires 0..n−1, target block on n..2n−1), qubit-wise cx in
//         the direction that acts as the logical CNOT (CSS codes).
// Fails with err::NoGate when the code has no such transversal gate (Eastin–Knill, T09 §8.1).
Result<Schedule> planLogicalGate(const StabilizerCode& code, LogicalGate gate);
Result<ir::Circuit> buildLogicalGate(const StabilizerCode& code, LogicalGate gate);

} // namespace qlab::qec
