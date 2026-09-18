// Spec 10 §6.6, §11; T06 §6 — the Mølmer–Sørensen table and schedule: the corrected amplitude
// relation, loop closure α(τ) = 0 and the XX(π/2) phase of the pulse actually played.
#include "PulseTestSupport.hpp"

#include <catch2/catch_approx.hpp>

#include <numbers>

using namespace qlab;
using namespace qlab::pulse;
using Catch::Approx;
using pulsetest::library;

namespace {
constexpr double kPi = std::numbers::pi;

struct Loop { Complex alpha; double phi = 0.0; };

// Independent quadrature of T06 (11.1) and (6.2) for H = g(t) S_x (a e^{−iδt} + a† e^{iδt}):
// two midpoint sub-steps per held sample, cumulative ∫g cos δt', ∫g sin δt'.
Loop integrate(const std::vector<Complex>& held, double dt, double couplingPerRabi, double delta) {
    constexpr int sub = 2;
    const double h = dt / sub;
    Complex acc{};
    double cc = 0.0, ss = 0.0, phi = 0.0;
    for (std::size_t k = 0; k < held.size(); ++k) {
        const double g = couplingPerRabi * held[k].real();
        for (int m = 0; m < sub; ++m) {
            const double t = (static_cast<double>(k) + (m + 0.5) / sub) * dt;
            const double c = std::cos(delta * t), s = std::sin(delta * t);
            phi += g * (s * cc - c * ss) * h;
            cc += g * c * h;
            ss += g * s * h;
            acc += g * Complex(c, s) * h;
        }
    }
    return {Complex(0.0, -1.0) * acc, phi};
}

// Bell-state fidelity from |00⟩|n=0⟩ after U = D(Sα) e^{iΦS²}, target XX(θ): S_x eigenvalues
// {2, 0, 0, −2} with weight 1/4 each, coherent-state overlaps exp(−|α|²(s − s')²/2), Φ_target = −θ/4.
double bellFidelity(const Loop& l, double theta) {
    const double s[4] = {2.0, 0.0, 0.0, -2.0};
    Complex acc{};
    for (double a : s)
        for (double b : s)
            acc += std::polar(1.0, (l.phi + theta / 4.0) * (a * a - b * b)) *
                   std::exp(-std::norm(l.alpha) * (a - b) * (a - b) / 2.0) / 16.0;
    return acc.real();
}
} // namespace

TEST_CASE("shipped ms table follows T06 (6.6): Omega = pi*sqrt(K)/(sqrt(eta_i eta_j)*T), no extra pi") {
    for (const char* id : {"ion_chain_11", "ion_chain_32"}) {
        INFO(id);
        const PulseLibrary& lib = library(id);
        const auto& tpl = lib.templates()["ms_bichromatic"];
        const int K = tpl["K"].get<int>();
        REQUIRE(tpl["amp_rule"].get<std::string>().find("pi*eta") == std::string::npos);
        std::size_t checked = 0;
        for (auto const& key : lib.keys()) {
            if (key.gate != "ms") continue;
            const Defcal* d = lib.find("ms", key.qubits);
            const double stored = d->extra["omega_rad_s_at_pi_2"].get<double>();
            const double T = lib.calibration().edge(key.qubits[0], key.qubits[1])->duration.value.v;
            const double etaI = lib.calibration().qubit(key.qubits[0])->lambDicke->value;
            const double etaJ = lib.calibration().qubit(key.qubits[1])->lambDicke->value;
            REQUIRE(std::abs(stored / (kPi * std::sqrt(K) / (std::sqrt(etaI * etaJ) * T)) - 1.0) < 1e-6);
            const double delta = physics::msDetuningForDuration(T, K);
            REQUIRE(physics::msAngle(etaI, etaJ, stored, T, -delta) == Approx(kPi / 2.0).epsilon(2e-6));
            ++checked;
        }
        const std::size_t n = lib.device().dataQubitCount();
        REQUIRE(checked == n * (n - 1) / 2);
        REQUIRE(lib.checkMsTable());
    }
    // A table generated with the superseded π (Ω smaller by √π) is refused.
    const auto dir = hw::deviceRoot() / "ion_chain_11";
    auto env = core::JsonEnvelope::load(dir / "pulses.json", "pulses");
    auto loaded = hw::loadDevice(dir);
    REQUIRE(env);
    REQUIRE(loaded);
    core::Json old = env->data;
    for (auto& d : old["defcals"])
        if (d["gate"] == "ms") d["omega_rad_s_at_pi_2"] = d["omega_rad_s_at_pi_2"].get<double>() / std::sqrt(kPi);
    auto stale = PulseLibrary::fromJson(old, loaded->device, loaded->calibration);
    REQUIRE(stale);
    auto check = stale->checkMsTable();
    REQUIRE_FALSE(check.has_value());
    REQUIRE(check.error().message.find("regenerate") != std::string::npos);
}

