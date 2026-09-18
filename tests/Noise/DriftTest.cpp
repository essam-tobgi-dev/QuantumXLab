// Spec 08 §2.6, T04 §8.2–8.3 / spec 25 §3.4 — quasi-static detuning drift: Ramsey decays as a
// Gaussian with T2*, a Hahn echo refocuses the drift and reveals T2.
#include "NoiseTestSupport.hpp"
#include <catch2/catch_approx.hpp>

using namespace ntest;
using namespace qlab::noise;
using Catch::Approx;

namespace {
constexpr double kT1 = 120e-6, kT2 = 90e-6, kT2Star = 30e-6;
enum class Sequence { Ramsey, Echo };

// One qubit with T1, T2 (echo), T2* and a thermal population; no calibrated gates, so a sequence
// is ideal pulses separated by exactly the idle channels.
NoiseModel driftModel() {
    auto m = NoiseModel::fromJson(core::Json::parse(
        R"({"schema": "qlab.noise/1", "qubits": {"0": {"t1_us": 120, "t2_us": 90, "t2_star_us": 30, "p_thermal": 0.01}}})"));
    NOISE_REQUIRE_OK(m);
    return std::move(*m);
}
// P(1) after sx – idle t – sx (Ramsey) or sx – idle t/2 – x – idle t/2 – sx (Hahn echo), T10 §8.3–8.4.
double runSequence(qsim::IBackend& b, const NoiseModel& m, Sequence s, double t, core::Random& rng, const ApplyOptions& o) {
    ApplyReport report;
    auto idle = [&](double dt) { NOISE_REQUIRE_OK(applyChannels(b, m.idleChannels(QubitIndex{0}, dt), rng, o, report)); };
    NOISE_REQUIRE_OK(b.applyGate(SX(), q({0})));
    if (s == Sequence::Ramsey) {
        idle(t);
    } else {
        idle(0.5 * t);
        NOISE_REQUIRE_OK(b.applyGate(X(), q({0})));
        idle(0.5 * t);
    }
    NOISE_REQUIRE_OK(b.applyGate(SX(), q({0})));
    auto p = b.probabilities(q({0}));
    NOISE_REQUIRE_OK(p);
    return (*p)[1];
}
double ramseyExact(double t) { return 0.5 * (1.0 + std::exp(-t / kT2) * std::exp(-std::pow(t / kT2Star, 2))); }
double echoExact(double t) { return 0.5 * (1.0 - std::exp(-t / kT2)); }
} // namespace

TEST_CASE("drift sigma follows (2.5) and Gauss-Hermite nodes reproduce the normal law") {
    const double sigma = driftSigmaFromT2Star(kT2Star);
    REQUIRE(sigma == Approx(std::numbers::sqrt2 / (2.0 * std::numbers::pi * kT2Star)).epsilon(1e-15));
    REQUIRE(t2StarFromDriftSigma(sigma) == Approx(kT2Star).epsilon(1e-15));
    REQUIRE(driftSigmaFromCalibration(kT2Star, kT2) == sigma);
    REQUIRE(driftSigmaFromCalibration(kT2, kT2) == 0.0); // T2* = T2: no quasi-static part (spec 08 §4.1)
    REQUIRE(driftSigmaFromT2Star(std::numeric_limits<double>::infinity()) == 0.0);
    for (std::size_t n : {7u, 20u, 40u, 64u}) {
        auto nodes = gaussHermiteDetunings(sigma, n);
        NOISE_REQUIRE_OK(nodes);
        REQUIRE(nodes->size() == n);
        double w = 0, m2 = 0, m4 = 0, m12 = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& node = (*nodes)[i];
            REQUIRE(node.weight > 0.0);
            if (i > 0) REQUIRE(node.detuningHz > (*nodes)[i - 1].detuningHz);
            REQUIRE(node.detuningHz == Approx(-(*nodes)[n - 1 - i].detuningHz).margin(1e-9 * sigma));
            const double x = node.detuningHz / sigma;
            w += node.weight;
            m2 += node.weight * x * x;
            m4 += node.weight * std::pow(x, 4);
            m12 += node.weight * std::pow(x, 12);
        }
        REQUIRE(w == Approx(1.0).margin(1e-14));
        REQUIRE(m2 == Approx(1.0).epsilon(1e-12));
        REQUIRE(m4 == Approx(3.0).epsilon(1e-12));
        REQUIRE(m12 == Approx(10395.0).epsilon(1e-10)); // 11!!, exact for n ≥ 7 (degree ≤ 2n − 1)
        if (n >= 40)
            for (double t : {10e-6, 30e-6, 60e-6, 90e-6}) {
                double c = 0.0;
                for (const auto& node : *nodes) c += node.weight * std::cos(2.0 * std::numbers::pi * node.detuningHz * t);
                REQUIRE(c == Approx(std::exp(-std::pow(t / kT2Star, 2))).margin(1e-12)); // T04 (8.2)
            }
    }
    REQUIRE_FALSE(gaussHermiteDetunings(sigma, 0));
    REQUIRE_FALSE(gaussHermiteDetunings(sigma, 65));
    REQUIRE_FALSE(gaussHermiteDetunings(-1.0, 7));
    auto none = gaussHermiteDetunings(0.0, 7);
    NOISE_REQUIRE_OK(none);
    REQUIRE(none->size() == 1);
}

