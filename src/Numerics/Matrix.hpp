#pragma once
// Spec 06 §1 — dense operations.
#include "Numerics/Types.hpp"
namespace qlab::num {

Matrix add(ConstMatrixView a, ConstMatrixView b);
Matrix sub(ConstMatrixView a, ConstMatrixView b);
Matrix scale(ConstMatrixView a, Complex s);
Matrix matmul(ConstMatrixView a, ConstMatrixView b);            // dispatches to Blas for dim ≥ 64
void matmulInto(ConstMatrixView a, ConstMatrixView b, MatrixView c, Complex alpha = 1.0, Complex beta = 0.0);
Matrix adjoint(ConstMatrixView a);
Matrix transpose(ConstMatrixView a);
Matrix conj(ConstMatrixView a);
Complex trace(ConstMatrixView a);
double frobeniusNorm(ConstMatrixView a);
double maxAbsNorm(ConstMatrixView a);
double norm1(ConstMatrixView a); // max column sum
Vector matvec(ConstMatrixView a, std::span<const Complex> x);
void matvecInto(ConstMatrixView a, std::span<const Complex> x, std::span<Complex> y);
Matrix outer(std::span<const Complex> a, std::span<const Complex> b); // |a⟩⟨b|
Matrix projector(std::span<const Complex> psi);                        // |ψ⟩⟨ψ|
bool sameShape(ConstMatrixView a, ConstMatrixView b);
bool approxEqual(ConstMatrixView a, ConstMatrixView b, double tolAbs);

inline Matrix operator+(const Matrix& a, const Matrix& b) { return add(a, b); }
inline Matrix operator-(const Matrix& a, const Matrix& b) { return sub(a, b); }
inline Matrix operator*(const Matrix& a, const Matrix& b) { return matmul(a, b); }
inline Matrix operator*(Complex s, const Matrix& a) { return scale(a, s); }
inline Matrix operator*(const Matrix& a, Complex s) { return scale(a, s); }
inline Matrix operator*(double s, const Matrix& a) { return scale(a, Complex(s, 0)); }
inline Vector operator*(const Matrix& a, const Vector& x) { return matvec(a, x); }

// Pauli matrices and common gates as constexpr Mat2 (spec T02).
namespace pauli {
inline constexpr Complex I_{0, 1};
inline const Mat2 I{1, 0, 0, 1};
inline const Mat2 X{0, 1, 1, 0};
inline const Mat2 Y{0, Complex(0, -1), Complex(0, 1), 0};
inline const Mat2 Z{1, 0, 0, -1};
} // namespace pauli

} // namespace qlab::num
