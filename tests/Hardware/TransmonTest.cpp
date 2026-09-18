// Spec 09 §5.1–5.3 — transmon physics against the worked examples of T05.
#include "Hardware/Hardware.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qlab;
using namespace qlab::hw;
using Catch::Approx;
using units::Frequency;

TEST_CASE("charge-basis spectrum matches an independent diagonalization") {
    // E_J/h = 15.0 GHz, E_C/h = 0.300 GHz (E_J/E_C = 50). Exact diagonalization of
    // H = 4E_C(n - n_g)^2 - (E_J/2) sum |n><n+1| + h.c. converges for a charge cutoff >= 10 to
    // f01 = 5.68258 GHz and alpha = -344.8 MHz, for either n_g (the charge dispersion is < 1 kHz
    // in this regime). Cross-checked against a standalone tridiagonal QL solver.
    // NOTE: T05 §3.3 prints 5.714 GHz / -318 MHz for this example; those numbers are not
    // reproducible under any (n_g, cutoff) and the theory document needs the correction.
    auto s = transmon::chargeBasisSpectrum(Frequency(15.0e9), Frequency(0.300e9));
    REQUIRE(s);
    REQUIRE(s->f01.v / 1e9 == Approx(5.68258).margin(0.001));        // within 1 MHz
    REQUIRE(s->anharmonicity.v / 1e6 == Approx(-344.8).margin(1.0)); // within 1 MHz
    REQUIRE(s->energiesJ.size() == 3);
    REQUIRE(s->energiesJ[0] < s->energiesJ[1]);
    REQUIRE(s->energiesJ[1] < s->energiesJ[2]);
    REQUIRE(s->nMatrixElement01 > 0.0);

    // The asymptotic formula sits ~17 MHz above the exact f01 and understates |alpha| by 45 MHz,
    // which is the next order in E_C/E_J (spec 09 §5.1: the simulator always uses the exact value).
    REQUIRE(transmon::approxF01(Frequency(15.0e9), Frequency(0.300e9)).v > s->f01.v);
    REQUIRE(std::abs(transmon::approxAlpha(Frequency(0.300e9)).v) < std::abs(s->anharmonicity.v));
    // Asymptotic formulas (T05 (3.4)): sqrt(8 E_J E_C) - E_C = 6.000 - 0.300 = 5.700 GHz.
    REQUIRE(transmon::approxF01(Frequency(15.0e9), Frequency(0.300e9)).v / 1e9 ==
            Approx(5.700).margin(0.001));
    REQUIRE(transmon::approxAlpha(Frequency(0.300e9)).v / 1e6 == Approx(-300.0).margin(0.001));
}

TEST_CASE("charge dispersion is exponentially small in sqrt(8 EJ/EC)") {
    auto d50 = transmon::chargeDispersion01(Frequency(15.0e9), Frequency(0.300e9));
    auto d20 = transmon::chargeDispersion01(Frequency(6.0e9), Frequency(0.300e9));
    REQUIRE(d50);
    REQUIRE(d20);
    REQUIRE(d50->v < d20->v);      // deeper transmon regime suppresses the dispersion
    REQUIRE(d50->v < 1e6);         // well under a MHz at E_J/E_C = 50
    REQUIRE(d50->v >= 0.0);
}

TEST_CASE("(f01, alpha) inversion round-trips through the exact spectrum") {
    const Frequency f(5.10e9), a(-330e6);
    auto ej = transmon::fromTargets(f, a);
    REQUIRE(ej);
    auto back = transmon::chargeBasisSpectrum(ej->EJ, ej->EC);
    REQUIRE(back);
    REQUIRE(back->f01.v == Approx(f.v).epsilon(1e-6));
    REQUIRE(back->anharmonicity.v == Approx(a.v).epsilon(1e-4));
    REQUIRE(ej->EJ.v / ej->EC.v > 20.0); // a real transmon, not a charge qubit
}

TEST_CASE("SQUID flux tunability follows T05 (4.1)") {
    const Frequency ejs(20e9);
    REQUIRE(transmon::josephsonEnergy(ejs, 0.0).v == Approx(ejs.v));       // sweet spot
    REQUIRE(transmon::josephsonEnergy(ejs, 0.5).v == Approx(0.0).margin(1e3)); // symmetric null
    REQUIRE(transmon::josephsonEnergy(ejs, 0.25).v ==
            Approx(ejs.v * std::cos(3.14159265358979 * 0.25)).epsilon(1e-9));
    // An asymmetric SQUID never fully closes.
    REQUIRE(transmon::josephsonEnergy(ejs, 0.5, 0.1).v > 0.0);
    // Periodicity in one flux quantum.
    REQUIRE(transmon::josephsonEnergy(ejs, 1.0).v == Approx(transmon::josephsonEnergy(ejs, 0.0).v));
}

