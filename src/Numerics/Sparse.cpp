#include "Numerics/Sparse.hpp"
#include <algorithm>
#include <map>
namespace qlab::num {

SparseMatrix SparseMatrix::fromTriplets(std::size_t r, std::size_t c, std::vector<Triplet> t) {
    std::sort(t.begin(), t.end(), [](const Triplet& a, const Triplet& b) { return a.row != b.row ? a.row < b.row : a.col < b.col; });
    SparseMatrix m(r, c);
    std::size_t i = 0;
    for (std::size_t row = 0; row < r; ++row) {
        while (i < t.size() && t[i].row == row) {
            std::size_t col = t[i].col; Complex v{};
            while (i < t.size() && t[i].row == row && t[i].col == col) { v += t[i].value; ++i; }
            if (v != Complex{}) { m.colIdx.push_back(col); m.values.push_back(v); }
        }
        m.rowPtr[row + 1] = m.values.size();
    }
    return m;
}
SparseMatrix SparseMatrix::fromDense(ConstMatrixView d, double dropTol) {
    SparseMatrix m(d.rows, d.cols);
    for (std::size_t i = 0; i < d.rows; ++i) {
        for (std::size_t j = 0; j < d.cols; ++j)
            if (std::abs(d(i, j)) > dropTol) { m.colIdx.push_back(j); m.values.push_back(d(i, j)); }
        m.rowPtr[i + 1] = m.values.size();
    }
    return m;
}
SparseMatrix SparseMatrix::identity(std::size_t n) {
    SparseMatrix m(n, n);
    for (std::size_t i = 0; i < n; ++i) { m.colIdx.push_back(i); m.values.push_back(Complex(1, 0)); m.rowPtr[i + 1] = i + 1; }
    return m;
}
Matrix SparseMatrix::toDense() const {
    Matrix d(rows, cols);
    for (std::size_t i = 0; i < rows; ++i)
        for (std::size_t k = rowPtr[i]; k < rowPtr[i + 1]; ++k) d(i, colIdx[k]) += values[k];
    return d;
}
void SparseMatrix::matvecInto(std::span<const Complex> x, std::span<Complex> y) const {
    for (std::size_t i = 0; i < rows; ++i) {
        Complex s{};
        for (std::size_t k = rowPtr[i]; k < rowPtr[i + 1]; ++k) s += values[k] * x[colIdx[k]];
        y[i] = s;
    }
}
Vector SparseMatrix::matvec(std::span<const Complex> x) const { Vector y(rows); matvecInto(x, y); return y; }
SparseMatrix& SparseMatrix::scale(Complex s) { for (auto& v : values) v *= s; return *this; }
SparseMatrix SparseMatrix::adjoint() const {
    std::vector<Triplet> t; t.reserve(nnz());
    for (std::size_t i = 0; i < rows; ++i)
        for (std::size_t k = rowPtr[i]; k < rowPtr[i + 1]; ++k) t.push_back({colIdx[k], i, std::conj(values[k])});
    return fromTriplets(cols, rows, std::move(t));
}
Complex SparseMatrix::at(std::size_t i, std::size_t j) const {
    for (std::size_t k = rowPtr[i]; k < rowPtr[i + 1]; ++k) if (colIdx[k] == j) return values[k];
    return Complex{};
}
SparseMatrix add(const SparseMatrix& a, const SparseMatrix& b, Complex alpha, Complex beta) {
    std::vector<Triplet> t; t.reserve(a.nnz() + b.nnz());
    for (std::size_t i = 0; i < a.rows; ++i)
        for (std::size_t k = a.rowPtr[i]; k < a.rowPtr[i + 1]; ++k) t.push_back({i, a.colIdx[k], alpha * a.values[k]});
    for (std::size_t i = 0; i < b.rows; ++i)
        for (std::size_t k = b.rowPtr[i]; k < b.rowPtr[i + 1]; ++k) t.push_back({i, b.colIdx[k], beta * b.values[k]});
    return SparseMatrix::fromTriplets(a.rows, a.cols, std::move(t));
}
SparseMatrix kron(const SparseMatrix& a, const SparseMatrix& b) {
    std::vector<Triplet> t; t.reserve(a.nnz() * b.nnz());
    for (std::size_t i = 0; i < a.rows; ++i)
        for (std::size_t ka = a.rowPtr[i]; ka < a.rowPtr[i + 1]; ++ka)
            for (std::size_t p = 0; p < b.rows; ++p)
                for (std::size_t kb = b.rowPtr[p]; kb < b.rowPtr[p + 1]; ++kb)
                    t.push_back({i * b.rows + p, a.colIdx[ka] * b.cols + b.colIdx[kb], a.values[ka] * b.values[kb]});
    return SparseMatrix::fromTriplets(a.rows * b.rows, a.cols * b.cols, std::move(t));
}
SparseMatrix matmul(const SparseMatrix& a, const SparseMatrix& b) {
    std::vector<Triplet> t;
    for (std::size_t i = 0; i < a.rows; ++i) {
        std::map<std::size_t, Complex> row;
        for (std::size_t ka = a.rowPtr[i]; ka < a.rowPtr[i + 1]; ++ka) {
            std::size_t j = a.colIdx[ka];
            for (std::size_t kb = b.rowPtr[j]; kb < b.rowPtr[j + 1]; ++kb) row[b.colIdx[kb]] += a.values[ka] * b.values[kb];
        }
        for (auto& [c, v] : row) t.push_back({i, c, v});
    }
    return SparseMatrix::fromTriplets(a.rows, b.cols, std::move(t));
}
} // namespace qlab::num
