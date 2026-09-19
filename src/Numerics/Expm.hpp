#pragma once
// Spec 06 §4 — matrix exponential family (T11 §5).
#include "Numerics/Matrix.hpp"
#include <functional>
#include <span>
namespace qlab::num {

// General: scaling-and-squaring with Padé(13) (Higham 2005, θ13 = 5.371920351148152).
Result<Matrix> expm(ConstMatrixView A);
// Hermitian H: V diag(e^{-i t λ}) V†  — i.e. exp(-i H t). For exp(+H t) pass t as complex via
// expmHermitianGeneral.
Result<Matrix> expmHermitian(ConstMatrixView H, double t);
// exp(s H) for Hermitian H and complex scalar s.
Result<Matrix> expmHermitianGeneral(ConstMatrixView H, Complex s);
// Closed form: exp(-i θ/2 n·σ) = cos(θ/2) I − i sin(θ/2) (n·σ), n normalized internally.
Mat2 expPauli(double nx, double ny, double nz, double theta);
// Closed form for 4×4 exp(-i θ/2 P⊗Q) with P,Q Pauli (or I): cos(θ/2) I − i sin(θ/2) P⊗Q.
Mat4 expPauliPair(const Mat2& P, const Mat2& Q, double theta);
// y = exp(A) v without forming exp(A): Krylov (Arnoldi, m ≤ 30) with Padé on the small Hessenberg;
// falls back to Taylor for tiny norms. Sparse operator supplied as a matvec functor.
Vector expmTimesVector(ConstMatrixView A, std::span<const Complex> v, double tolAbs = 1e-12);
Vector
expmTimesVector(std::size_t dim,
                const std::function<void(std::span<const Complex>, std::span<Complex>)>& applyA,
                std::span<const Complex> v, double tolAbs = 1e-12, std::size_t maxKrylov = 30);
} // namespace qlab::num
