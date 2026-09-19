#include "Numerics/Matrix.hpp"
#include "Core/JobSystem.hpp"
#include "Numerics/Blas.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace qlab::num {

Matrix::Matrix(ConstMatrixView v) : rows(v.rows), cols(v.cols), data(v.rows * v.cols) {
    for (std::size_t i = 0; i < rows; ++i)
        for (std::size_t j = 0; j < cols; ++j)
            data[i * cols + j] = v(i, j);
}
Matrix Matrix::identity(std::size_t n) {
    Matrix m(n, n);
    for (std::size_t i = 0; i < n; ++i)
        m(i, i) = 1.0;
    return m;
}
Matrix Matrix::fromRows(std::initializer_list<std::initializer_list<Complex>> rowsInit) {
    std::size_t r = rowsInit.size(), c = r ? rowsInit.begin()->size() : 0;
    Matrix m(r, c);
    std::size_t i = 0;
    for (auto& row : rowsInit) {
        std::size_t j = 0;
        for (auto v : row)
            m(i, j++) = v;
        ++i;
    }
    return m;
}
Matrix Matrix::diagonal(std::span<const Complex> d) {
    Matrix m(d.size(), d.size());
    for (std::size_t i = 0; i < d.size(); ++i)
        m(i, i) = d[i];
    return m;
}
Matrix Matrix::diagonal(std::span<const double> d) {
    Matrix m(d.size(), d.size());
    for (std::size_t i = 0; i < d.size(); ++i)
        m(i, i) = d[i];
    return m;
}
Matrix& Matrix::operator+=(ConstMatrixView b) {
    for (std::size_t i = 0; i < rows; ++i)
        for (std::size_t j = 0; j < cols; ++j)
            (*this)(i, j) += b(i, j);
    return *this;
}
Matrix& Matrix::operator-=(ConstMatrixView b) {
    for (std::size_t i = 0; i < rows; ++i)
        for (std::size_t j = 0; j < cols; ++j)
            (*this)(i, j) -= b(i, j);
    return *this;
}
Matrix& Matrix::operator*=(Complex s) {
    for (auto& v : data)
        v *= s;
    return *this;
}

double kahanSum(std::span<const double> x) {
    double sum = 0.0, c = 0.0;
    for (double v : x) {
        double y = v - c;
        double t = sum + y;
        c = (t - sum) - y;
        sum = t;
    }
    return sum;
}
namespace {
double pairwiseNorm2(const Complex* v, std::size_t n) {
    if (n <= 128) {
        double s = 0;
        for (std::size_t i = 0; i < n; ++i)
            s += std::norm(v[i]);
        return s;
    }
    std::size_t h = n / 2;
    return pairwiseNorm2(v, h) + pairwiseNorm2(v + h, n - h);
}
Complex pairwiseDot(const Complex* a, const Complex* b, std::size_t n) {
    if (n <= 128) {
        Complex s{};
        for (std::size_t i = 0; i < n; ++i)
            s += std::conj(a[i]) * b[i];
        return s;
    }
    std::size_t h = n / 2;
    return pairwiseDot(a, b, h) + pairwiseDot(a + h, b + h, n - h);
}
} // namespace
double norm2Squared(std::span<const Complex> v) {
    return pairwiseNorm2(v.data(), v.size());
}
Complex dot(std::span<const Complex> a, std::span<const Complex> b) {
    assert(a.size() == b.size());
    return pairwiseDot(a.data(), b.data(), a.size());
}

bool sameShape(ConstMatrixView a, ConstMatrixView b) {
    return a.rows == b.rows && a.cols == b.cols;
}
Matrix add(ConstMatrixView a, ConstMatrixView b) {
    assert(sameShape(a, b));
    Matrix r(a);
    r += b;
    return r;
}
Matrix sub(ConstMatrixView a, ConstMatrixView b) {
    assert(sameShape(a, b));
    Matrix r(a);
    r -= b;
    return r;
}
Matrix scale(ConstMatrixView a, Complex s) {
    Matrix r(a);
    r *= s;
    return r;
}

