// Spec 10 §6 expressions and `cal.*` paths; spec 10 §7 / T05 §7–§8 / T06 §6 closed-form relations.
#include "PulseTestSupport.hpp"

#include <catch2/catch_approx.hpp>

#include <numbers>

using namespace qlab;
using namespace qlab::pulse;
using Catch::Approx;
using pulsetest::library;

namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
} // namespace

TEST_CASE("pulses.json expressions: parameters, pi, arithmetic, and diagnostics") {
    const ParamMap theta{{"theta", kPi / 2.0}};
    const PathResolver none;
    REQUIRE(*evalExpression("0.5*theta/(pi/2)", theta, none) == Approx(0.5).epsilon(1e-15));
    REQUIRE(*evalExpression("-theta", theta, none) == -kPi / 2.0);
    REQUIRE(*evalExpression("theta-1", theta, none) ==
            Approx(kPi / 2.0 - 1.0).epsilon(1e-15)); // '-' subtracts
    REQUIRE(*evalExpression("2*(1+3)/4", {}, none) == 2.0);
    REQUIRE(*evalExpression(" 1e-3 * 2 ", {}, none) == Approx(2e-3).epsilon(1e-15));
    for (const char* bad : {"1/0", "foo", "2pi", "(1+2", "3*", "theta-"}) {
        INFO(bad);
        auto r = evalExpression(bad, theta, none);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == kErrLibrary);
    }
    const core::Json cal =
        core::Json::parse(R"({"qubits": {"0": {"f01_ghz": [4.8, 2e-5, "qubit_spectroscopy"]}}})");
    const core::Json dev = core::Json::parse(R"({"ion": {"f_qubit_ghz": 12.6}})");
    const PathResolver raw = makeJsonResolver(cal, dev);
    REQUIRE(*evalExpression("cal.qubits.0.f01_ghz*1000", {}, raw) == Approx(4800.0));
    REQUIRE(*evalExpression("device.ion.f_qubit_ghz", {}, raw) == 12.6);
}

TEST_CASE("cal.* and device.* paths resolve against the typed calibration in the field's unit") {
    const PulseLibrary& fixed = library("sc_fixed_5");
    const PathResolver cal = fixed.resolver();
    const hw::Calibration& c = fixed.calibration();
    REQUIRE(*cal("cal.qubits.3.f01_ghz") == Approx(c.qubit(3)->f01.value.v / 1e9).epsilon(1e-15));
    REQUIRE(*cal("cal.qubits.0.anharmonicity_mhz") ==
            Approx(c.qubit(0)->anharmonicity.value.v / 1e6).epsilon(1e-15));
    REQUIRE(*cal("cal.qubits.2.readout_f_ghz") ==
            Approx(c.qubit(2)->readoutFrequency->value.v / 1e9).epsilon(1e-15));
    REQUIRE(*cal("cal.qubits.4.t1_us") == Approx(c.qubit(4)->t1.value.v * 1e6).epsilon(1e-15));
    REQUIRE(*cal("cal.edges.1-0.duration_ns") ==
            Approx(440.0).epsilon(1e-15)); // either order of the key
    REQUIRE(*evalExpression("cal.edges.0-1.duration_ns*2", {}, cal) ==
            Approx(880.0).epsilon(1e-15));
    REQUIRE(*evalExpression("cal.edges.0-1.duration_ns-40", {}, cal) ==
            Approx(400.0).epsilon(1e-15));
    REQUIRE(*cal("device.timing.dt_ps") == 222.0);

    const PulseLibrary& grid = library("sc_tunable_grid_54");
    REQUIRE(*grid.resolver()("cal.edges.0-1.siswap.duration_ns") == Approx(32.0).epsilon(1e-15));
    REQUIRE(*grid.resolver()("cal.edges.0-1.coupler") == 54.0);

    const PulseLibrary& ions = library("ion_chain_11");
    const PathResolver ion = ions.resolver();
    REQUIRE(*ion("cal.qubits.5.lamb_dicke") ==
            Approx(ions.calibration().qubit(5)->lambDicke->value).epsilon(1e-15));
    REQUIRE(*ion("cal.motional.axial_modes_mhz.0") ==
            Approx(0.30).epsilon(1e-9)); // COM mode at ω_z (T06 §1.3)
    REQUIRE(*ion("device.ion.f_qubit_ghz") == Approx(12.642812118).epsilon(1e-15));

    for (const char* bad :
         {"cal.qubits.99.f01_ghz", "cal.qubits.0.lamb_dicke", "cal.edges.0-4.duration_ns",
          "cal.qubits.x.f01_ghz", "cal.nothing", "device.ion.f_qubit_ghz"}) {
        INFO(bad);
        auto r = cal(bad);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().message.find(bad) != std::string::npos);
    }
}

