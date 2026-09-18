// Spec 14 §5.5, T02 §6 — Cartan decomposition through the magic basis. In that basis
// SU(2)⊗SU(2) is SO(4) and the canonical gate is diagonal, so with M = Q†UQ the symmetric unitary
// MᵀM = P D Pᵀ has a REAL orthogonal eigenbasis P; then M = K₁ D^{1/2} Pᵀ with K₁ = M P D^{−1/2}
// real orthogonal, and U = (Q K₁ Q†)(Q D^{1/2} Q†)(Q Pᵀ Q†) = (A₁⊗A₀) · N(a,b,c) · (B₁⊗B₀).
#include "Compiler/Kak.hpp"
#include "Compiler/Types.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Matrix.hpp"
#include "Numerics/Solve.hpp"
#include "Numerics/Tensor.hpp"
#include <array>
#include <cmath>
#include <numbers>

namespace qlab::compiler {
namespace {
using C = num::Complex;
constexpr double kPi = std::numbers::pi;

// Columns: Φ⁺, iΨ⁺, Ψ⁻, iΦ⁻ (Bell states). Their canonical-gate phases are
//   θ₀ = a − b + c,  θ₁ = a + b − c,  θ₂ = −a − b − c,  θ₃ = −a + b + c.
const num::Matrix& magic() {
    static const num::Matrix q = [] {
        const double r = 1.0 / std::sqrt(2.0);
        const C i(0, 1);
        return num::scale(num::Matrix::fromRows({{1, 0, 0, i}, {0, i, 1, 0}, {0, i, -1, 0}, {1, 0, 0, -i}}), r);
    }();
    return q;
}

// Cyclic Jacobi for a real symmetric 4×4: a = V diag(w) Vᵀ with V orthogonal (columns).
void jacobi4(std::array<std::array<double, 4>, 4> a, std::array<std::array<double, 4>, 4>& v) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) v[i][j] = i == j ? 1.0 : 0.0;
    for (int sweep = 0; sweep < 64; ++sweep) {
        double off = 0.0;
        for (int p = 0; p < 4; ++p)
            for (int q = p + 1; q < 4; ++q) off += a[p][q] * a[p][q];
        if (off < 1e-30) break;
        for (int p = 0; p < 4; ++p)
            for (int q = p + 1; q < 4; ++q) {
                if (std::abs(a[p][q]) < 1e-300) continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < 4; ++k) {   // A ← A·J
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 4; ++k) {   // A ← Jᵀ·A
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 4; ++k) {
                    const double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
    }
}

// m = hi ⊗ lo with det(lo) = 1; false when m is not a tensor product (to 1e-8).
bool factor(const num::Matrix& m, num::Matrix& hi, num::Matrix& lo) {
    std::size_t bi = 0, bj = 0;
    double best = -1.0;
    for (std::size_t i = 0; i < 2; ++i)
        for (std::size_t j = 0; j < 2; ++j) {
            double n = 0.0;
            for (std::size_t k = 0; k < 2; ++k)
                for (std::size_t l = 0; l < 2; ++l) n += std::norm(m(2 * i + k, 2 * j + l));
            if (n > best) { best = n; bi = i; bj = j; }
        }
    lo = num::Matrix(2, 2);
    for (std::size_t k = 0; k < 2; ++k)
        for (std::size_t l = 0; l < 2; ++l) lo(k, l) = m(2 * bi + k, 2 * bj + l);
    const C det = lo(0, 0) * lo(1, 1) - lo(0, 1) * lo(1, 0);
    if (std::abs(det) < 1e-12) return false;
    lo = num::scale(lo, 1.0 / std::sqrt(det));
    hi = num::Matrix(2, 2);
    for (std::size_t i = 0; i < 2; ++i)
        for (std::size_t j = 0; j < 2; ++j) {
            C tr{};
            for (std::size_t k = 0; k < 2; ++k)
                for (std::size_t l = 0; l < 2; ++l) tr += std::conj(lo(k, l)) * m(2 * i + k, 2 * j + l);
            hi(i, j) = tr / 2.0;
        }
    return num::approxEqual(num::kron(hi, lo), m, 1e-8);
}
} // namespace

num::Matrix canonicalGate(double a, double b, double c) {
    const double theta[4] = {a - b + c, a + b - c, -a - b - c, -a + b + c};
    num::Matrix d(4, 4);
    for (std::size_t j = 0; j < 4; ++j) d(j, j) = std::polar(1.0, theta[j]);
    return num::matmul(num::matmul(magic(), d), num::adjoint(magic().view()));
}

num::Matrix KakDecomposition::reconstruct() const {
    const num::Matrix left = num::kron(after1, after0), right = num::kron(before1, before0);
    return num::scale(num::matmul(num::matmul(left, canonicalGate(a, b, c)), right), std::polar(1.0, phase));
}

std::uint32_t KakDecomposition::cxCount(double eps) const {
    int nonzero = 0;
    bool quarter = false;
    for (double v : {a, b, c})
        if (std::abs(v) > eps) { ++nonzero; quarter = std::abs(std::abs(v) - kPi / 4) < eps; }
    if (nonzero == 0) return 0;
    if (nonzero == 1 && quarter) return 1;
    return nonzero <= 2 ? 2 : 3;
}

