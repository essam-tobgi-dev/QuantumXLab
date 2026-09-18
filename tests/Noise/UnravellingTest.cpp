// Spec 08 §7.1–7.2 — the StateVector stochastic unravelling reproduces the exact DensityMatrix
// evolution: 4000 seeded shots agree within 4 standard errors for every outcome and observable.
#include "NoiseTestSupport.hpp"
#include "Data/Fidelity.hpp"
#include <catch2/catch_approx.hpp>

using namespace ntest;
using namespace qlab::noise;
using Catch::Approx;

namespace {
struct Step {
    Matrix gate;               // applied first when non-empty
    std::vector<QubitIndex> gateTargets;
    AttachedChannel noise;     // then this channel
};

// Generalized damping at a strong thermal population, as a duration-dependent channel.
ChannelPtr generalizedAmplitudeDampingProbe() { return channel(amplitudeDampingChannel(15e-6, 0.3)); }

// A two-qubit circuit exercising every unravelling path: Pauli weights (depolarizing, bit flip,
// Pauli), single unitaries (over-rotation, ZZ) and general Kraus sets (relaxation, damping, dephasing).
std::vector<Step> mixedCircuit() {
    std::vector<Step> c;
    c.push_back({H(), q({0}), attach(channel(thermalRelaxationChannel(40e-6, 30e-6, 0.05)), q({0}), 12e-6)});
    c.push_back({CX(), q({0, 1}), attach(channel(depolarizingChannel(2, 0.15)), q({0, 1}))});
    c.push_back({{}, {}, attach(channel(overRotationChannel("ZX", 0.5)), q({0, 1}))});
    c.push_back({RY(0.9), q({1}), attach(channel(amplitudeDampingChannel(25e-6, 0.1)), q({1}), 10e-6)});
    c.push_back({{}, {}, attach(channel(zzCrosstalkChannel(20e3)), q({0, 1}), 8e-6)});
    c.push_back({SX(), q({0}), attach(channel(phaseDampingChannel(20e-6)), q({0}), 9e-6)});
    c.push_back({{}, {}, attach(channel(bitFlipChannel(0.07)), q({1}))});
    c.push_back({S(), q({1}), attach(channel(pauliChannel(0.02, 0.05, 0.08)), q({0}))});
    c.push_back({H(), q({1}), attach(channel(generalizedAmplitudeDampingProbe()), q({1}), 30e-6)});
    return c;
}

void run(qsim::IBackend& b, const std::vector<Step>& circuit, core::Random& rng, ApplyReport& report) {
    for (const auto& step : circuit) {
        if (!step.gate.empty()) NOISE_REQUIRE_OK(b.applyGate(step.gate, step.gateTargets));
        NOISE_REQUIRE_OK(applyChannel(b, step.noise, rng, {}, report));
    }
}

struct Observables { std::vector<double> values; }; // P(00), P(01), P(10), P(11), <X0>, <Z1>, <Y1 X0>

Observables observe(const qsim::IBackend& b) {
    Observables o;
    auto p = b.probabilities(q({0, 1}));
    NOISE_REQUIRE_OK(p);
    o.values.assign(p->begin(), p->end());
    for (const char* label : {"IX", "ZI", "YX"}) o.values.push_back(expectation(b, label));
    return o;
}
} // namespace