TEST_CASE("dispersive shift and critical photon number match hand computation") {
    // g = 70 MHz, f_q = 5.0 GHz, f_r = 7.0 GHz, alpha = -330 MHz.
    // Delta = -2.0 GHz; chi = g^2/Delta * alpha/(Delta+alpha) (T05 (6.3)).
    const Frequency g(70e6), fq(5.0e9), fr(7.0e9), alpha(-330e6);
    const double delta = fq.v - fr.v;
    const double expected = (g.v * g.v / delta) * (alpha.v / (delta + alpha.v));
    REQUIRE(transmon::dispersiveShift(g, fq, fr, alpha).v == Approx(expected).epsilon(1e-12));
    REQUIRE(expected < 0.0); // qubit below the resonator with negative alpha
    REQUIRE(expected / 1e3 == Approx(-346.996).margin(0.01)); // ≈ -347 kHz

    // n_crit = Delta^2 / (4 g^2)
    REQUIRE(transmon::criticalPhotonNumber(g, fq, fr) ==
            Approx(delta * delta / (4.0 * g.v * g.v)).epsilon(1e-12));
    REQUIRE(transmon::criticalPhotonNumber(g, fq, fr) == Approx(204.08).margin(0.1));

    // Purcell rate kappa g^2/Delta^2 (1/s)
    const Frequency kappa(2e6);
    REQUIRE(transmon::purcellRate(kappa, g, fq, fr) ==
            Approx(2.0 * 3.14159265358979 * kappa.v * g.v * g.v / (delta * delta)).epsilon(1e-9));
}

TEST_CASE("static ZZ matches the T05 (5.3) closed form") {
    // g = 3.2 MHz, f1 = 4.8 GHz, f2 = 5.0 GHz, alpha1 = alpha2 = -320 MHz.
    const Frequency g(3.2e6), f1(4.8e9), f2(5.0e9), a1(-320e6), a2(-320e6);
    const double delta = f1.v - f2.v;
    const double expected =
        2.0 * g.v * g.v * (a1.v + a2.v) / ((delta + a1.v) * (delta - a2.v));
    const double got = transmon::staticZZ(g, f1, f2, a1, a2).v;
    REQUIRE(got == Approx(expected).epsilon(1e-12));
    // |Delta| = 200 MHz < |alpha| = 320 MHz is the straddling regime: ZZ is positive there.
    REQUIRE(got > 0.0);
    REQUIRE(got / 1e3 == Approx(210.051).margin(0.01));
    // Outside the straddling regime the sign flips.
    const double outside = transmon::staticZZ(g, f1, units::Frequency(f1.v + 400e6), a1, a2).v;
    REQUIRE(outside < 0.0);
    REQUIRE(outside / 1e3 == Approx(-227.556).margin(0.01));
    // ZZ vanishes when both anharmonicities do (harmonic limit).
    REQUIRE(transmon::staticZZ(g, f1, f2, Frequency(0.0), Frequency(0.0)).v ==
            Approx(0.0).margin(1e-12));
}

TEST_CASE("tunable coupler has an off point above both qubits") {
    const Frequency g12(5e6), g1c(90e6), g2c(90e6), f1(6.0e9), f2(6.1e9);
    auto off = transmon::couplerOffPoint(g12, g1c, g2c, f1, f2);
    REQUIRE(off);
    REQUIRE(off->v > f2.v);
    REQUIRE(transmon::effectiveCoupling(g12, g1c, g2c, f1, f2, *off).v == Approx(0.0).margin(1e3));
    // Moving the coupler down from the off point turns the interaction back on.
    const double on = transmon::effectiveCoupling(g12, g1c, g2c, f1, f2, Frequency(off->v - 500e6)).v;
    REQUIRE(std::abs(on) > 1e6);
}

TEST_CASE("Kerr operators reproduce the Duffing ladder") {
    auto ops = transmon::kerrOperators(Frequency(5e9), Frequency(-330e6), 3);
    REQUIRE(ops.a.rows == 3);
    REQUIRE(ops.a(0, 1).real() == Approx(1.0));
    REQUIRE(ops.a(1, 2).real() == Approx(std::sqrt(2.0)));
    REQUIRE(num::isHermitian(ops.H));
    // H/hbar levels: 0, omega, 2 omega + alpha  (rad/s)
    const double w = 2.0 * 3.14159265358979 * 5e9, al = 2.0 * 3.14159265358979 * -330e6;
    REQUIRE(ops.H(0, 0).real() == Approx(0.0).margin(1e-6));
    REQUIRE(ops.H(1, 1).real() == Approx(w).epsilon(1e-12));
    REQUIRE(ops.H(2, 2).real() == Approx(2.0 * w + al).epsilon(1e-12));
}
