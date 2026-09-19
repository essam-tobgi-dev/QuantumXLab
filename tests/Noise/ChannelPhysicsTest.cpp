// Spec 08 §2, §9 / spec 25 §3.4 — closed-form physics of the catalogue on the DensityMatrix
// backend.
#include "NoiseTestSupport.hpp"
#include <catch2/catch_approx.hpp>

using namespace ntest;
using namespace qlab::noise;
using Catch::Approx;

TEST_CASE("depolarizing with p = 1 gives the maximally mixed state") {
    core::Random rng(1);
    const std::vector<Matrix> preparations{Matrix::identity(2), H(), SX(), RY(0.73)};
    for (const auto& prep : preparations) {
        qsim::DensityMatrixBackend dm;
        NOISE_REQUIRE_OK(dm.allocate(1));
        NOISE_REQUIRE_OK(dm.applyGate(prep, q({0})));
        apply(dm, attach(channel(depolarizingChannel(1, 1.0)), q({0})), rng);
        REQUIRE(
            num::approxEqual(dm.rho(), num::scale(Matrix::identity(2), Complex(0.5, 0)), 1e-15));
    }
    // ρ → (1−p)ρ + p I/d on d = 4 and d = 8 as well (spec 08 §2.1).
    qsim::DensityMatrixBackend dm3;
    NOISE_REQUIRE_OK(dm3.allocate(3));
    NOISE_REQUIRE_OK(dm3.applyGate(H(), q({0})));
    NOISE_REQUIRE_OK(dm3.applyGate(CX(), q({0, 2})));
    apply(dm3, attach(channel(depolarizingChannel(2, 1.0)), q({2, 1})), rng);
    auto pair = dm3.reducedDensityMatrix(q({1, 2}));
    NOISE_REQUIRE_OK(pair);
    REQUIRE(num::approxEqual(*pair, num::scale(Matrix::identity(4), Complex(0.25, 0)), 1e-15));
    auto three = channels::depolarizingNq(3, 1.0);
    NOISE_REQUIRE_OK(three);
    auto mixed =
        applyToDensity(*three, num::projector(std::vector<Complex>{0, 0, 0, 1, 0, 0, 0, 0}));
    NOISE_REQUIRE_OK(mixed);
    REQUIRE(num::approxEqual(*mixed, num::scale(Matrix::identity(8), Complex(0.125, 0)), 1e-15));
    // Partial strength: (1 − p)ρ + p I/2 exactly.
    auto partial = channels::depolarizing1q(0.37);
    NOISE_REQUIRE_OK(partial);
    const Matrix rho =
        num::projector(std::vector<Complex>{std::sqrt(0.2), Complex(0, std::sqrt(0.8))});
    auto out = applyToDensity(*partial, rho);
    NOISE_REQUIRE_OK(out);
    Matrix expected = num::scale(rho, Complex(0.63, 0));
    expected += num::scale(Matrix::identity(2), Complex(0.37 / 2, 0));
    REQUIRE(num::approxEqual(*out, expected, 1e-15));
}

TEST_CASE("amplitude damping on |1> leaves exactly exp(-t/T1) in the excited state") {
    core::Random rng(2);
    const double t1 = 87e-6;
    for (double t : {0.0, 1e-9, 35e-9, 12e-6, 87e-6, 300e-6}) {
        qsim::DensityMatrixBackend dm;
        NOISE_REQUIRE_OK(dm.allocate(2));
        NOISE_REQUIRE_OK(dm.applyGate(X(), q({1})));
        apply(dm, attach(channel(amplitudeDampingChannel(t1)), q({1}), t), rng);
        INFO("t = " << t);
        REQUIRE(dm.population(QubitIndex{1}, 1) ==
                Approx(std::exp(-t / t1)).margin(1e-12)); // spec 25 §3.4
        REQUIRE(dm.population(QubitIndex{0}, 0) ==
                Approx(1.0).margin(1e-15)); // spectator untouched
    }
    // With p_th the population relaxes to p_th: ρ11(t) = p_th + (1 − p_th) e^{−t/T1}, and → p_th.
    const double pth = 0.013;
    for (double t : {5e-6, 60e-6, 60.0 * t1}) {
        qsim::DensityMatrixBackend dm;
        NOISE_REQUIRE_OK(dm.allocate(1));
        NOISE_REQUIRE_OK(dm.applyGate(X(), q({0})));
        apply(dm, attach(channel(amplitudeDampingChannel(t1, pth)), q({0}), t), rng);
        REQUIRE(dm.population(QubitIndex{0}, 1) ==
                Approx(pth + (1.0 - pth) * std::exp(-t / t1)).margin(1e-12));
    }
}

