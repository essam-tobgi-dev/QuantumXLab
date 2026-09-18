#pragma once
// Spec 06 §3 — structure checks and distances.
#include "Numerics/Matrix.hpp"
#include <span>
namespace qlab::num {

bool isUnitary(ConstMatrixView U, double tolAbs = tol::kUnitaryTol);          // ‖U†U − I‖_max ≤ tol
bool isHermitian(ConstMatrixView H, double tolAbs = tol::kHermitianTol);
bool isPositiveSemidefinite(ConstMatrixView rho, double tolAbs = tol::kPsdTol); // min eigenvalue ≥ −tol
bool isTracePreserving(std::span<const Matrix> kraus, double tolAbs = tol::kTraceTol); // Σ K†K = I
bool isDensityMatrix(ConstMatrixView rho, double tolAbs = tol::kTraceTol);     // Hermitian, PSD, Tr = 1
bool isNormalized(std::span<const Complex> psi, double tolAbs = tol::kNormTol);

double traceDistance(ConstMatrixView rho, ConstMatrixView sigma);   // ½‖ρ−σ‖₁
double fidelity(ConstMatrixView rho, ConstMatrixView sigma);        // (Tr √(√ρ σ √ρ))² — squared convention
double fidelity(std::span<const Complex> psi, std::span<const Complex> phi); // |⟨ψ|φ⟩|²
double fidelity(std::span<const Complex> psi, ConstMatrixView rho);         // ⟨ψ|ρ|ψ⟩
double purity(ConstMatrixView rho);                                  // Tr ρ²
double hellingerFidelity(std::span<const double> p, std::span<const double> q); // (Σ √(p q))²
double vonNeumannEntropy(ConstMatrixView rho);                       // −Tr ρ log₂ ρ (bits)

// U ≈ e^{iφ} V. φ = arg Tr(U†V); if |Tr(U†V)| < 1e-6, falls back to the first non-zero column ratio.
bool equalUpToGlobalPhase(ConstMatrixView U, ConstMatrixView V, double tolAbs = tol::kPhaseTol);
// Returns the relative phase φ such that U ≈ e^{iφ} V (0 if undefined).
double globalPhaseBetween(ConstMatrixView U, ConstMatrixView V);
// Average gate fidelity between two unitaries: (|Tr(U†V)|² + d) / (d(d+1)) (T10 §1).
double averageGateFidelity(ConstMatrixView U, ConstMatrixView V);

} // namespace qlab::num
