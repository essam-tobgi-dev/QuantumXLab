#include "Hardware/IonChain.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace qlab::hw::ion {
using namespace qlab::num;
namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;
}

Result<std::vector<double>> equilibriumPositions(int n) {
    if (n < 1)
        return fail(ErrorCode::InvalidArgument, "equilibriumPositions: n < 1");
    std::vector<double> u(static_cast<std::size_t>(n));
    if (n == 1) {
        u[0] = 0.0;
        return u;
    }
    const double spacing = 2.018 * std::pow(double(n), -0.559);
    for (int i = 0; i < n; ++i)
        u[static_cast<std::size_t>(i)] = spacing * (i - 0.5 * (n - 1));
    const std::size_t N = u.size();
    for (int it = 0; it < 100; ++it) {
        std::vector<double> F(N, 0.0);
        RealMatrix J(N, N);
        for (std::size_t m = 0; m < N; ++m) {
            F[m] = u[m];
            J(m, m) = 1.0;
            for (std::size_t k = 0; k < N; ++k) {
                if (k == m)
                    continue;
                double d = u[m] - u[k];
                double s =
                    d > 0 ? 1.0 : -1.0; // sign(m−k) ordering equals sign of separation for sorted u
                F[m] -= s / (d * d);
                double dd = 2.0 * s / (d * d * d); // ∂/∂u_m of −s/d² = 2s/d³
                J(m, m) += dd;
                J(m, k) -= dd;
            }
        }
        // Solve J δ = −F (dense Gaussian elimination, N ≤ 64)
        std::vector<double> rhs(N);
        for (std::size_t i = 0; i < N; ++i)
            rhs[i] = -F[i];
        for (std::size_t c = 0; c < N; ++c) {
            std::size_t piv = c;
            for (std::size_t r = c + 1; r < N; ++r)
                if (std::abs(J(r, c)) > std::abs(J(piv, c)))
                    piv = r;
            if (piv != c) {
                for (std::size_t k = 0; k < N; ++k)
                    std::swap(J(c, k), J(piv, k));
                std::swap(rhs[c], rhs[piv]);
            }
            if (std::abs(J(c, c)) < 1e-14)
                return fail(ErrorCode::Internal, "equilibriumPositions: singular Jacobian");
            for (std::size_t r = c + 1; r < N; ++r) {
                double f = J(r, c) / J(c, c);
                for (std::size_t k = c; k < N; ++k)
                    J(r, k) -= f * J(c, k);
                rhs[r] -= f * rhs[c];
            }
        }
        std::vector<double> delta(N);
        for (std::size_t i = N; i-- > 0;) {
            double s = rhs[i];
            for (std::size_t k = i + 1; k < N; ++k)
                s -= J(i, k) * delta[k];
            delta[i] = s / J(i, i);
        }
        double maxStep = 0.0;
        for (std::size_t i = 0; i < N; ++i) {
            u[i] += delta[i];
            maxStep = std::max(maxStep, std::abs(delta[i]));
        }
        std::sort(u.begin(), u.end());
        if (maxStep < 1e-14)
            break;
    }
    return u;
}

RealMatrix axialHessian(const std::vector<double>& u) {
    const std::size_t N = u.size();
    RealMatrix A(N, N);
    for (std::size_t n = 0; n < N; ++n) {
        double diag = 1.0;
        for (std::size_t p = 0; p < N; ++p) {
            if (p == n)
                continue;
            double d = std::abs(u[n] - u[p]);
            diag += 2.0 / (d * d * d);
            A(n, p) = -2.0 / (d * d * d);
        }
        A(n, n) = diag;
    }
    return A;
}

units::Length lengthScale(units::Mass m, units::Frequency fz) {
    const double e = units::consts::e.v, eps0 = units::consts::eps0.v;
    const double wz = kTwoPi * fz.v;
    return units::Length(std::cbrt(e * e / (4.0 * std::numbers::pi * eps0 * m.v * wz * wz)));
}
double lambDicke(double deltaK, double b, units::Mass m, units::Frequency fMode, double cosTheta) {
    return deltaK * cosTheta * b *
           std::sqrt(units::consts::hbar.v / (2.0 * m.v * kTwoPi * fMode.v));
}
bool linearChainStable(int n, units::Frequency fz, units::Frequency fr) {
    return fr.v / fz.v > 0.73 * std::pow(double(n), 0.86);
}

