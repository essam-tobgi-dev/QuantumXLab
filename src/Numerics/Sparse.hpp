#pragma once
// Spec 06 §7 — CSR complex sparse matrix for multi-level Hamiltonians.
#include "Numerics/Matrix.hpp"
#include <span>
#include <tuple>
#include <vector>
namespace qlab::num {
struct Triplet { std::size_t row, col; Complex value; };
struct SparseMatrix {
    std::size_t rows = 0, cols = 0;
    std::vector<std::size_t> rowPtr; // size rows+1
    std::vector<std::size_t> colIdx;
    Vector values;

    SparseMatrix() = default;
    SparseMatrix(std::size_t r, std::size_t c) : rows(r), cols(c), rowPtr(r + 1, 0) {}
    static SparseMatrix fromTriplets(std::size_t r, std::size_t c, std::vector<Triplet> t); // sums duplicates
    static SparseMatrix fromDense(ConstMatrixView d, double dropTol = 0.0);
    static SparseMatrix identity(std::size_t n);
    Matrix toDense() const;
    std::size_t nnz() const { return values.size(); }
    void matvecInto(std::span<const Complex> x, std::span<Complex> y) const; // y = A x
    Vector matvec(std::span<const Complex> x) const;
    SparseMatrix& scale(Complex s);
    SparseMatrix adjoint() const;
    Complex at(std::size_t i, std::size_t j) const;
};
SparseMatrix add(const SparseMatrix& a, const SparseMatrix& b, Complex alpha = 1.0, Complex beta = 1.0); // αA + βB
SparseMatrix kron(const SparseMatrix& a, const SparseMatrix& b); // left factor more significant (same rule as dense)
SparseMatrix matmul(const SparseMatrix& a, const SparseMatrix& b);
} // namespace qlab::num
