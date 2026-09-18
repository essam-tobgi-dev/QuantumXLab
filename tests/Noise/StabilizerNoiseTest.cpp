// Spec 08 §7.3–7.4, §2.5 — Pauli-frame noise on the stabilizer backend (exact for Pauli channels,
// Model-class twirl for the rest), and the backends that must refuse a channel.
#include "NoiseTestSupport.hpp"
#include "Data/Fidelity.hpp"
#include <catch2/catch_approx.hpp>

using namespace ntest;
using namespace qlab::noise;
using Catch::Approx;

namespace {
using Circuit = std::vector<std::pair<Matrix, std::vector<QubitIndex>>>;

// Runs Clifford gates interleaved with channels (channel k after gate k) and returns P over (q0, q1).
Status runClifford(qsim::IBackend& b, const Circuit& gates, const std::vector<AttachedChannel>& noise,
                   core::Random& rng, const ApplyOptions& options, ApplyReport& report) {
    for (std::size_t k = 0; k < gates.size(); ++k) {
        QXL_TRY(b.applyGate(gates[k].first, gates[k].second));
        if (k < noise.size()) QXL_TRY(applyChannel(b, noise[k], rng, options, report));
    }
    return {};
}

std::vector<double> stabilizerAverage(const Circuit& gates, const std::vector<AttachedChannel>& noise,
                                      const ApplyOptions& options, ApplyReport& report, std::vector<double>& se) {
    qsim::StabilizerBackend pristine;
    NOISE_REQUIRE_OK(pristine.allocate(2));
    const core::Random master(0xC11FF0DDull);
    const std::size_t shots = 4000;
    std::vector<double> sum(4, 0.0), sumSq(4, 0.0);
    for (std::size_t s = 0; s < shots; ++s) {
        core::Random rng = master.stream(s);
        auto st = pristine.clone();
        NOISE_REQUIRE_OK(runClifford(*st, gates, noise, rng, options, report));
        auto p = st->probabilities(q({0, 1}));
        NOISE_REQUIRE_OK(p);
        for (std::size_t i = 0; i < 4; ++i) { sum[i] += (*p)[i]; sumSq[i] += (*p)[i] * (*p)[i]; }
    }
    se.assign(4, 0.0);
    for (std::size_t i = 0; i < 4; ++i) {
        sum[i] /= static_cast<double>(shots);
        se[i] = std::sqrt(std::max(sumSq[i] / shots - sum[i] * sum[i], 1e-14) / shots);
    }
    return sum;
}

std::vector<double> densityMatrixExact(const Circuit& gates, const std::vector<AttachedChannel>& noise) {
    qsim::DensityMatrixBackend dm;
    NOISE_REQUIRE_OK(dm.allocate(2));
    core::Random rng(0);
    ApplyReport report;
    NOISE_REQUIRE_OK(runClifford(dm, gates, noise, rng, {}, report));
    auto p = dm.probabilities(q({0, 1}));
    NOISE_REQUIRE_OK(p);
    return *p;
}
} // namespace

TEST_CASE("Pauli channels on the stabilizer backend reproduce the DensityMatrix within 4 sigma") {
    const Circuit gates{{H(), q({0})}, {CX(), q({0, 1})}, {S(), q({1})}, {H(), q({1})}, {SX(), q({0})}};
    auto shotAveragedDrift = channel(driftChannel(5e3)); // Pauli-Z form: admissible without a twirl
    const std::vector<AttachedChannel> noise{attach(channel(depolarizingChannel(1, 0.2)), q({0})),
                                             attach(channel(depolarizingChannel(2, 0.12)), q({1, 0})),
                                             attach(channel(phaseDampingChannel(15e-6)), q({1}), 8e-6),
                                             attach(shotAveragedDrift, q({1}), 40e-6),
                                             attach(channel(pauliChannel(0.05, 0.1, 0.02)), q({0}))};
    ApplyReport report;
    std::vector<double> se;
    const auto estimate = stabilizerAverage(gates, noise, {}, report, se);
    const auto exact = densityMatrixExact(gates, noise);
    REQUIRE(report.cls == data::FidelityClass::Statistical);
    REQUIRE(report.twirled.empty());
    for (std::size_t i = 0; i < 4; ++i) {
        INFO("outcome " << i << ": stabilizer " << estimate[i] << " +- " << se[i] << ", DM " << exact[i]);
        REQUIRE(std::abs(estimate[i] - exact[i]) < 4.0 * se[i]);
    }
}

