#pragma once
// Spec 07 §8 — entanglement and state measures. Every quantity here is Simulator-only: it cannot
// be read from a physical machine without tomography (spec 00 §6), and the probe instruments of
// spec 12 §8 are the only sanctioned UI path to them.
#include "QSim/Types.hpp"
#include <array>
#include <span>

namespace qlab::qsim::measures {

// Bloch vector r = (Tr ρX, Tr ρY, Tr ρZ) of a single-qubit density matrix (T01 §5).
// |r| = 1 for pure states, 0 for the maximally mixed state.
Result<std::array<double, 3>> blochVector(const Matrix& rho1q);

// Von Neumann entropy S(ρ) = −Tr ρ log₂ ρ, in bits. Eigenvalues below 1e-15 are dropped.
double entropyBits(const Matrix& rho);

// Purity Tr ρ².
double purity(const Matrix& rho);

// Two-qubit concurrence (T01 §8): C = max(0, λ₁ − λ₂ − λ₃ − λ₄) where λ are the decreasing
// square roots of the eigenvalues of ρ ρ̃ with ρ̃ = (Y⊗Y) ρ* (Y⊗Y). rho must be 4×4.
Result<double> concurrence(const Matrix& rho2q);

// Schmidt coefficients of a pure state across the bipartition {keep} | rest (pure states only).
// Returns the singular values in decreasing order; Σ λ² = 1.
Result<num::RealVector> schmidtCoefficients(std::span<const Complex> psi, std::uint32_t n,
                                            std::span<const QubitIndex> keep);

// Mutual information I(A:B) = S(ρ_A) + S(ρ_B) − S(ρ_AB) in bits; used by the entanglement graph
// of spec 21 §3.8. Caller supplies the three reduced states.
double mutualInformation(const Matrix& rhoA, const Matrix& rhoB, const Matrix& rhoAB);

// Fidelity of a state against a target: ⟨φ|ρ|φ⟩ for a pure target, squared Uhlmann otherwise.
double fidelityTo(const Matrix& rho, std::span<const Complex> target);
double fidelityTo(const Matrix& rho, const Matrix& sigma);

} // namespace qlab::qsim::measures
