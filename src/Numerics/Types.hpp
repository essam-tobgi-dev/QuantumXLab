#pragma once
// Spec 06 §1, §10 — scalars, storage, views, tolerances.
#include "Core/Aligned.hpp"
#include "Core/Error.hpp"
#include <array>
#include <cassert>
#include <complex>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>

namespace qlab::num {

using Complex = std::complex<double>;
using Vector = core::aligned_vector<Complex>;
using RealVector = std::vector<double>;

namespace tol {
inline constexpr double kUnitaryTol = 1e-12;
inline constexpr double kNormTol = 1e-10;
inline constexpr double kTraceTol = 1e-10;
inline constexpr double kHermitianTol = 1e-12;
inline constexpr double kPsdTol = 1e-9;
inline constexpr double kPhaseTol = 1e-9;
inline constexpr double kEigTol = 1e-13;
inline constexpr double kFftTol = 1e-12;
} // namespace tol

inline constexpr std::size_t kDenseLiouvillianMaxDim = 64;

struct Matrix;

// Non-owning views (spec 06 §1). `rowStride` in elements.
struct ConstMatrixView {
    const Complex* ptr = nullptr;
    std::size_t rows = 0, cols = 0, rowStride = 0;
    ConstMatrixView() = default;
    ConstMatrixView(const Complex* p, std::size_t r, std::size_t c, std::size_t stride = 0)
        : ptr(p), rows(r), cols(c), rowStride(stride ? stride : c) {}
    ConstMatrixView(const Matrix& m);
    const Complex& operator()(std::size_t i, std::size_t j) const { return ptr[i * rowStride + j]; }
    bool square() const { return rows == cols; }
};
struct MatrixView {
    Complex* ptr = nullptr;
    std::size_t rows = 0, cols = 0, rowStride = 0;
    MatrixView() = default;
    MatrixView(Complex* p, std::size_t r, std::size_t c, std::size_t stride = 0)
        : ptr(p), rows(r), cols(c), rowStride(stride ? stride : c) {}
    MatrixView(Matrix& m);
    Complex& operator()(std::size_t i, std::size_t j) const { return ptr[i * rowStride + j]; }
    operator ConstMatrixView() const { return ConstMatrixView(ptr, rows, cols, rowStride); }
    bool square() const { return rows == cols; }
};

// Dense row-major complex matrix, owning.
struct Matrix {
    std::size_t rows = 0, cols = 0;
    Vector data;

    Matrix() = default;
    Matrix(std::size_t r, std::size_t c) : rows(r), cols(c), data(r * c, Complex{}) {}
    Matrix(std::size_t r, std::size_t c, Complex fill) : rows(r), cols(c), data(r * c, fill) {}
    explicit Matrix(ConstMatrixView v);
    static Matrix identity(std::size_t n);
    static Matrix zeros(std::size_t r, std::size_t c) { return Matrix(r, c); }
    static Matrix fromRows(std::initializer_list<std::initializer_list<Complex>> rowsInit);
    static Matrix diagonal(std::span<const Complex> d);
    static Matrix diagonal(std::span<const double> d);

    Complex& operator()(std::size_t i, std::size_t j) { return data[i * cols + j]; }
    const Complex& operator()(std::size_t i, std::size_t j) const { return data[i * cols + j]; }
    Complex& at(std::size_t i, std::size_t j) { assert(i < rows && j < cols); return data[i * cols + j]; }
    bool square() const { return rows == cols; }
    std::size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }
    MatrixView view() { return MatrixView(data.data(), rows, cols); }
    ConstMatrixView view() const { return ConstMatrixView(data.data(), rows, cols); }
    Matrix& operator+=(ConstMatrixView b);
    Matrix& operator-=(ConstMatrixView b);
    Matrix& operator*=(Complex s);
    Matrix& operator*=(double s) { return (*this) *= Complex(s, 0.0); }
};

inline ConstMatrixView::ConstMatrixView(const Matrix& m) : ptr(m.data.data()), rows(m.rows), cols(m.cols), rowStride(m.cols) {}
inline MatrixView::MatrixView(Matrix& m) : ptr(m.data.data()), rows(m.rows), cols(m.cols), rowStride(m.cols) {}

struct RealMatrix {
    std::size_t rows = 0, cols = 0;
    RealVector data;
    RealMatrix() = default;
    RealMatrix(std::size_t r, std::size_t c, double fill = 0.0) : rows(r), cols(c), data(r * c, fill) {}
    double& operator()(std::size_t i, std::size_t j) { return data[i * cols + j]; }
    double operator()(std::size_t i, std::size_t j) const { return data[i * cols + j]; }
    static RealMatrix identity(std::size_t n) { RealMatrix m(n, n); for (std::size_t i = 0; i < n; ++i) m(i, i) = 1.0; return m; }
};

// Small fixed-size matrices for gate kernels (spec 06 §1). Row-major std::array storage.
template <std::size_t N> struct MatN {
    std::array<Complex, N * N> a{};
    constexpr MatN() = default;
    constexpr MatN(std::initializer_list<Complex> init) {
        std::size_t k = 0;
        for (auto v : init) { if (k < N * N) a[k++] = v; }
    }
    constexpr Complex& operator()(std::size_t i, std::size_t j) { return a[i * N + j]; }
    constexpr const Complex& operator()(std::size_t i, std::size_t j) const { return a[i * N + j]; }
    static constexpr MatN identity() { MatN m; for (std::size_t i = 0; i < N; ++i) m(i, i) = Complex(1, 0); return m; }
    static constexpr std::size_t dim() { return N; }
    Matrix toMatrix() const { Matrix m(N, N); for (std::size_t i = 0; i < N * N; ++i) m.data[i] = a[i]; return m; }
    static MatN fromMatrix(ConstMatrixView m) {
        MatN r; assert(m.rows == N && m.cols == N);
        for (std::size_t i = 0; i < N; ++i) for (std::size_t j = 0; j < N; ++j) r(i, j) = m(i, j);
        return r;
    }
    constexpr MatN operator*(const MatN& o) const {
        MatN r;
        for (std::size_t i = 0; i < N; ++i)
            for (std::size_t j = 0; j < N; ++j) {
                Complex s{};
                for (std::size_t k = 0; k < N; ++k) s += (*this)(i, k) * o(k, j);
                r(i, j) = s;
            }
        return r;
    }
    constexpr MatN adjoint() const {
        MatN r;
        for (std::size_t i = 0; i < N; ++i) for (std::size_t j = 0; j < N; ++j) r(j, i) = std::conj((*this)(i, j));
        return r;
    }
    constexpr bool isDiagonal(double eps = 1e-15) const {
        for (std::size_t i = 0; i < N; ++i) for (std::size_t j = 0; j < N; ++j)
            if (i != j && std::abs((*this)(i, j)) > eps) return false;
        return true;
    }
};
using Mat2 = MatN<2>;
using Mat4 = MatN<4>;
using Mat8 = MatN<8>;

// Kahan/pairwise-safe accumulation helpers (spec 06 §10).
double kahanSum(std::span<const double> x);
double norm2Squared(std::span<const Complex> v); // Σ|v_i|² (pairwise)
Complex dot(std::span<const Complex> a, std::span<const Complex> b); // ⟨a|b⟩ = Σ conj(a_i) b_i

} // namespace qlab::num