Result<KakDecomposition> kakDecompose(num::ConstMatrixView u) {
    if (u.rows != 4 || u.cols != 4 || !num::isUnitary(u, 1e-9)) return fail(ErrorCode::InvalidArgument, "kakDecompose takes a 4×4 unitary");
    KakDecomposition k;
    k.phase = std::arg(num::determinant(u)) / 4.0;
    const num::Matrix su = num::scale(u, std::polar(1.0, -k.phase));   // det = 1
    const num::Matrix m = num::matmul(num::matmul(num::adjoint(magic().view()), su), magic());
    const num::Matrix m2 = num::matmul(num::transpose(m.view()), m);

    // Re(MᵀM) and Im(MᵀM) commute: a generic real combination has their common eigenbasis.
    num::Matrix p(4, 4);
    bool diagonalised = false;
    for (double t : {0.37, 1.23, 2.11, 0.79, 2.77, 1.71}) {
        std::array<std::array<double, 4>, 4> sym{}, v{};
        for (std::size_t i = 0; i < 4; ++i)
            for (std::size_t j = 0; j < 4; ++j)
                sym[i][j] = 0.5 * (std::cos(t) * (m2(i, j).real() + m2(j, i).real()) + std::sin(t) * (m2(i, j).imag() + m2(j, i).imag()));
        jacobi4(sym, v);
        for (std::size_t i = 0; i < 4; ++i)
            for (std::size_t j = 0; j < 4; ++j) p(i, j) = v[i][j];
        const num::Matrix d = num::matmul(num::matmul(num::transpose(p.view()), m2), p);
        double off = 0.0;
        for (std::size_t i = 0; i < 4; ++i)
            for (std::size_t j = 0; j < 4; ++j)
                if (i != j) off = std::max(off, std::abs(d(i, j)));
        if (off < 1e-9) { diagonalised = true; break; }
    }
    if (!diagonalised) return fail(ErrorCode::Internal, "kakDecompose: no common real eigenbasis found");
    if (num::determinant(p.view()).real() < 0)
        for (std::size_t i = 0; i < 4; ++i) p(i, 0) = -p(i, 0);   // P ∈ SO(4)

    const num::Matrix d = num::matmul(num::matmul(num::transpose(p.view()), m2), p);
    double theta[4], sum = 0.0;
    for (std::size_t j = 0; j < 4; ++j) { theta[j] = std::arg(d(j, j)) / 2.0; sum += theta[j]; }
    if (std::cos(sum) < 0.0) theta[0] += kPi;   // det D^{1/2} = +1, so that K₁ ∈ SO(4)
    num::Matrix rootInverse(4, 4);
    for (std::size_t j = 0; j < 4; ++j) rootInverse(j, j) = std::polar(1.0, -theta[j]);
    num::Matrix k1 = num::matmul(num::matmul(m, p), rootInverse);
    for (auto& z : k1.data) z = C(z.real(), 0.0);   // real up to rounding (unitary and complex orthogonal)

    const num::Matrix qDagger = num::adjoint(magic().view());
    const num::Matrix left = num::matmul(num::matmul(magic(), k1), qDagger);
    const num::Matrix right = num::matmul(num::matmul(magic(), num::transpose(p.view())), qDagger);
    if (!factor(left, k.after1, k.after0) || !factor(right, k.before1, k.before0))
        return fail(ErrorCode::Internal, "kakDecompose: local factors are not tensor products");

    k.a = (theta[0] + theta[1]) / 2.0;
    k.b = (theta[1] + theta[3]) / 2.0;
    k.c = (theta[0] + theta[3]) / 2.0;
    // exp(i(v + mπ/2)PP) = exp(i v PP) · (i P⊗P)^m: the Pauli factors are local and join A.
    struct Axis { double* value; const char* pauli; };
    for (const Axis& axis : {Axis{&k.a, "x"}, Axis{&k.b, "y"}, Axis{&k.c, "z"}}) {
        double turns = std::nearbyint(*axis.value / (kPi / 2));
        double v = *axis.value - turns * kPi / 2;
        if (v <= -kPi / 4 + 1e-12) { v += kPi / 2; turns -= 1.0; }
        *axis.value = v;
        k.phase += turns * kPi / 2;
        if (std::fmod(std::abs(turns), 2.0) == 1.0) {   // P⊗P = −(iP)⊗(iP), and det(iP) = 1 keeps A in SU(2)⊗SU(2)
            const num::Matrix pauli = num::scale(*ir::gates::matrix(axis.pauli, {}), C(0, 1));
            k.after0 = num::matmul(k.after0, pauli);
            k.after1 = num::matmul(k.after1, pauli);
            k.phase += kPi;
        }
    }
    if (!num::approxEqual(k.reconstruct(), num::Matrix(u), 1e-8)) return fail(ErrorCode::Internal, "kakDecompose: reconstruction failed");
    return k;
}

} // namespace qlab::compiler