TEST_CASE("non-Pauli channels need twirl_non_pauli on the stabilizer backend and mark the run Model") {
    const Circuit gates{{X(), q({0})}, {H(), q({1})}, {H(), q({1})}};
    const double t1 = 30e-6, t2 = 25e-6, t = 20e-6;
    const std::vector<AttachedChannel> noise{attach(channel(thermalRelaxationChannel(t1, t2, 0.0)), q({0}), t),
                                             attach(channel(thermalRelaxationChannel(t1, t2, 0.0)), q({1}), t)};
    qsim::StabilizerBackend st;
    NOISE_REQUIRE_OK(st.allocate(2));
    core::Random rng(5);
    ApplyReport refused;
    NOISE_REQUIRE_OK(st.applyGate(X(), q({0})));
    auto s = applyChannel(st, noise[0], rng, {}, refused);
    REQUIRE_FALSE(s);
    REQUIRE(s.error().code == err::NotPauli);
    REQUIRE(s.error().message.find("twirl_non_pauli") != std::string::npos);

    ApplyOptions twirl;
    twirl.twirlNonPauli = true;
    ApplyReport report;
    std::vector<double> se;
    const auto estimate = stabilizerAverage(gates, noise, twirl, report, se);
    REQUIRE(report.cls == data::FidelityClass::Model);
    REQUIRE(report.twirled == std::vector<std::string>{"thermal_relaxation"});
    // Oracle: the DensityMatrix run of the (7.1) Pauli channel that replaces thermal_relaxation.
    const double g1 = 1.0 - std::exp(-t / t1), g2 = 1.0 - std::exp(-t / t2);
    auto twirled = channel(pauliChannel(g1 / 4, g1 / 4, g2 / 2 - g1 / 4));
    const auto exact = densityMatrixExact(gates, {attach(twirled, q({0})), attach(twirled, q({1}))});
    for (std::size_t i = 0; i < 4; ++i) {
        INFO("outcome " << i << ": stabilizer " << estimate[i] << " +- " << se[i] << ", twirled DM " << exact[i]);
        REQUIRE(std::abs(estimate[i] - exact[i]) < 4.0 * se[i]);
    }
    // Leakage has no Pauli twirl at all (T04 §9.3).
    auto leak = applyChannel(st, attach(channel(leakageChannel(0.01, 0.01)), q({0})), rng, twirl, report);
    REQUIRE_FALSE(leak);
    REQUIRE(leak.error().code == err::UnsupportedBackend);
}

TEST_CASE("leakage acts on d = 3 density matrices only, and the Lindblad backends refuse gate-level channels") {
    core::Random rng(6);
    auto leak = channel(leakageChannel(0.02, 0.3));
    qsim::DensityMatrixBackend qutrit;
    NOISE_REQUIRE_OK(qutrit.allocate(2, 3));
    NOISE_REQUIRE_OK(qutrit.applyGate(X(), q({1}))); // qubit-subspace gate on a 3-level site
    apply(qutrit, attach(leak, q({1})), rng);
    REQUIRE(qutrit.population(QubitIndex{1}, 2) == Approx(0.02).margin(1e-15));
    REQUIRE(qutrit.population(QubitIndex{1}, 1) == Approx(0.98).margin(1e-15));
    apply(qutrit, attach(leak, q({1})), rng); // seepage returns 30 % of |2⟩, 2 % of |1⟩ leaks again
    REQUIRE(qutrit.population(QubitIndex{1}, 2) == Approx(0.02 * 0.7 + 0.98 * 0.02).margin(1e-15));
    // A qubit channel on the qutrit leaves |2⟩ alone (spec 08 §2.5, T04 §9.3).
    apply(qutrit, attach(channel(depolarizingChannel(1, 1.0)), q({1})), rng);
    REQUIRE(qutrit.population(QubitIndex{1}, 2) == Approx(0.02 * 0.7 + 0.98 * 0.02).margin(1e-15));
    REQUIRE(qutrit.stateNorm() == Approx(1.0).margin(1e-14));

    qsim::DensityMatrixBackend qubits;
    NOISE_REQUIRE_OK(qubits.allocate(1));
    ApplyReport report;
    auto mismatch = applyChannel(qubits, attach(leak, q({0})), rng, {}, report);
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.error().code == err::LevelsMismatch);
    qsim::StateVectorBackend sv;
    NOISE_REQUIRE_OK(sv.allocate(1));
    auto svLeak = applyChannel(sv, attach(leak, q({0})), rng, {}, report);
    REQUIRE_FALSE(svLeak);
    REQUIRE(svLeak.error().code == err::UnsupportedBackend);

    qsim::LindbladBackend lindblad;
    NOISE_REQUIRE_OK(lindblad.allocate(1));
    auto refused = applyChannel(lindblad, attach(channel(depolarizingChannel(1, 0.1)), q({0})), rng, {}, report);
    REQUIRE_FALSE(refused);
    REQUIRE(refused.error().code == err::UnsupportedBackend);
    REQUIRE(refused.error().message.find("lindbladOperators") != std::string::npos);
    // Target validation happens before any backend work.
    auto outOfRange = applyChannel(sv, attach(channel(bitFlipChannel(0.1)), q({3})), rng, {}, report);
    REQUIRE_FALSE(outOfRange);
    REQUIRE(outOfRange.error().code == err::UnknownTarget);
    auto wrongArity = applyChannel(sv, attach(channel(bitFlipChannel(0.1)), q({0, 1})), rng, {}, report);
    REQUIRE_FALSE(wrongArity);
    REQUIRE(wrongArity.error().code == err::BadDimensions);
    // Per-shot drift needs a detuning for the targeted qubit (spec 08 §2.6).
    qsim::StateVectorBackend pair;
    NOISE_REQUIRE_OK(pair.allocate(2));
    ApplyOptions shortShot;
    const double single[] = {1e3};
    shortShot.shotDetuningHz = single;
    auto missing = applyChannel(pair, attach(channel(driftChannel(1e3)), q({1}), 1e-6), rng, shortShot, report);
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error().code == err::UnknownTarget);
    apply(pair, attach(channel(driftChannel(1e3)), q({0}), 1e-6), rng, shortShot); // qubit 0 is covered
}