void matmulInto(ConstMatrixView a, ConstMatrixView b, MatrixView c, Complex alpha, Complex beta) {
    assert(a.cols == b.rows && c.rows == a.rows && c.cols == b.cols);
    const std::size_t n = a.rows, m = b.cols, k = a.cols;
    if (Blas::available() && n >= Blas::kMinDim && m >= Blas::kMinDim && k >= Blas::kMinDim) {
        Blas::zgemm(a, b, c, alpha, beta);
        return;
    }
    auto rowBlock = [&](std::size_t i0, std::size_t i1) {
        for (std::size_t i = i0; i < i1; ++i) {
            Complex* crow = c.ptr + i * c.rowStride;
            for (std::size_t j = 0; j < m; ++j)
                crow[j] = (beta == 0.0) ? Complex{} : beta * crow[j];
            for (std::size_t p = 0; p < k; ++p) {
                Complex aip = alpha * a(i, p);
                if (aip == 0.0)
                    continue;
                const Complex* brow = b.ptr + p * b.rowStride;
                for (std::size_t j = 0; j < m; ++j)
                    crow[j] += aip * brow[j];
            }
        }
    };
    if (n * m * k >= (1u << 18) && n >= 8)
        core::JobSystem::global().parallelFor(n, std::max<std::size_t>(1, n / 16), rowBlock);
    else
        rowBlock(0, n);
}
Matrix matmul(ConstMatrixView a, ConstMatrixView b) {
    Matrix c(a.rows, b.cols);
    matmulInto(a, b, c.view());
    return c;
}
Matrix adjoint(ConstMatrixView a) {
    Matrix r(a.cols, a.rows);
    for (std::size_t i = 0; i < a.rows; ++i)
        for (std::size_t j = 0; j < a.cols; ++j)
            r(j, i) = std::conj(a(i, j));
    return r;
}
Matrix transpose(ConstMatrixView a) {
    Matrix r(a.cols, a.rows);
    for (std::size_t i = 0; i < a.rows; ++i)
        for (std::size_t j = 0; j < a.cols; ++j)
            r(j, i) = a(i, j);
    return r;
}
Matrix conj(ConstMatrixView a) {
    Matrix r(a);
    for (auto& v : r.data)
        v = std::conj(v);
    return r;
}
Complex trace(ConstMatrixView a) {
    Complex t{};
    for (std::size_t i = 0; i < std::min(a.rows, a.cols); ++i)
        t += a(i, i);
    return t;
}
double frobeniusNorm(ConstMatrixView a) {
    double s = 0;
    for (std::size_t i = 0; i < a.rows; ++i)
        for (std::size_t j = 0; j < a.cols; ++j)
            s += std::norm(a(i, j));
    return std::sqrt(s);
}
double maxAbsNorm(ConstMatrixView a) {
    double s = 0;
    for (std::size_t i = 0; i < a.rows; ++i)
        for (std::size_t j = 0; j < a.cols; ++j)
            s = std::max(s, std::abs(a(i, j)));
    return s;
}
double norm1(ConstMatrixView a) {
    double best = 0;
    for (std::size_t j = 0; j < a.cols; ++j) {
        double s = 0;
        for (std::size_t i = 0; i < a.rows; ++i)
            s += std::abs(a(i, j));
        best = std::max(best, s);
    }
    return best;
}
void matvecInto(ConstMatrixView a, std::span<const Complex> x, std::span<Complex> y) {
    assert(x.size() == a.cols && y.size() == a.rows);
    for (std::size_t i = 0; i < a.rows; ++i) {
        Complex s{};
        const Complex* row = a.ptr + i * a.rowStride;
        for (std::size_t j = 0; j < a.cols; ++j)
            s += row[j] * x[j];
        y[i] = s;
    }
}
Vector matvec(ConstMatrixView a, std::span<const Complex> x) {
    Vector y(a.rows);
    matvecInto(a, x, y);
    return y;
}
Matrix outer(std::span<const Complex> a, std::span<const Complex> b) {
    Matrix m(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j)
            m(i, j) = a[i] * std::conj(b[j]);
    return m;
}
Matrix projector(std::span<const Complex> psi) {
    return outer(psi, psi);
}
bool approxEqual(ConstMatrixView a, ConstMatrixView b, double tolAbs) {
    if (!sameShape(a, b))
        return false;
    for (std::size_t i = 0; i < a.rows; ++i)
        for (std::size_t j = 0; j < a.cols; ++j)
            if (std::abs(a(i, j) - b(i, j)) > tolAbs)
                return false;
    return true;
}
} // namespace qlab::num