namespace {
Result<std::pair<RealVector, RealMatrix>> realSymEig(const RealMatrix& A) {
    const std::size_t N = A.rows;
    Matrix H(N, N);
    for (std::size_t i = 0; i < N; ++i)
        for (std::size_t j = 0; j < N; ++j)
            H(i, j) = A(i, j);
    auto e = eigh(H, true);
    if (!e)
        return std::unexpected(e.error());
    RealMatrix V(N, N);
    for (std::size_t i = 0; i < N; ++i)
        for (std::size_t j = 0; j < N; ++j)
            V(i, j) = e->vectors(i, j).real();
    // Fix sign so the largest component of each column is positive (deterministic participation
    // vectors).
    for (std::size_t j = 0; j < N; ++j) {
        std::size_t im = 0;
        for (std::size_t i = 1; i < N; ++i)
            if (std::abs(V(i, j)) > std::abs(V(im, j)))
                im = i;
        if (V(im, j) < 0)
            for (std::size_t i = 0; i < N; ++i)
                V(i, j) = -V(i, j);
    }
    return std::pair{e->values, V};
}
} // namespace

Result<Modes> normalModes(const ChainParams& p) {
    auto u = equilibriumPositions(p.count);
    if (!u)
        return std::unexpected(u.error());
    Modes m;
    m.u = *u;
    m.lengthScale = lengthScale(p.mass, p.omegaZ);
    const std::size_t N = m.u.size();
    RealMatrix A = axialHessian(m.u);
    auto ax = realSymEig(A);
    if (!ax)
        return std::unexpected(ax.error());
    m.axialVectors = ax->second;
    for (std::size_t k = 0; k < N; ++k)
        m.axial.push_back(units::Frequency(p.omegaZ.v * std::sqrt(std::max(0.0, ax->first[k]))));
    // Radial: B = (ωr/ωz)² δ − ½(A − δ); eigenvalues descending → COM highest.
    RealMatrix B(N, N);
    const double ratio2 = (p.omegaR.v / p.omegaZ.v) * (p.omegaR.v / p.omegaZ.v);
    for (std::size_t i = 0; i < N; ++i)
        for (std::size_t j = 0; j < N; ++j)
            B(i, j) = (i == j ? ratio2 : 0.0) - 0.5 * (A(i, j) - (i == j ? 1.0 : 0.0));
    auto rd = realSymEig(B);
    if (!rd)
        return std::unexpected(rd.error());
    m.radialVectors = RealMatrix(N, N);
    for (std::size_t k = 0; k < N; ++k) {
        std::size_t src = N - 1 - k; // descending
        m.radial.push_back(units::Frequency(p.omegaZ.v * std::sqrt(std::max(0.0, rd->first[src]))));
        for (std::size_t i = 0; i < N; ++i)
            m.radialVectors(i, k) = rd->second(i, src);
    }
    const double deltaK = 2.0 * kTwoPi / p.ramanWavelength.v; // counter-propagating Raman pair
    m.etaAxial.assign(N, std::vector<double>(N));
    m.etaRadial.assign(N, std::vector<double>(N));
    for (std::size_t i = 0; i < N; ++i)
        for (std::size_t k = 0; k < N; ++k) {
            m.etaAxial[i][k] =
                lambDicke(deltaK, m.axialVectors(i, k), p.mass, m.axial[k], p.beamAngleCos);
            m.etaRadial[i][k] =
                lambDicke(deltaK, m.radialVectors(i, k), p.mass, m.radial[k], p.beamAngleCos);
        }
    return m;
}

SpinMotionOps spinMotionOperators(std::size_t nQubits, units::Frequency fMode, int fock) {
    SpinMotionOps ops;
    ops.nQubits = nQubits;
    ops.fock = fock;
    const std::size_t F = static_cast<std::size_t>(fock), Q = ipow(2, nQubits);
    SparseMatrix a(F, F), n(F, F);
    std::vector<Triplet> ta, tn;
    for (std::size_t k = 1; k < F; ++k)
        ta.push_back({k - 1, k, std::sqrt(double(k))});
    for (std::size_t k = 0; k < F; ++k)
        tn.push_back({k, k, double(k)});
    a = SparseMatrix::fromTriplets(F, F, ta);
    n = SparseMatrix::fromTriplets(F, F, tn);
    SparseMatrix IQ = SparseMatrix::identity(Q), IF = SparseMatrix::identity(F);
    ops.a = kron(a, IQ);
    ops.adag = ops.a.adjoint(); // mode is the high subsystem
    ops.H0 = kron(n, IQ);
    ops.H0.scale(kTwoPi * fMode.v);
    Matrix X = pauli::X.toMatrix(), Y = pauli::Y.toMatrix(), Z = pauli::Z.toMatrix();
    for (std::size_t q = 0; q < nQubits; ++q) {
        std::size_t t[1] = {q};
        ops.sigmaX.push_back(kron(IF, SparseMatrix::fromDense(embed(X, t, nQubits))));
        ops.sigmaY.push_back(kron(IF, SparseMatrix::fromDense(embed(Y, t, nQubits))));
        ops.sigmaZ.push_back(kron(IF, SparseMatrix::fromDense(embed(Z, t, nQubits))));
    }
    return ops;
}
} // namespace qlab::hw::ion
