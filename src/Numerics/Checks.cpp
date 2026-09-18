#include "Numerics/Checks.hpp"
#include "Numerics/Eigen.hpp"
#include <cmath>
namespace qlab::num {

bool isUnitary(ConstMatrixView U, double tolAbs) {
    if (!U.square() || U.rows == 0) return false;
    Matrix prod = matmul(adjoint(U), U);
    for (std::size_t i = 0; i < U.rows; ++i)
        for (std::size_t j = 0; j < U.cols; ++j) {
            Complex expect = (i == j) ? Complex(1, 0) : Complex(0, 0);
            if (std::abs(prod(i, j) - expect) > tolAbs) return false;
        }
    return true;
}
bool isHermitian(ConstMatrixView H, double tolAbs) {
    if (!H.square()) return false;
    for (std::size_t i = 0; i < H.rows; ++i)
        for (std::size_t j = i; j < H.cols; ++j)
            if (std::abs(H(i, j) - std::conj(H(j, i))) > tolAbs) return false;
    return true;
}
bool isPositiveSemidefinite(ConstMatrixView rho, double tolAbs) {
    if (!isHermitian(rho, 1e-9)) return false;
    auto e = eigh(rho);
    if (!e) return false;
    return e->values.empty() || e->values.front() >= -tolAbs;
}
bool isTracePreserving(std::span<const Matrix> kraus, double tolAbs) {
    if (kraus.empty()) return false;
    std::size_t d = kraus.front().cols;
    Matrix sum(d, d);
    for (auto& K : kraus) {
        if (K.cols != d) return false;
        sum += matmul(adjoint(K), K);
    }
    for (std::size_t i = 0; i < d; ++i)
        for (std::size_t j = 0; j < d; ++j) {
            Complex expect = (i == j) ? Complex(1, 0) : Complex(0, 0);
            if (std::abs(sum(i, j) - expect) > tolAbs) return false;
        }
    return true;
}
bool isDensityMatrix(ConstMatrixView rho, double tolAbs) {
    return isHermitian(rho, 1e-9) && std::abs(trace(rho) - Complex(1, 0)) <= tolAbs && isPositiveSemidefinite(rho, 1e-8);
}
bool isNormalized(std::span<const Complex> psi, double tolAbs) {
    return std::abs(norm2Squared(psi) - 1.0) <= tolAbs;
}
double traceDistance(ConstMatrixView rho, ConstMatrixView sigma) {
    Matrix diff = sub(rho, sigma);
    auto e = eigh(diff);
    if (!e) return 0.0;
    double s = 0;
    for (double v : e->values) s += std::abs(v);
    return 0.5 * s;
}
double fidelity(ConstMatrixView rho, ConstMatrixView sigma) {
    auto sr = sqrtm(rho);
    if (!sr) return 0.0;
    Matrix m = matmul(matmul(*sr, sigma), *sr);
    // m is PSD (Hermitian up to rounding); symmetrize.
    Matrix h = scale(add(m, adjoint(m)), 0.5);
    auto e = eigh(h);
    if (!e) return 0.0;
    double s = 0;
    for (double v : e->values) s += std::sqrt(std::max(0.0, v));
    return s * s;
}
double fidelity(std::span<const Complex> psi, std::span<const Complex> phi) {
    return std::norm(dot(psi, phi));
}
double fidelity(std::span<const Complex> psi, ConstMatrixView rho) {
    Vector v = matvec(rho, psi);
    return std::real(dot(psi, v));
}
double purity(ConstMatrixView rho) { return std::real(trace(matmul(rho, rho))); }
double hellingerFidelity(std::span<const double> p, std::span<const double> q) {
    double s = 0;
    std::size_t n = std::min(p.size(), q.size());
    for (std::size_t i = 0; i < n; ++i) s += std::sqrt(std::max(0.0, p[i]) * std::max(0.0, q[i]));
    return s * s;
}
double vonNeumannEntropy(ConstMatrixView rho) {
    auto e = eigh(rho);
    if (!e) return 0.0;
    double S = 0;
    for (double v : e->values)
        if (v > 1e-15) S -= v * std::log2(v);
    return S;
}
double globalPhaseBetween(ConstMatrixView U, ConstMatrixView V) {
    Complex t = trace(matmul(adjoint(V), U)); // U ≈ e^{iφ}V → Tr(V†U) = e^{iφ} d
    if (std::abs(t) > 1e-6) return std::arg(t);
    for (std::size_t j = 0; j < U.cols; ++j)
        for (std::size_t i = 0; i < U.rows; ++i)
            if (std::abs(V(i, j)) > 1e-9 && std::abs(U(i, j)) > 1e-9) return std::arg(U(i, j) / V(i, j));
    return 0.0;
}
bool equalUpToGlobalPhase(ConstMatrixView U, ConstMatrixView V, double tolAbs) {
    if (!sameShape(U, V)) return false;
    double phi = globalPhaseBetween(U, V);
    Complex ph = std::polar(1.0, phi);
    for (std::size_t i = 0; i < U.rows; ++i)
        for (std::size_t j = 0; j < U.cols; ++j)
            if (std::abs(U(i, j) - ph * V(i, j)) > tolAbs) return false;
    return true;
}
double averageGateFidelity(ConstMatrixView U, ConstMatrixView V) {
    double d = static_cast<double>(U.rows);
    Complex t = trace(matmul(adjoint(U), V));
    return (std::norm(t) + d) / (d * (d + 1.0));
}
} // namespace qlab::num
