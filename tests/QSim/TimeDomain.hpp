#pragma once
// Time-domain models shared by the Lindblad and trajectories tests (spec 07 §5, T04, T05 §7):
// H in rad/s, t in s, ħ = 1, little-endian sites.
#include "Circuits.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Eigen.hpp"
#include <catch2/catch_test_macros.hpp>
#include <format>
#include <functional>
#include <numbers>

namespace qtest {

inline constexpr double kTwoPi = 2.0 * std::numbers::pi;

inline SystemModel bareModel(std::uint32_t sites, std::uint32_t levels) {
    SystemModel m;
    m.siteDims.assign(sites, levels);
    m.h0 = Matrix(m.dimension(), m.dimension());
    m.frameFrequenciesHz.assign(sites, 0.0);
    return m;
}

// H += Re Ω(t)·(a + a†) + Im Ω(t)·i(a† − a): Ω(t) = Ω_Rabi/2 is a resonant x drive (T05 (7.1)).
inline DriveTerm ladderDrive(const SystemModel& m, std::uint32_t site,
                             std::function<Complex(double)> envelope) {
    auto [a, b] = driveFromLadder(m.siteDims, site);
    return DriveTerm{std::format("d[{}]", site), std::move(a), std::move(b), std::move(envelope)};
}

// Energy relaxation √γ a (T04 §3.1).
inline CollapseOp decay(const SystemModel& m, std::uint32_t site, double gamma) {
    Matrix a = ladderAnnihilate(m.siteDims, site);
    a *= std::sqrt(gamma);
    return {std::format("T1 q{}", site), std::move(a)};
}

// Pure dephasing √(2γφ) a†a (spec 07 §5), which on a qubit equals √(γφ/2) Z: ρ01 decays at γφ (T04
// (4.1)).
inline CollapseOp dephasing(const SystemModel& m, std::uint32_t site, double gammaPhi) {
    Matrix n = numberOperator(m.siteDims, site);
    n *= std::sqrt(2.0 * gammaPhi);
    return {std::format("Tphi q{}", site), std::move(n)};
}

// Duffing anharmonicity (α/2) a†a†aa of one site: α on |2⟩, 3α on |3⟩, … (spec 09 §5.2).
inline Matrix anharmonicity(const SystemModel& m, std::uint32_t site, double alpha) {
    Matrix h(m.dimension(), m.dimension());
    for (std::uint32_t lv = 2; lv < m.siteDims[site]; ++lv) {
        Matrix p = levelProjector(m.siteDims, site, lv);
        p *= 0.5 * alpha * lv * (lv - 1);
        h += p;
    }
    return h;
}

// Gaussian of duration tg and width σ = tg/4 whose area over [0, tg] is `area`: Ω(t) and dΩ/dt.
struct GaussianPulse {
    double tg = 0, sigma = 0, amp = 0;
    GaussianPulse(double duration, double area) : tg(duration), sigma(duration / 4.0) {
        amp = area / (sigma * std::sqrt(2.0 * std::numbers::pi) *
                      std::erf(tg / (2.0 * std::sqrt(2.0) * sigma)));
    }
    double value(double t) const {
        if (t < 0 || t > tg)
            return 0.0;
        const double u = (t - 0.5 * tg) / sigma;
        return amp * std::exp(-0.5 * u * u);
    }
    double slope(double t) const { return -(t - 0.5 * tg) / (sigma * sigma) * value(t); }
};

inline double minEigenvalue(const Matrix& rho) {
    auto e = num::eigh(rho);
    REQUIRE(e.has_value());
    return e->values.front();
}

} // namespace qtest
