#include "Hardware/Transmon.hpp"
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::hw::transmon {
using namespace qlab::num;
namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;
}

Result<Spectrum> chargeBasisSpectrum(units::Frequency EJ, units::Frequency EC, double ng, int d,
                                     int N) {
    if (N < 5 || d < 2 || d > 2 * N)
        return fail(ErrorCode::InvalidArgument, "chargeBasisSpectrum: bad truncation");
    const std::size_t dim = static_cast<std::size_t>(2 * N + 1);
    Matrix H(dim, dim);
    for (int n = -N; n <= N; ++n) {
        std::size_t i = static_cast<std::size_t>(n + N);
        H(i, i) = 4.0 * EC.v * (n - ng) * (n - ng);
        if (i + 1 < dim) {
            H(i, i + 1) = -0.5 * EJ.v;
            H(i + 1, i) = -0.5 * EJ.v;
        }
    }
    auto eig = eigh(H);
    if (!eig)
        return std::unexpected(eig.error());
    Spectrum s;
    s.vectors = Matrix(dim, static_cast<std::size_t>(d));
    for (int k = 0; k < d; ++k) {
        s.energiesJ.push_back(eig->values[static_cast<std::size_t>(k)] * units::consts::h.v);
        for (std::size_t i = 0; i < dim; ++i)
            s.vectors(i, static_cast<std::size_t>(k)) =
                eig->vectors(i, static_cast<std::size_t>(k));
    }
    s.f01 = units::Frequency(eig->values[1] - eig->values[0]);
    s.anharmonicity =
        units::Frequency(d >= 3 ? eig->values[2] - 2.0 * eig->values[1] + eig->values[0] : 0.0);
    auto nme = [&](int a, int b) {
        Complex acc{};
        for (int n = -N; n <= N; ++n) {
            std::size_t i = static_cast<std::size_t>(n + N);
            acc += std::conj(eig->vectors(i, static_cast<std::size_t>(a))) * double(n) *
                   eig->vectors(i, static_cast<std::size_t>(b));
        }
        return std::abs(acc);
    };
    s.nMatrixElement01 = nme(0, 1);
    if (d >= 3)
        s.nMatrixElement12 = nme(1, 2);
    return s;
}

Result<units::Frequency> chargeDispersion01(units::Frequency EJ, units::Frequency EC, int N) {
    auto a = chargeBasisSpectrum(EJ, EC, 0.0, 2, N);
    auto b = chargeBasisSpectrum(EJ, EC, 0.5, 2, N);
    if (!a)
        return std::unexpected(a.error());
    if (!b)
        return std::unexpected(b.error());
    return units::Frequency(std::abs(a->f01.v - b->f01.v));
}

units::Frequency approxF01(units::Frequency EJ, units::Frequency EC) {
    return units::Frequency(std::sqrt(8.0 * EJ.v * EC.v) - EC.v);
}
units::Frequency approxAlpha(units::Frequency EC) {
    return units::Frequency(-EC.v);
}

Result<EjEc> fromTargets(units::Frequency f01, units::Frequency alpha, int N) {
    if (f01.v <= 0.0 || alpha.v >= 0.0)
        return fail(ErrorCode::InvalidArgument, "fromTargets: f01 must be > 0 and α < 0");
    double EC = -alpha.v, EJ = (f01.v + EC) * (f01.v + EC) / (8.0 * EC);
    EjEc r;
    for (int it = 0; it < 60; ++it) {
        auto s0 = chargeBasisSpectrum(units::Frequency(EJ), units::Frequency(EC), 0.0, 3, N);
        if (!s0)
            return std::unexpected(s0.error());
        double F1 = s0->f01.v - f01.v, F2 = s0->anharmonicity.v - alpha.v;
        r.iterations = it + 1;
        if (std::abs(F1) < 1e-10 * f01.v && std::abs(F2) < 1e-10 * std::abs(alpha.v)) {
            r.EJ = units::Frequency(EJ);
            r.EC = units::Frequency(EC);
            return r;
        }
        const double hJ = EJ * 1e-6, hC = EC * 1e-6;
        auto sJ = chargeBasisSpectrum(units::Frequency(EJ + hJ), units::Frequency(EC), 0.0, 3, N);
        auto sC = chargeBasisSpectrum(units::Frequency(EJ), units::Frequency(EC + hC), 0.0, 3, N);
        if (!sJ || !sC)
            return fail(ErrorCode::Internal, "fromTargets: spectrum failed");
        double J11 = (sJ->f01.v - s0->f01.v) / hJ, J12 = (sC->f01.v - s0->f01.v) / hC;
        double J21 = (sJ->anharmonicity.v - s0->anharmonicity.v) / hJ,
               J22 = (sC->anharmonicity.v - s0->anharmonicity.v) / hC;
        double det = J11 * J22 - J12 * J21;
        if (std::abs(det) < 1e-30)
            return fail(ErrorCode::Internal, "fromTargets: singular Jacobian");
        double dJ = (-F1 * J22 + F2 * J12) / det, dC = (-J11 * F2 + J21 * F1) / det;
        EJ += dJ;
        EC += dC;
        if (EJ <= 0 || EC <= 0)
            return fail(ErrorCode::InvalidArgument,
                        "fromTargets: diverged to non-physical energies");
    }
    return fail(
        ErrorCode::Internal,
        std::format("fromTargets: no convergence for f01={} Hz, alpha={} Hz", f01.v, alpha.v));
}

