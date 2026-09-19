#include "Numerics/Expm.hpp"
#include "Numerics/Eigen.hpp"
#include "Numerics/Solve.hpp"
#include <cmath>
namespace qlab::num {
namespace {
// Padé(13) coefficients b_0..b_13 (Higham 2005).
constexpr double kB[14] = {64764752532480000.,
                           32382376266240000.,
                           7771770303897600.,
                           1187353796428800.,
                           129060195264000.,
                           10559470521600.,
                           670442572800.,
                           33522128640.,
                           1323241920.,
                           40840800.,
                           960960.,
                           16380.,
                           182.,
                           1.};
constexpr double kTheta13 = 5.371920351148152;
} // namespace

Result<Matrix> expm(ConstMatrixView Ain) {
    if (!Ain.square())
        return fail(ErrorCode::InvalidArgument, "expm: matrix must be square");
    std::size_t n = Ain.rows;
    if (n == 0)
        return Matrix();
    double nrm = norm1(Ain);
    int s = 0;
    if (nrm > kTheta13)
        s = static_cast<int>(std::ceil(std::log2(nrm / kTheta13)));
    Matrix A(Ain);
    if (s > 0)
        A *= Complex(std::ldexp(1.0, -s), 0.0);
    Matrix I = Matrix::identity(n);
    Matrix A2 = matmul(A, A), A4 = matmul(A2, A2), A6 = matmul(A4, A2);
    auto lin = [&](double c0, double c2, double c4, double c6) {
        Matrix r = scale(I, c0);
        r += scale(A2, c2);
        r += scale(A4, c4);
        r += scale(A6, c6);
        return r;
    };
    Matrix inner = lin(kB[7], kB[9], kB[11], kB[13]);
    Matrix U = matmul(A6, inner);
    U += lin(kB[1], kB[3], kB[5], 0.0);
    U = matmul(A, U);
    Matrix innerV = lin(kB[6], kB[8], kB[10], kB[12]);
    Matrix V = matmul(A6, innerV);
    V += lin(kB[0], kB[2], kB[4], 0.0);
    Matrix P = add(V, U), Q = sub(V, U);
    auto R = solve(Q, P);
    if (!R)
        return std::unexpected(R.error());
    Matrix E = std::move(*R);
    for (int k = 0; k < s; ++k)
        E = matmul(E, E);
    return E;
}
Result<Matrix> expmHermitianGeneral(ConstMatrixView H, Complex sc) {
    auto e = eigh(H);
    if (!e)
        return std::unexpected(e.error());
    std::size_t n = H.rows;
    Matrix D(n, n);
    for (std::size_t i = 0; i < n; ++i)
        D(i, i) = std::exp(sc * e->values[i]);
    return matmul(matmul(e->vectors, D), adjoint(e->vectors));
}
Result<Matrix> expmHermitian(ConstMatrixView H, double t) {
    return expmHermitianGeneral(H, Complex(0.0, -t));
}

Mat2 expPauli(double nx, double ny, double nz, double theta) {
    double L = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (L > 0) {
        nx /= L;
        ny /= L;
        nz /= L;
    }
    double c = std::cos(theta / 2), s = std::sin(theta / 2);
    Complex mi(0, -1);
    Mat2 r;
    r(0, 0) = c + mi * s * nz;
    r(0, 1) = mi * s * Complex(nx, -ny);
    r(1, 0) = mi * s * Complex(nx, ny);
    r(1, 1) = c - mi * s * nz;
    return r;
}
Mat4 expPauliPair(const Mat2& P, const Mat2& Q, double theta) {
    double c = std::cos(theta / 2), s = std::sin(theta / 2);
    Mat4 r = Mat4::identity();
    for (auto& v : r.a)
        v *= c;
    // P⊗Q with P on the high (second) qubit and Q on the low (first) — callers use pauli pairs
    // symmetrically.
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < 4; ++j)
            r(i, j) += Complex(0, -s) * P(i >> 1, j >> 1) * Q(i & 1, j & 1);
    return r;
}

Vector
expmTimesVector(std::size_t dim,
                const std::function<void(std::span<const Complex>, std::span<Complex>)>& applyA,
                std::span<const Complex> v, double tolAbs, std::size_t maxKrylov) {
    double beta = std::sqrt(norm2Squared(v));
    Vector result(dim, Complex{});
    if (beta == 0.0 || dim == 0)
        return result;
    std::size_t m = std::min(maxKrylov, dim);
    std::vector<Vector> V;
    V.reserve(m + 1);
    V.emplace_back(v.begin(), v.end());
    for (auto& x : V[0])
        x /= beta;
    Matrix Hm(m + 1, m + 1);
    Vector w(dim);
    std::size_t k = 0;
    bool breakdown = false;
    for (; k < m; ++k) {
        applyA(V[k], w);
        for (std::size_t j = 0; j <= k; ++j) { // modified Gram–Schmidt (twice for stability)
            Complex h = dot(V[j], w);
            for (std::size_t i = 0; i < dim; ++i)
                w[i] -= h * V[j][i];
            Hm(j, k) += h;
        }
        for (std::size_t j = 0; j <= k; ++j) {
            Complex h = dot(V[j], w);
            for (std::size_t i = 0; i < dim; ++i)
                w[i] -= h * V[j][i];
            Hm(j, k) += h;
        }
        double hn = std::sqrt(norm2Squared(w));
        Hm(k + 1, k) = hn;
        if (hn < tolAbs) {
            breakdown = true;
            ++k;
            break;
        }
        Vector next(w);
        for (auto& x : next)
            x /= hn;
        V.push_back(std::move(next));
    }
    std::size_t kk = breakdown ? k : m;
    Matrix Hk(kk, kk);
    for (std::size_t i = 0; i < kk; ++i)
        for (std::size_t j = 0; j < kk; ++j)
            Hk(i, j) = Hm(i, j);
    auto E = expm(Hk);
    if (!E)
        return result;
    for (std::size_t j = 0; j < kk; ++j) {
        Complex c = beta * (*E)(j, 0);
        for (std::size_t i = 0; i < dim; ++i)
            result[i] += c * V[j][i];
    }
    return result;
}
Vector expmTimesVector(ConstMatrixView A, std::span<const Complex> v, double tolAbs) {
    return expmTimesVector(
        A.rows, [&](std::span<const Complex> x, std::span<Complex> y) { matvecInto(A, x, y); }, v,
        tolAbs);
}
} // namespace qlab::num
