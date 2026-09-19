// Spec 07 §5, spec 25 §3.5, T05 §7 — multi-level transmons on the Lindblad backend: leakage versus
// pulse length, DRAG, invariants of driven dissipative evolution across integrators, the 3^5 cap.
#include "TimeDomain.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qtest;
using Catch::Approx;

namespace {
constexpr double kAlpha = -kTwoPi * 300e6; // α/2π = −300 MHz

// One d = 3 transmon in the drive frame (δ = 0) under a π pulse Ω(t) = Ωx + iΩy, with Ωy = β
// dΩx/dt.
SystemModel transmonUnder(std::function<double(double)> omegaX,
                          std::function<double(double)> slopeX, double beta) {
    SystemModel m = bareModel(1, 3);
    m.h0 = anharmonicity(m, 0, kAlpha);
    m.drives.push_back(
        ladderDrive(m, 0, [=](double t) { return 0.5 * Complex(omegaX(t), beta * slopeX(t)); }));
    return m;
}

LindbladBackend evolved(SystemModel m, double duration, LindbladSettings s = {}) {
    LindbladBackend lb;
    lb.setSettings(s);
    REQUIRE(lb.setModel(std::move(m)).has_value());
    REQUIRE(lb.evolve(duration).has_value());
    return lb;
}
} // namespace

TEST_CASE("Lindblad: 3-level transmon leakage shrinks with a longer Gaussian pi pulse") {
    double previous = 1.0;
    for (double tg : {4e-9, 8e-9, 16e-9}) {
        INFO("tg = " << tg * 1e9 << " ns");
        const GaussianPulse g(tg, std::numbers::pi);
        auto lb = evolved(transmonUnder([g](double t) { return g.value(t); },
                                        [g](double t) { return g.slope(t); }, 0.0),
                          tg);
        const double p2 = lb.population(0, 2);
        REQUIRE(p2 > 1e-5);     // resolved well above integration error
        REQUIRE(p2 < previous); // spectral weight at α falls with the pulse length
        REQUIRE(lb.population(0, 1) > 0.8);
        REQUIRE(lb.leakage() == Approx(p2).margin(1e-12));
        for (const auto& s : lb.trajectory()) {
            REQUIRE(std::abs(s.trace - 1.0) < 1e-9);
            REQUIRE(s.populations[0] + s.populations[1] + s.populations[2] ==
                    Approx(1.0).margin(1e-9));
        }
        REQUIRE(lb.trajectory().back().timeS == Approx(tg).margin(1e-18));
        previous = p2;
    }
}

TEST_CASE("Lindblad: DRAG with the T05 quadrature convention suppresses leakage") {
    // Raised-cosine π pulses (smooth edges, so leakage is the non-adiabatic 1–2 transition that
    // DRAG targets). Ωy = −Ω̇x/α cancels it to first order (T05 (7.3)); the opposite sign enhances
    // it.
    auto leakage = [](double tg, double beta) {
        const double w = kTwoPi / tg, amp = kTwoPi / tg; // area A·tg/2 = π
        auto x = [=](double t) {
            return (t < 0 || t > tg) ? 0.0 : 0.5 * amp * (1.0 - std::cos(w * t));
        };
        auto dx = [=](double t) {
            return (t < 0 || t > tg) ? 0.0 : 0.5 * amp * w * std::sin(w * t);
        };
        return evolved(transmonUnder(x, dx, beta), tg).population(0, 2);
    };
    // Spec 25 §3.5: 20 ns π pulse, P₂ without DRAG > P₂ with DRAG, both < 1e-2.
    const double plain20 = leakage(20e-9, 0.0), drag20 = leakage(20e-9, -1.0 / kAlpha);
    REQUIRE(plain20 < 1e-2);
    REQUIRE(drag20 < plain20 / 100.0);
    REQUIRE(leakage(20e-9, 1.0 / kAlpha) > plain20);
    // Shorter pulse, larger leakage: the same ordering by an order of magnitude.
    const double plain10 = leakage(10e-9, 0.0);
    REQUIRE(leakage(10e-9, -1.0 / kAlpha) < plain10 / 10.0);
    REQUIRE(leakage(10e-9, 1.0 / kAlpha) > 2.0 * plain10);
}