TEST_CASE("thermal relaxation: <Z> decays with T1, <X> and <Y> with T2, off-diagonals exactly "
          "exp(-t/T2)") {
    core::Random rng(3);
    struct Params {
        double t1, t2, pth;
    };
    const Params sets[] = {{90.99e-6, 80.47e-6, 0.0073},
                           {50e-6, 100e-6, 0.0},
                           {120e-6, 30e-6, 0.02},
                           {std::numeric_limits<double>::infinity(), 40e-6, 0.0}};
    for (const auto& s : sets) {
        auto relax = channel(thermalRelaxationChannel(s.t1, s.t2, s.pth));
        for (double t : {0.0, 32e-9, 700e-9, 20e-6, 150e-6}) {
            INFO("T1 = " << s.t1 << " T2 = " << s.t2 << " p_th = " << s.pth << " t = " << t);
            const double e1 = std::isfinite(s.t1) ? std::exp(-t / s.t1) : 1.0,
                         e2 = std::exp(-t / s.t2);
            qsim::DensityMatrixBackend one;
            NOISE_REQUIRE_OK(one.allocate(1));
            NOISE_REQUIRE_OK(one.applyGate(X(), q({0})));
            apply(one, attach(relax, q({0}), t), rng);
            REQUIRE(expectation(one, "Z") ==
                    Approx(1.0 - 2.0 * s.pth - 2.0 * (1.0 - s.pth) * e1).margin(1e-12));
            qsim::DensityMatrixBackend plus;
            NOISE_REQUIRE_OK(plus.allocate(1));
            NOISE_REQUIRE_OK(plus.applyGate(H(), q({0})));
            apply(plus, attach(relax, q({0}), t), rng);
            REQUIRE(expectation(plus, "X") == Approx(e2).margin(1e-12));
            REQUIRE(std::abs(plus.rho()(0, 1)) == Approx(0.5 * e2).margin(1e-12)); // spec 08 §9
            qsim::DensityMatrixBackend yplus;
            NOISE_REQUIRE_OK(yplus.allocate(1));
            NOISE_REQUIRE_OK(yplus.applyGate(H(), q({0})));
            NOISE_REQUIRE_OK(yplus.applyGate(S(), q({0})));
            apply(yplus, attach(relax, q({0}), t), rng);
            REQUIRE(expectation(yplus, "Y") == Approx(e2).margin(1e-12));
        }
    }
    // The channel is the exact semigroup element: t then t' equals t + t' (no idle splitting
    // needed, T04 §6).
    auto relax = channel(thermalRelaxationChannel(70e-6, 55e-6, 0.01));
    qsim::DensityMatrixBackend split, whole;
    for (auto* b : {&split, &whole}) {
        NOISE_REQUIRE_OK(b->allocate(1));
        NOISE_REQUIRE_OK(b->applyGate(RY(1.1), q({0})));
    }
    apply(split, attach(relax, q({0}), 13e-6), rng);
    apply(split, attach(relax, q({0}), 29e-6), rng);
    apply(whole, attach(relax, q({0}), 42e-6), rng);
    REQUIRE(num::approxEqual(split.rho(), whole.rho(), 1e-14));
    // T2 > 2 T1 is rejected by the channel (the loaders clamp before building it).
    auto bad = thermalRelaxationChannel(50e-6, 101e-6, 0.0);
    REQUIRE_FALSE(bad);
    REQUIRE(bad.error().code == err::Unphysical);
}

TEST_CASE("generalized amplitude damping relaxes to n_th/(2 n_th + 1)") {
    for (double nth : {0.0, 2.5e-3, 0.019, 0.1, 1.7}) {
        const double pth = channels::thermalPopulationFromPhotons(nth);
        REQUIRE(pth == Approx(nth / (2.0 * nth + 1.0)).epsilon(1e-15));
        REQUIRE(channels::photonsFromThermalPopulation(pth) ==
                Approx(nth).epsilon(1e-12).margin(1e-15));
        auto full = channels::generalizedAmplitudeDamping(1.0, pth);
        NOISE_REQUIRE_OK(full);
        for (const auto& start :
             {Matrix::fromRows({{1, 0}, {0, 0}}), Matrix::fromRows({{0, 0}, {0, 1}})}) {
            auto rho = applyToDensity(*full, start);
            NOISE_REQUIRE_OK(rho);
            REQUIRE((*rho)(1, 1).real() == Approx(pth).margin(1e-15));
            REQUIRE(std::abs((*rho)(0, 1)) < 1e-15);
        }
        // diag(1 − p_th, p_th) is a fixed point for every γ (T04 §3.2).
        auto partial = channels::generalizedAmplitudeDamping(0.3, pth);
        NOISE_REQUIRE_OK(partial);
        const Matrix fixed = Matrix::fromRows({{1.0 - pth, 0}, {0, pth}});
        auto again = applyToDensity(*partial, fixed);
        NOISE_REQUIRE_OK(again);
        REQUIRE(num::approxEqual(*again, fixed, 1e-15));
    }
}

