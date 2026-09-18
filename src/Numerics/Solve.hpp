#pragma once
// Dense linear solves (LU with partial pivoting) used by expm and fits.
#include "Numerics/Matrix.hpp"
namespace qlab::num {
// Solve A X = B for X (A square). Returns error if singular.
Result<Matrix> solve(ConstMatrixView A, ConstMatrixView B);
Result<Matrix> inverse(ConstMatrixView A);
Complex determinant(ConstMatrixView A);
} // namespace qlab::num