units::Frequency josephsonEnergy(units::Frequency EJsum, double phi, double d) {
    const double x = std::numbers::pi * phi;
    const double c = std::cos(x), t = std::tan(x);
    return units::Frequency(EJsum.v * std::abs(c) * std::sqrt(1.0 + d * d * t * t));
}

KerrOps kerrOperators(units::Frequency f01, units::Frequency alpha, int d) {
    const std::size_t D = static_cast<std::size_t>(d);
    KerrOps k{Matrix(D, D), Matrix(D, D), Matrix(D, D), Matrix(D, D)};
    for (std::size_t n = 1; n < D; ++n) {
        k.a(n - 1, n) = std::sqrt(double(n));
        k.adag(n, n - 1) = std::sqrt(double(n));
    }
    const double w = kTwoPi * f01.v, al = kTwoPi * alpha.v;
    for (std::size_t n = 0; n < D; ++n) {
        k.n(n, n) = double(n);
        k.H(n, n) = w * double(n) +
                    0.5 * al * double(n) * double(n - 1 + (n == 0 ? 1 : 0)) * (n == 0 ? 0.0 : 1.0);
    }
    // (α/2) a†a†aa has diagonal n(n−1)
    for (std::size_t n = 0; n < D; ++n)
        k.H(n, n) = w * double(n) + 0.5 * al * double(n) * double(n >= 1 ? n - 1 : 0);
    return k;
}

units::Frequency staticZZ(units::Frequency g, units::Frequency f1, units::Frequency f2,
                          units::Frequency a1, units::Frequency a2) {
    const double D = f1.v - f2.v;
    const double den = (D + a1.v) * (D - a2.v);
    if (std::abs(den) < 1.0)
        return units::Frequency(INFINITY);
    return units::Frequency(2.0 * g.v * g.v * (a1.v + a2.v) / den);
}
units::Frequency dispersiveShift(units::Frequency g, units::Frequency fq, units::Frequency fr,
                                 units::Frequency alpha) {
    const double D = fq.v - fr.v;
    if (std::abs(D) < 1.0 || std::abs(D + alpha.v) < 1.0)
        return units::Frequency(INFINITY);
    return units::Frequency(g.v * g.v / D * alpha.v / (D + alpha.v));
}
double criticalPhotonNumber(units::Frequency g, units::Frequency fq, units::Frequency fr) {
    const double D = fq.v - fr.v;
    return D * D / (4.0 * g.v * g.v);
}
double purcellRate(units::Frequency kappa, units::Frequency g, units::Frequency fq,
                   units::Frequency fr) {
    const double D = fq.v - fr.v;
    return kTwoPi * kappa.v * g.v * g.v / (D * D);
}
units::Frequency effectiveCoupling(units::Frequency g12, units::Frequency g1c, units::Frequency g2c,
                                   units::Frequency f1, units::Frequency f2, units::Frequency fc) {
    const double D1 = f1.v - fc.v, D2 = f2.v - fc.v;
    return units::Frequency(g12.v + 0.5 * g1c.v * g2c.v * (1.0 / D1 + 1.0 / D2));
}
Result<units::Frequency> couplerOffPoint(units::Frequency g12, units::Frequency g1c,
                                         units::Frequency g2c, units::Frequency f1,
                                         units::Frequency f2) {
    double lo = std::max(f1.v, f2.v) + 50e6, hi = 12e9;
    auto f = [&](double fc) {
        return effectiveCoupling(g12, g1c, g2c, f1, f2, units::Frequency(fc)).v;
    };
    double flo = f(lo), fhi = f(hi);
    if (flo * fhi > 0.0)
        return fail(ErrorCode::NotFound, "no coupler off point above the qubits");
    for (int i = 0; i < 200; ++i) {
        double mid = 0.5 * (lo + hi), fm = f(mid);
        if (fm * flo <= 0.0) {
            hi = mid;
            fhi = fm;
        } else {
            lo = mid;
            flo = fm;
        }
        if (hi - lo < 1.0)
            break;
    }
    return units::Frequency(0.5 * (lo + hi));
}
units::Frequency couplingFromCapacitance(units::Capacitance Cg, units::Capacitance C1,
                                         units::Capacitance C2, units::Frequency f1,
                                         units::Frequency f2) {
    return units::Frequency(0.5 * Cg.v / std::sqrt(C1.v * C2.v) * std::sqrt(f1.v * f2.v));
}
} // namespace qlab::hw::transmon