TEST_CASE("Ramsey with quasi-static detuning decays as a Gaussian with T2* (DensityMatrix, fitted)") {
    const NoiseModel m = driftModel();
    REQUIRE(m.qubit(QubitIndex{0})->t1S == Approx(kT1).epsilon(1e-15));
    REQUIRE(m.qubit(QubitIndex{0})->driftSigmaHz == Approx(driftSigmaFromT2Star(kT2Star)).epsilon(1e-15));
    core::Random rng(1);
    double stt = 0, st3 = 0, st4 = 0, sy1 = 0, sy2 = 0; // normal equations of −ln E = a t + b t²
    for (int i = 1; i <= 24; ++i) {
        const double t = i * 2.5e-6;
        qsim::DensityMatrixBackend dm;
        NOISE_REQUIRE_OK(dm.allocate(1));
        const double p1 = runSequence(dm, m, Sequence::Ramsey, t, rng, {});
        INFO("t = " << t);
        REQUIRE(p1 == Approx(ramseyExact(t)).margin(1e-12)); // exact Gaussian-averaged channel (spec 08 §2.6)
        const double y = -std::log(2.0 * p1 - 1.0);
        stt += t * t; st3 += t * t * t; st4 += t * t * t * t;
        sy1 += y * t; sy2 += y * t * t;
    }
    const double det = stt * st4 - st3 * st3;
    const double a = (sy1 * st4 - sy2 * st3) / det, b = (stt * sy2 - st3 * sy1) / det;
    REQUIRE(1.0 / a == Approx(kT2).epsilon(1e-6));
    REQUIRE(1.0 / std::sqrt(b) == Approx(kT2Star).epsilon(1e-6)); // spec 25 asks for 1 %
}

TEST_CASE("a Hahn echo refocuses the drift and reveals T2 (DensityMatrix, shot average over Gauss-Hermite nodes)") {
    const NoiseModel m = driftModel();
    auto nodes = gaussHermiteDetunings(m.qubit(QubitIndex{0})->driftSigmaHz, 40);
    NOISE_REQUIRE_OK(nodes);
    core::Random rng(2);
    for (double t : {5e-6, 30e-6, 60e-6, 90e-6}) {
        INFO("t = " << t);
        double ramsey = 0.0, echo = 0.0;
        for (const auto& node : *nodes) {
            const double shot[] = {node.detuningHz};
            ApplyOptions options;
            options.shotDetuningHz = shot;
            qsim::DensityMatrixBackend a, b;
            NOISE_REQUIRE_OK(a.allocate(1));
            NOISE_REQUIRE_OK(b.allocate(1));
            ramsey += node.weight * runSequence(a, m, Sequence::Ramsey, t, rng, options);
            echo += node.weight * runSequence(b, m, Sequence::Echo, t, rng, options);
        }
        REQUIRE(ramsey == Approx(ramseyExact(t)).margin(1e-12));
        REQUIRE(echo == Approx(echoExact(t)).margin(1e-12)); // exponential with T2: the Gaussian part is gone
        // Without shot detunings each idle window dephases on its own, which an echo cannot undo:
        // e^{−2((t/2)/T2*)²} remains (the per-window form is exact for a single window only).
        qsim::DensityMatrixBackend windowed;
        NOISE_REQUIRE_OK(windowed.allocate(1));
        const double p = runSequence(windowed, m, Sequence::Echo, t, rng, {});
        REQUIRE(p == Approx(0.5 * (1.0 - std::exp(-t / kT2) * std::exp(-0.5 * std::pow(t / kT2Star, 2)))).margin(1e-12));
    }
}

TEST_CASE("per-shot detuning on the StateVector backend: Ramsey shows T2*, echo shows T2 (4000 shots, 4 sigma)") {
    const NoiseModel m = driftModel();
    qsim::StateVectorBackend pristine;
    NOISE_REQUIRE_OK(pristine.allocate(1));
    const core::Random master(0xD1F7ull);
    std::uint64_t stream = 0;
    for (double t : {15e-6, 30e-6, 45e-6}) {
        for (Sequence s : {Sequence::Ramsey, Sequence::Echo}) {
            const std::uint64_t shots = 4000;
            double sum = 0.0, sumSq = 0.0;
            for (std::uint64_t shot = 0; shot < shots; ++shot) {
                core::Random rng = master.stream(stream++);
                const auto detunings = m.drawShotDetunings(rng);
                ApplyOptions options;
                options.shotDetuningHz = detunings;
                auto b = pristine.clone();
                const double p1 = runSequence(*b, m, s, t, rng, options); // this trajectory's P(1)
                sum += p1;
                sumSq += p1 * p1;
            }
            const double n = static_cast<double>(shots), mean = sum / n;
            const double se = std::sqrt(std::max(sumSq / n - mean * mean, 1e-12) / n);
            const double exact = s == Sequence::Ramsey ? ramseyExact(t) : echoExact(t);
            INFO((s == Sequence::Ramsey ? "Ramsey" : "echo") << " t = " << t << " mean " << mean << " exact " << exact << " se " << se);
            REQUIRE(std::abs(mean - exact) < 4.0 * se);
        }
    }
}