TEST_CASE("area theorem, DRAG and cross-resonance relations (spec 10 §7, T05 §7–§8)") {
    // θ = κ·A·area and its inverse
    REQUIRE(physics::rotationAngle(2.0e8, 0.25, 3.0e-8) == Approx(1.5));
    REQUIRE(physics::amplitudeForAngle(1.5, 2.0e8, 3.0e-8) == Approx(0.25));
    // β = −1/α with α/2π = −330 MHz
    REQUIRE(physics::dragBeta(kTwoPi * -330e6) == Approx(1.0 / (kTwoPi * 330e6)).epsilon(1e-15));
    // T05 §8 numbers: J/2π = 3 MHz, Δ/2π = +150 MHz, α_c/2π = −330 MHz → ν_ZX = −0.0367 Ω, ν_IX =
    // +0.0167 Ω
    const double J = kTwoPi * 3e6, delta = kTwoPi * 150e6, alpha = kTwoPi * -330e6;
    REQUIRE(physics::crZxRate(J, 1.0, delta, alpha) ==
            Approx(-0.02 * 330.0 / 180.0).epsilon(1e-12));
    REQUIRE(physics::crIxRate(J, 1.0, delta, alpha) == Approx(3.0 / 180.0).epsilon(1e-12));
    const double omegaFor2MHz = kTwoPi * 2e6 / std::abs(physics::crZxRate(J, 1.0, delta, alpha));
    REQUIRE(omegaFor2MHz / kTwoPi ==
            Approx(2e6 * 180.0 / (0.02 * 330.0)).epsilon(1e-12));    // 54.55 MHz
    REQUIRE(std::abs(omegaFor2MHz / (kTwoPi * 54e6) - 1.0) < 0.015); // T05 quotes it as "54 MHz"
    REQUIRE((kPi / 2.0) / (kTwoPi * 2e6) == Approx(125e-9));         // "t = 125 ns"
    // inverse: the amplitude that makes ∫ν_ZX dt = π/4 over a 100 ns envelope area
    const double kappa = kTwoPi * 100e6, area = 100e-9;
    const double amp = physics::crAmplitudeForAngle(kPi / 4.0, J, delta, alpha, kappa, area);
    REQUIRE(physics::crZxRate(J, kappa * amp, delta, alpha) * area ==
            Approx(kPi / 4.0).epsilon(1e-12));
}