TEST_CASE("the MS schedule closes the phase-space loop and accumulates the XX(pi/2) phase") {
    struct Case { const char* id; std::uint32_t i, j; };
    for (const Case& c : {Case{"ion_chain_11", 0, 1}, Case{"ion_chain_11", 3, 9}, Case{"ion_chain_32", 5, 20}}) {
        INFO(c.id << " ions " << c.i << "," << c.j);
        const PulseLibrary& lib = library(c.id);
        auto s = lib.scheduleFor("ms", {c.i, c.j}, {{"theta", kPi / 2.0}});
        REQUIRE(s);
        REQUIRE(s->verify(lib.device()));
        auto p = lib.msParams(c.i, c.j, kPi / 2.0);
        REQUIRE(p);
        REQUIRE(s->duration() == lib.quantise(p->durationS, true));
        auto drives = toSystemDrives(*s, lib);
        REQUIRE(drives);
        const DriveEnvelope* e = drives->find("ms[" + std::to_string(c.i) + "," + std::to_string(c.j) + "]");
        REQUIRE(e != nullptr);
        REQUIRE(e->kind == DriveKind::SpinDependentForce);
        REQUIRE(e->detuningRadPerS == Approx(p->detuningRadPerS).epsilon(1e-9)); // carried by the ms frame
        REQUIRE(e->detuningRadPerS < 0.0);                                       // θ > 0 needs δ < 0 (T06 (6.5))
        REQUIRE(e->scale == Approx(p->omegaMaxRadPerS).epsilon(1e-15));
        const double tMid = 0.4 * p->durationS;
        const Complex h = e->held[static_cast<std::size_t>(tMid / e->dtS)];
        REQUIRE(e->at(tMid) == 2.0 * h * std::cos(e->carrierPhase(tMid)));
        REQUIRE(e->carrierPhase(tMid) ==
                Approx(2.0 * kPi * (p->modeFrequencyHz - p->detuningRadPerS / (2.0 * kPi)) * tMid).epsilon(1e-12));

        const double perRabi = std::sqrt(e->etaI * e->etaJ) / 2.0; // g = ηΩ/2 (T06 (6.1))
        const Loop loop = integrate(e->held, e->dtS, perRabi, e->detuningRadPerS);
        INFO("|alpha| = " << std::abs(loop.alpha) << ", theta = " << -4.0 * loop.phi);
        REQUIRE(std::abs(loop.alpha) < 1e-6);                  // closure: residual error |α|²(2n̄+1) < 1e−12
        REQUIRE(std::abs(-4.0 * loop.phi - kPi / 2.0) < 1e-6); // phase: θ = −4Φ = π/2
        REQUIRE(bellFidelity(loop, kPi / 2.0) > 1.0 - 1e-9);   // spec 10 §11 asks > 0.995 at zero heating

        // Spec 10 §6.6 read literally for this shaped envelope — δτ = 2πK with the square-pulse Ω —
        // leaves the loop open by the Gaussian edges' missing area.
        const double scaleSquare = p->omegaSquareRadPerS / p->omegaMaxRadPerS;
        const Loop square = integrate(e->held, e->dtS, perRabi * scaleSquare, -p->squareDetuningRadPerS);
        REQUIRE(std::abs(square.alpha) > 0.02);
        REQUIRE(bellFidelity(square, kPi / 2.0) < 0.995);
        const double ratio = std::abs(p->detuningRadPerS) / p->squareDetuningRadPerS;
        REQUIRE(ratio > 1.0);
        REQUIRE(ratio < p->durationS / (p->durationS - 2.0 * p->edgeS));
    }
}

TEST_CASE("MS angle sign and amplitude scaling; the calibrated range is |theta| <= pi/2") {
    const PulseLibrary& lib = library("ion_chain_11");
    for (double theta : {-kPi / 2.0, kPi / 4.0, 0.2}) {
        INFO(theta);
        auto p = lib.msParams(2, 7, theta);
        REQUIRE(p);
        REQUIRE(p->amplitude == Approx(std::sqrt(std::abs(theta) / (kPi / 2.0))).epsilon(1e-14));
        auto drives = toSystemDrives(*lib.scheduleFor("ms", {2, 7}, {{"theta", theta}}), lib);
        REQUIRE(drives);
        const DriveEnvelope& e = drives->drives.front();
        REQUIRE((e.detuningRadPerS > 0.0) == (theta < 0.0));
        const Loop loop = integrate(e.held, e.dtS, std::sqrt(e.etaI * e.etaJ) / 2.0, e.detuningRadPerS);
        REQUIRE(std::abs(loop.alpha) < 1e-6);
        REQUIRE(-4.0 * loop.phi == Approx(theta).margin(1e-6));
        REQUIRE(bellFidelity(loop, theta) > 1.0 - 1e-9);
    }
    auto tooFar = lib.scheduleFor("ms", {2, 7}, {{"theta", 1.7}});
    REQUIRE_FALSE(tooFar.has_value());
    REQUIRE(tooFar.error().code == kErrWaveform);
    // symmetric gate: ms(7,2) is the same pulse
    auto swapped = lib.scheduleFor("ms", {7, 2}, {{"theta", kPi / 2.0}});
    REQUIRE(swapped);
    REQUIRE(swapped->channels().contains(ChannelId::bichromatic(2, 7)));
}