TEST_CASE("phase damping and measurement dephasing multiply coherences by the stated factors") {
    // (2.3) with λ = 1 − e^{−2t/Tφ}: ρ01 → ρ01 e^{−t/Tφ} (T04 (4.2)); populations untouched.
    const Matrix plus = Matrix::fromRows({{0.5, 0.5}, {0.5, 0.5}});
    const double tphi = 40e-6;
    for (double t : {0.0, 3e-6, 40e-6, 200e-6}) {
        auto pd = channels::phaseDampingOver(tphi, t);
        NOISE_REQUIRE_OK(pd);
        auto rho = applyToDensity(*pd, plus);
        NOISE_REQUIRE_OK(rho);
        REQUIRE((*rho)(0, 1).real() == Approx(0.5 * std::exp(-t / tphi)).margin(1e-15));
        REQUIRE((*rho)(1, 1).real() == Approx(0.5).margin(1e-15));
    }
    // spec 08 §3: at scale 1 an unmeasured neighbour loses coherence as e^{−t_ro/T2}; scale s mixes
    // λ.
    const double t2 = 80e-6, tro = 700e-9;
    for (double s : {0.0, 0.25, 1.0}) {
        auto md = channels::measurementDephasing(tro, t2, s);
        NOISE_REQUIRE_OK(md);
        auto rho = applyToDensity(*md, plus);
        NOISE_REQUIRE_OK(rho);
        const double lambda = s * (1.0 - std::exp(-2.0 * tro / t2));
        REQUIRE((*rho)(0, 1).real() == Approx(0.5 * std::sqrt(1.0 - lambda)).margin(1e-15));
        if (s == 1.0)
            REQUIRE((*rho)(0, 1).real() == Approx(0.5 * std::exp(-tro / t2)).margin(1e-15));
    }
}

TEST_CASE("coherent errors: over-rotation, ZZ phase and detuning phase act as their unitaries") {
    core::Random rng(4);
    const double eps = 0.21;
    qsim::DensityMatrixBackend one;
    NOISE_REQUIRE_OK(one.allocate(1));
    apply(one, attach(channel(overRotationChannel("X", eps)), q({0})), rng);
    REQUIRE(one.population(QubitIndex{0}, 1) ==
            Approx(std::pow(std::sin(eps / 2), 2)).margin(1e-15));
    // "ZX" in target order: Z on targets[0] (control), X on targets[1]. With the control in |1⟩ the
    // target rotates the other way; its population is the same, ⟨Y_target⟩ changes sign.
    for (int control : {0, 1}) {
        qsim::DensityMatrixBackend two;
        NOISE_REQUIRE_OK(two.allocate(2));
        if (control)
            NOISE_REQUIRE_OK(two.applyGate(X(), q({1})));
        apply(two, attach(channel(overRotationChannel("ZX", eps)), q({1, 0})), rng);
        REQUIRE(two.population(QubitIndex{0}, 1) ==
                Approx(std::pow(std::sin(eps / 2), 2)).margin(1e-15));
        REQUIRE(expectation(two, "IY") ==
                Approx((control ? 1.0 : -1.0) * std::sin(eps)).margin(1e-15));
    }
    // exp(−i 2πζt ZZ/4) on |++⟩: ⟨X0⟩ = cos(πζt) (T04 (9.1)).
    const double zeta = 72.29e3, t = 3.1e-6;
    qsim::DensityMatrixBackend pair;
    NOISE_REQUIRE_OK(pair.allocate(2));
    NOISE_REQUIRE_OK(pair.applyGate(H(), q({0})));
    NOISE_REQUIRE_OK(pair.applyGate(H(), q({1})));
    apply(pair, attach(channel(zzCrosstalkChannel(zeta)), q({0, 1}), t), rng);
    REQUIRE(expectation(pair, "IX") == Approx(std::cos(std::numbers::pi * zeta * t)).margin(1e-14));
    REQUIRE(expectation(pair, "XX") == Approx(1.0).margin(1e-14)); // ZZ commutes with XX
    // R_Z(2π δf t): ⟨X⟩ = cos(2π δf t), ⟨Y⟩ = sin(2π δf t) from |+⟩.
    const double df = 1.3e3, tau = 170e-6;
    qsim::DensityMatrixBackend det;
    NOISE_REQUIRE_OK(det.allocate(1));
    NOISE_REQUIRE_OK(det.applyGate(H(), q({0})));
    apply(det, attach(channel(detuningPhaseChannel(df)), q({0}), tau), rng);
    REQUIRE(expectation(det, "X") ==
            Approx(std::cos(2 * std::numbers::pi * df * tau)).margin(1e-14));
    REQUIRE(expectation(det, "Y") ==
            Approx(std::sin(2 * std::numbers::pi * df * tau)).margin(1e-14));
}
