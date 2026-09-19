#pragma once
// Spec 21 §3.11 — Pauli expectation table. Exact ⟨P⟩ from a state vector or a density matrix; from
// shots, the estimator Σ_b (−1)^{parity(b ∩ supp P)} p̂_b with its standard error
// σ = √((1 − ⟨P⟩²)/N), but only when the program measured every qubit of supp P in the basis P
// names — otherwise the row reads "not measured". Labels follow qsim::PauliString: the
// most-significant qubit first ("XZIY" = X on q3, Z on q2, I on q1, Y on q0).
#include "Core/Error.hpp"
#include "Data/Fidelity.hpp"
#include "Data/Histogram.hpp"
#include "Numerics/Types.hpp"
#include "QSim/Types.hpp"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::viz::math {

// ⟨ψ|P|ψ⟩ (real part; P is Hermitian for a real sign). O(2^n).
Result<double> pauliExpectation(std::span<const num::Complex> psi, const qsim::PauliString& p);
// Tr(ρP) for a 2^n × 2^n density matrix. O(2^n).
Result<double> pauliExpectation(const num::Matrix& rho, const qsim::PauliString& p);

// How the program read its qubits out: per qubit the basis letter ('Z' unless the program rotated
// into X or Y before measuring; 0 = not measured) and the classical bit holding the outcome.
struct MeasurementMap {
    std::vector<char> basis;              // index = qubit
    std::vector<std::int32_t> bitOfQubit; // index = qubit; −1 = not measured
    bool measures(std::uint32_t qubit, char letter) const;
};

struct PauliEstimate {
    double value = 0.0;
    double standardError = 0.0; // √((1 − ⟨P⟩²)/N)
    std::uint64_t shots = 0;
};
// nullopt when the string was not measured (some letter of supp P disagrees with the basis map).
std::optional<PauliEstimate> pauliFromCounts(const data::Histogram& counts,
                                             const qsim::PauliString& p, const MeasurementMap& map);

struct PauliRow {
    std::string label; // "XZIY"; single-qubit rows are "X0", "Y0", "Z0", "X1", …
    qsim::PauliString pauli;
    std::optional<double> exact;           // from the snapshot (Simulator-only)
    std::optional<PauliEstimate> estimate; // from shots (Physical)
    bool userAdded = false;
    data::FidelityClass cls = data::FidelityClass::Exact;
};

// The default rows: X_k, Y_k, Z_k for every qubit k < n (k ascending), as full-width strings.
std::vector<qsim::PauliString> singleQubitPaulis(std::uint32_t nQubits);
// Row label of a single-qubit Pauli ("Z3") or the MSB-first string for anything else.
std::string pauliRowLabel(const qsim::PauliString& p);
// Pads or rejects a user string so that it spans exactly n qubits (shorter strings act on the low
// qubits: "ZZ" on 4 qubits is "IIZZ"). Errors name the offending character.
Result<qsim::PauliString> parseUserPauli(std::string_view text, std::uint32_t nQubits);

} // namespace qlab::viz::math
