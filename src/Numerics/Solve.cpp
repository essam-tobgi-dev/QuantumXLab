#include "Numerics/Solve.hpp"
#include <cmath>
#include <vector>
namespace qlab::num {
namespace {
struct Lu {
    Matrix lu;
    std::vector<std::size_t> perm;
    int sign = 1;
    bool singular = false;
};
Lu luDecompose(ConstMatrixView A) {
    Lu r;
    r.lu = Matrix(A);
    const std::size_t n = A.rows;
    r.perm.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        r.perm[i] = i;
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t p = k;
        double best = std::abs(r.lu(k, k));
        for (std::size_t i = k + 1; i < n; ++i)
            if (std::abs(r.lu(i, k)) > best) {
                best = std::abs(r.lu(i, k));
                p = i;
            }
        if (best == 0.0) {
            r.singular = true;
            return r;
        }
        if (p != k) {
            for (std::size_t j = 0; j < n; ++j)
                std::swap(r.lu(k, j), r.lu(p, j));
            std::swap(r.perm[k], r.perm[p]);
            r.sign = -r.sign;
        }
        for (std::size_t i = k + 1; i < n; ++i) {
            Complex f = r.lu(i, k) / r.lu(k, k);
            r.lu(i, k) = f;
            if (f == 0.0)
                continue;
            for (std::size_t j = k + 1; j < n; ++j)
                r.lu(i, j) -= f * r.lu(k, j);
        }
    }
    return r;
}
} // namespace
Result<Matrix> solve(ConstMatrixView A, ConstMatrixView B) {
    if (!A.square() || A.rows != B.rows)
        return fail(ErrorCode::InvalidArgument, "solve: shape mismatch");
    Lu lu = luDecompose(A);
    if (lu.singular)
        return fail(ErrorCode::InvalidArgument, "solve: singular matrix");
    const std::size_t n = A.rows, m = B.cols;
    Matrix X(n, m);
    for (std::size_t c = 0; c < m; ++c) {
        std::vector<Complex> y(n);
        for (std::size_t i = 0; i < n; ++i) {
            Complex s = B(lu.perm[i], c);
            for (std::size_t j = 0; j < i; ++j)
                s -= lu.lu(i, j) * y[j];
            y[i] = s;
        }
        for (std::size_t ii = n; ii-- > 0;) {
            Complex s = y[ii];
            for (std::size_t j = ii + 1; j < n; ++j)
                s -= lu.lu(ii, j) * X(j, c);
            X(ii, c) = s / lu.lu(ii, ii);
        }
    }
    return X;
}
Result<Matrix> inverse(ConstMatrixView A) {
    return solve(A, Matrix::identity(A.rows));
}
Complex determinant(ConstMatrixView A) {
    Lu lu = luDecompose(A);
    if (lu.singular)
        return 0.0;
    Complex d = static_cast<double>(lu.sign);
    for (std::size_t i = 0; i < A.rows; ++i)
        d *= lu.lu(i, i);
    return d;
}
} // namespace qlab::num