TEST_CASE("Mølmer–Sørensen square-pulse relations match T06 (6.5)-(6.6) and its worked numbers") {
    const double eta = 0.08, omega = kTwoPi * 500e3;
    const double d1 = physics::msMaxEntanglingDetuning(eta, omega, 1);
    REQUIRE(d1 / kTwoPi == Approx(80e3).epsilon(1e-12));                       // δ/2π = 80 kHz
    REQUIRE(physics::msLoopDuration(d1, 1) == Approx(12.5e-6).epsilon(1e-12)); // τ = 12.5 µs
    const double d2 = physics::msMaxEntanglingDetuning(eta, omega, 2);
    REQUIRE(d2 / kTwoPi == Approx(113.137e3).epsilon(1e-5));
    REQUIRE(physics::msLoopDuration(d2, 2) == Approx(17.678e-6).epsilon(1e-4));
    // θ = −η_iη_jΩ²τ/δ: the maximally entangling XX(+π/2) needs δ < 0; |θ| = π/2 for both loops
    REQUIRE(physics::msAngle(eta, eta, omega, physics::msLoopDuration(d1, 1), -d1) ==
            Approx(kPi / 2.0).epsilon(1e-12));
    REQUIRE(physics::msAngle(eta, eta, omega, physics::msLoopDuration(d2, 2), d2) ==
            Approx(-kPi / 2.0).epsilon(1e-12));
    REQUIRE(physics::msAmplitudeForAngle(kPi / 2.0, eta, eta, physics::msLoopDuration(d1, 1), d1) ==
            Approx(omega).epsilon(1e-12));
    // Spec 10 §6.6 at the shipped 200 µs, K = 1: Ω = π√K/(ητ); the superseded line was smaller by
    // √π.
    const double tau = 200e-6;
    const double om = physics::msAmplitudeForAngle(kPi / 2.0, eta, eta, tau,
                                                   physics::msDetuningForDuration(tau, 1));
    REQUIRE(om == Approx(kPi / (eta * tau)).epsilon(1e-12));
}

TEST_CASE(
    "MS propagator integrals are exact for piecewise-constant couplings (T06 (6.2), (11.1))") {
    // constant g split into segments, loop not closed: α = −(g/δ)(e^{iδτ} − 1), Φ = (g²/δ)(τ − sin
    // δτ/δ)
    const double g = 1234.0, delta = kTwoPi * 3.3e3, dt = 1e-6;
    const std::vector<double> coupling(137, g);
    const double tau = 137 * dt;
    const auto r = physics::msIntegrals(coupling, dt, delta);
    const Complex alpha = -(g / delta) * (std::polar(1.0, delta * tau) - 1.0);
    REQUIRE(std::abs(r.alpha - alpha) < 1e-12 * std::abs(alpha));
    REQUIRE(r.phi == Approx(g * g / delta * (tau - std::sin(delta * tau) / delta)).epsilon(1e-12));
    // closed loop: α = 0 and θ = −4g²τ/δ
    const double closedDt = (kTwoPi / delta) / 1000.0;
    const auto closed = physics::msIntegrals(std::vector<double>(1000, g), closedDt, delta);
    REQUIRE(std::abs(closed.alpha) < 1e-12 * g * 1000 * closedDt);
    REQUIRE(closed.theta() == Approx(-4.0 * g * g * (kTwoPi / delta) / delta).epsilon(1e-12));

    // unequal segments against an independent fine midpoint double integral
    const std::vector<double> steps{1.0e3, 3.0e3, -2.0e3, 0.5e3};
    const double h = 10e-6, d = kTwoPi * 7e3;
    const auto exact = physics::msIntegrals(steps, h, d);
    const int sub = 4000;
    double cc = 0.0, ss = 0.0, phi = 0.0;
    Complex acc{};
    for (std::size_t k = 0; k < steps.size(); ++k)
        for (int m = 0; m < sub; ++m) {
            const double t = (static_cast<double>(k) + (m + 0.5) / sub) * h, w = steps[k] * h / sub;
            phi += steps[k] * (std::sin(d * t) * cc - std::cos(d * t) * ss) * (h / sub);
            cc += w * std::cos(d * t);
            ss += w * std::sin(d * t);
            acc += w * std::polar(1.0, d * t);
        }
    REQUIRE(std::abs(exact.alpha - Complex(0.0, -1.0) * acc) < 1e-6 * std::abs(acc));
    REQUIRE(exact.phi == Approx(phi).epsilon(1e-4));
}
