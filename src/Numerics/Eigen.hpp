#pragma once
// Spec 06 §5–§6 — Hermitian eigensolver, SVD, Schmidt decomposition.
#include "Numerics/Matrix.hpp"
namespace qlab::num {
struct EigResult { RealVector values; Matrix vectors; }; // values ascending; vectors as COLUMNS
// Hermitian eigendecomposition. n ≤ 16: cyclic complex Jacobi. Larger: LAPACK (Accelerate) when available, else Jacobi.
Result<EigResult> eigh(ConstMatrixView H, bool forceOwn = false);
struct SvdResult { RealVector singular; Matrix U; Matrix V; }; // A = U diag(s) V†, singular descending
Result<SvdResult> svd(ConstMatrixView A);
struct SchmidtResult { RealVector coefficients; Matrix low; Matrix high; }; // |ψ⟩ = Σ λ_k |low_k⟩ ⊗ |high_k⟩
// ψ index = iHigh*dimLow + iLow (low subsystem = least significant).
Result<SchmidtResult> schmidt(std::span<const Complex> psi, std::size_t dimLow, std::size_t dimHigh);
// f(H) for Hermitian H via eigendecomposition.
Result<Matrix> hermitianFunction(ConstMatrixView H, double (*f)(double));
Result<Matrix> sqrtm(ConstMatrixView psd);
} // namespace qlab::num