TEST_CASE("Lindblad: trace, Hermiticity and positivity on driven dissipative coupled transmons") {
    // Two d = 3 transmons, detuned by 40 MHz, exchange-coupled (g/2π = 5 MHz), both driven with
    // Gaussians, with T1 and pure dephasing on each (spec 07 §5, spec 25 §3.5 first row).
    SystemModel m = bareModel(2, 3);
    m.h0 = anharmonicity(m, 0, kAlpha);
    m.h0 += anharmonicity(m, 1, -kTwoPi * 320e6);
    Matrix detune = numberOperator(m.siteDims, 1);
    detune *= kTwoPi * 40e6;
    m.h0 += detune;
    const Matrix a = ladderAnnihilate(m.siteDims, 0), b = ladderAnnihilate(m.siteDims, 1);
    Matrix exchange = num::matmul(num::adjoint(a), b);
    exchange += num::matmul(num::adjoint(b), a);
    exchange *= kTwoPi * 5e6;
    m.h0 += exchange;
    // Truncated Gaussians switch on and off at 1 ns sample boundaries, as AWG envelopes do.
    const GaussianPulse g0(10e-9, std::numbers::pi), g1(8e-9, std::numbers::pi / 2);
    m.drives.push_back(ladderDrive(
        m, 0, [g0](double t) { return Complex(0.5 * g0.value(t), 0.1 * g0.value(t)); }));
    m.drives.push_back(
        ladderDrive(m, 1, [g1](double t) { return Complex(0.0, 0.5 * g1.value(t - 2e-9)); }));
    for (std::uint32_t site : {0u, 1u}) {
        m.collapse.push_back(
            decay(m, site, 1.0 / 30e-9)); // exaggerated rates so dissipation is visible
        m.collapse.push_back(dephasing(m, site, 1.0 / 50e-9));
    }
    std::vector<Matrix> finals;
    for (auto integrator : {LindbladIntegrator::Rk4, LindbladIntegrator::Dopri5}) {
        INFO("integrator " << static_cast<int>(integrator));
        LindbladSettings s;
        s.integrator = integrator;
        auto lb = evolved(m, 12e-9, s);
        REQUIRE(lb.trajectory().size() == 13); // t = 0 plus one sample per 1 ns segment
        for (const auto& sample : lb.trajectory())
            REQUIRE(std::abs(sample.trace - 1.0) < 1e-9);
        const Matrix& rho = lb.rho();
        REQUIRE(std::abs(lb.stateNorm() - 1.0) < 1e-9);
        REQUIRE(maxHermitianDefect(rho) < 1e-9);
        REQUIRE(minEigenvalue(rho) > -1e-9);
        REQUIRE(num::purity(rho) < 0.95);
        finals.push_back(rho);
    }
    // RK4 at 10 ps (global error O(h⁴), spec 07 §5) and adaptive Dormand–Prince agree. Envelope
    // steps at sample boundaries must not degrade either to first order.
    REQUIRE(maxAbsDiff(finals[0], finals[1]) < 1e-8);
}

TEST_CASE(
    "Lindblad: exponential midpoint (Magnus 2) agrees with RK4 on a driven dissipative transmon") {
    SystemModel m = bareModel(1, 3);
    m.h0 = anharmonicity(m, 0, kAlpha);
    Matrix detune = numberOperator(m.siteDims, 0);
    detune *= kTwoPi * 3e6;
    m.h0 += detune;
    const GaussianPulse g(12e-9, std::numbers::pi);
    m.drives.push_back(ladderDrive(
        m, 0, [g](double t) { return 0.5 * Complex(g.value(t), -g.slope(t) / kAlpha); }));
    m.collapse.push_back(decay(m, 0, 1.0 / 40e-9));
    m.collapse.push_back(dephasing(m, 0, 1.0 / 60e-9));
    const auto rk4 = evolved(m, 15e-9);
    LindbladSettings s;
    s.integrator = LindbladIntegrator::Magnus2;
    const auto magnus = evolved(m, 15e-9, s);
    REQUIRE(maxAbsDiff(rk4.rho(), magnus.rho()) < 1e-6);
    REQUIRE(std::abs(magnus.stateNorm() - 1.0) < 1e-9);
    REQUIRE(minEigenvalue(magnus.rho()) > -1e-9);
    // Coarser AWG-length steps stay second-order accurate (exponential midpoint, spec 06 §6).
    s.stepS = 0.1e-9;
    REQUIRE(maxAbsDiff(rk4.rho(), evolved(m, 15e-9, s).rho()) < 1e-3);
}