TEST_CASE("StateVector unravelling over 4000 seeded shots matches the DensityMatrix within 4 sigma") {
    const auto circuit = mixedCircuit();
    core::Random dmRng(1);
    qsim::DensityMatrixBackend dm;
    NOISE_REQUIRE_OK(dm.allocate(2));
    ApplyReport dmReport;
    run(dm, circuit, dmRng, dmReport);
    REQUIRE(dmReport.cls == data::FidelityClass::Exact);
    REQUIRE(dmReport.applied == circuit.size());
    const Observables exact = observe(dm);
    REQUIRE(exact.values[0] + exact.values[1] + exact.values[2] + exact.values[3] == Approx(1.0).margin(1e-12));

    qsim::StateVectorBackend pristine;
    NOISE_REQUIRE_OK(pristine.allocate(2));
    const core::Random master(0x5EEDull);
    const std::size_t shots = 4000, k = exact.values.size();
    std::vector<double> sum(k, 0.0), sumSq(k, 0.0);
    ApplyReport svReport;
    for (std::size_t s = 0; s < shots; ++s) {
        core::Random rng = master.stream(s); // one stream per shot (spec 04 §6)
        auto sv = pristine.clone();
        run(*sv, circuit, rng, svReport);
        REQUIRE(sv->stateNorm() == Approx(1.0).margin(1e-12)); // renormalised after every draw
        const Observables o = observe(*sv);
        for (std::size_t i = 0; i < k; ++i) { sum[i] += o.values[i]; sumSq[i] += o.values[i] * o.values[i]; }
    }
    REQUIRE(svReport.cls == data::FidelityClass::Statistical);
    const double n = static_cast<double>(shots);
    for (std::size_t i = 0; i < k; ++i) {
        const double mean = sum[i] / n, se = std::sqrt(std::max(sumSq[i] / n - mean * mean, 1e-14) / n);
        INFO("observable " << i << ": SV " << mean << " +- " << se << ", DM " << exact.values[i]);
        REQUIRE(std::abs(mean - exact.values[i]) < 4.0 * se);
    }
    // The comparison has power: the exact values are far from the noiseless circuit's.
    qsim::DensityMatrixBackend ideal;
    NOISE_REQUIRE_OK(ideal.allocate(2));
    for (const auto& step : circuit)
        if (!step.gate.empty()) NOISE_REQUIRE_OK(ideal.applyGate(step.gate, step.gateTargets));
    const Observables clean = observe(ideal);
    double distance = 0.0;
    for (std::size_t i = 0; i < k; ++i) distance = std::max(distance, std::abs(clean.values[i] - exact.values[i]));
    REQUIRE(distance > 0.1);
}

TEST_CASE("sampled counts follow the unravelled distribution, and readout errors apply per backend") {
    // Terminal sampling: DM applies Mᵀ to the exact distribution (§7.1); SV flips sampled bits (§7.2).
    const std::vector<Assignment2> assign{{{{0.97, 0.03}, {0.08, 0.92}}}, {{{0.94, 0.06}, {0.12, 0.88}}}};
    auto readout = ReadoutModel::independent(q({1, 0}), assign); // bit 0 = qubit 1, bit 1 = qubit 0
    NOISE_REQUIRE_OK(readout);
    qsim::DensityMatrixBackend dm;
    qsim::StateVectorBackend sv;
    for (qsim::IBackend* b : std::initializer_list<qsim::IBackend*>{&dm, &sv}) {
        NOISE_REQUIRE_OK(b->allocate(2));
        NOISE_REQUIRE_OK(b->applyGate(RY(1.2), q({0})));
        NOISE_REQUIRE_OK(b->applyGate(CX(), q({0, 1})));
        NOISE_REQUIRE_OK(b->applyGate(RY(0.4), q({1})));
    }
    auto p = dm.probabilities(q({1, 0}));
    NOISE_REQUIRE_OK(p);
    auto expected = readout->applyToProbabilities(*p);
    NOISE_REQUIRE_OK(expected);
    core::Random rng(99);
    const std::uint64_t shots = 40000;
    for (const qsim::IBackend* b : std::initializer_list<const qsim::IBackend*>{&dm, &sv}) {
        auto counts = sampleWithReadout(*b, *readout, shots, rng);
        NOISE_REQUIRE_OK(counts);
        auto freq = probabilitiesFromCounts(*counts, 2);
        NOISE_REQUIRE_OK(freq);
        for (std::size_t i = 0; i < 4; ++i) {
            const double e = (*expected)[i], sigma = std::sqrt(e * (1 - e) / static_cast<double>(shots));
            INFO(qsim::kindName(b->kind()) << " outcome " << i << ": " << (*freq)[i] << " vs " << e);
            REQUIRE(std::abs((*freq)[i] - e) < 4.0 * sigma);
        }
    }
    // Mid-circuit form: projective outcome, then the classical flip, bit order as requested.
    std::size_t ones = 0;
    const std::size_t trials = 20000;
    const core::Random master(3);
    for (std::size_t s = 0; s < trials; ++s) {
        auto copy = sv.clone();
        core::Random r = master.stream(s);
        auto outcome = measureWithReadout(*copy, *readout, r);
        NOISE_REQUIRE_OK(outcome);
        REQUIRE(outcome->bits.size() == 2);
        ones += outcome->bits[1]; // qubit 0
    }
    const double p1 = (*expected)[2] + (*expected)[3], sigma = std::sqrt(p1 * (1 - p1) / trials);
    REQUIRE(std::abs(static_cast<double>(ones) / trials - p1) < 4.0 * sigma);
}