TEST_CASE("Lindblad: dimension cap 3^5 and argument validation") {
    LindbladBackend lb;
    REQUIRE(lb.capabilities().maxQubits == 5);
    REQUIRE(lb.capabilities().timeDomain);
    REQUIRE(lb.evolve(1e-9).error().code == err::NotAllocated);
    REQUIRE(lb.allocate(5, 3).has_value());
    REQUIRE(lb.rho().rows == 243);
    REQUIRE(lb.levels() == 3);
    REQUIRE(lb.allocate(6, 2).error().code == err::TooLarge);
    REQUIRE(lb.allocate(4, 4).error().code == err::TooLarge); // 256 > 243
    // A site larger than a transmon is legal while the total dimension fits: 2 x 6 = 36 <= 243
    // (this is how an ion mode with a Fock cutoff is represented).
    REQUIRE(lb.allocate(2, 6).has_value());
    REQUIRE(lb.rho().rows == 36);
    REQUIRE(lb.allocate(2, 16).error().code == err::TooLarge);   // 256 > 243
    REQUIRE(lb.allocate(2, 1).error().code == err::Unsupported); // a site needs at least 2 levels
    REQUIRE(lb.setModel(bareModel(6, 2)).error().code == err::TooLarge);
    REQUIRE(lb.setModel(SystemModel{}).error().code == err::BadTargets);
    SystemModel bad = bareModel(2, 2);
    bad.h0(0, 1) = 1.0; // not Hermitian
    REQUIRE(lb.setModel(bad).error().code == err::NotUnitary);
    SystemModel wrongDrive = bareModel(2, 2);
    wrongDrive.drives.push_back(
        ladderDrive(bareModel(1, 2), 0, [](double) { return Complex(1.0); }));
    REQUIRE(lb.setModel(wrongDrive).error().code == err::BadTargets);
    SystemModel wrongCollapse = bareModel(1, 3);
    wrongCollapse.collapse.push_back(decay(bareModel(1, 2), 0, 1.0));
    REQUIRE(lb.setModel(wrongCollapse).error().code == err::BadTargets);
    // The pulse-level backend refuses gate-level operations and Kraus maps.
    REQUIRE(lb.allocate(1, 2).has_value());
    REQUIRE(lb.applyGate(X(), q({0})).error().code == err::Unsupported);
    REQUIRE(lb.applyControlled(X(), q({0}), q({0})).error().code == err::Unsupported);
    REQUIRE(lb.applyChannel(depolarizing(0.1), q({0})).error().code == err::Unsupported);
    REQUIRE(lb.evolve(-1e-9).error().code == ErrorCode::InvalidArgument);
    LindbladSettings zeroStep;
    zeroStep.stepS = 0.0;
    lb.setSettings(zeroStep);
    REQUIRE(lb.evolve(1e-9).error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("a site may be larger than a transmon: the ion motional mode needs a Fock cutoff") {
    // Regression: the per-site dimension was capped at 5, which refused an ion system built as
    // qubit sites plus one motional mode of dimension = Fock cutoff (8 by default, spec 09 §5.4),
    // even though the cost bound that matters is the total dimension (spec 07 §5, D <= 243).
    SystemModel m;
    m.siteDims = {2, 2, 8}; // two ions and the shared mode: D = 32, well inside the cap
    const std::size_t D = m.dimension();
    REQUIRE(D == 32);
    m.h0 = Matrix(D, D);
    m.frameFrequenciesHz.assign(3, 0.0);
    LindbladBackend lb;
    auto st = lb.setModel(m);
    if (!st)
        UNSCOPED_INFO(st.error().format());
    REQUIRE(st.has_value());

    // The total-dimension cap still binds: five 3-level sites plus a mode would exceed it.
    SystemModel big;
    big.siteDims = {3, 3, 3, 3, 8};
    const std::size_t bigD = big.dimension();
    big.h0 = Matrix(bigD, bigD);
    big.frameFrequenciesHz.assign(big.siteDims.size(), 0.0);
    auto refused = lb.setModel(big);
    REQUIRE_FALSE(refused.has_value());
    REQUIRE(refused.error().code == err::TooLarge);

    // A dimension below 2 is still nonsense.
    SystemModel tiny;
    tiny.siteDims = {1};
    tiny.h0 = Matrix(1, 1);
    tiny.frameFrequenciesHz.assign(1, 0.0);
    REQUIRE_FALSE(lb.setModel(tiny).has_value());
}
