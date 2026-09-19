// Spec 16 §3–§4, §6; T09 §5.3–§5.4 — the generated circuits on the stabilizer backend: fault-free
// runs fire no detector, single faults fire the detectors the theory predicts, the noise plans hold
// the sites of each setting, and the Pauli-frame engine reproduces the tableau engine shot by shot.
#include "QEC/Sampler.hpp"
#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>

using namespace qlab;
using namespace qlab::qec;
using Catch::Approx;

namespace {
StabilizerCode shipped(const std::string& id) {
    return loadShippedCode(id).value();
}

MemoryExperiment plan(const std::string& id, std::uint32_t rounds,
                      LogicalBasis basis = LogicalBasis::Z) {
    ExtractionOptions opt;
    opt.rounds = rounds;
    opt.basis = basis;
    return planMemoryExperiment(shipped(id), opt).value();
}

std::uint32_t roundStartOp(const MemoryExperiment& ex, std::uint32_t round) {
    for (std::uint32_t i = 0; i < ex.schedule.ops.size(); ++i)
        if (ex.schedule.ops[i].kind == OpKind::RoundStart && ex.schedule.ops[i].a == round)
            return i;
    FAIL("round start not found");
    return 0;
}

std::vector<std::uint32_t> fired(const std::vector<std::uint8_t>& events) {
    std::vector<std::uint32_t> f;
    for (std::uint32_t i = 0; i < events.size(); ++i)
        if (events[i])
            f.push_back(i);
    return f;
}
} // namespace

TEST_CASE(
    "Sampler: fault-free memory experiments fire no detector and read the prepared logical state") {
    for (const std::string& id : shippedCodeIds())
        for (LogicalBasis basis : {LogicalBasis::Z, LogicalBasis::X}) {
            INFO(id << " basis " << basisName(basis));
            const std::uint32_t rounds = id == "surface_rot_7" ? 2 : 3;
            const MemoryExperiment ex = plan(id, rounds, basis);
            const auto sampler = TableauSampler::create(ex.schedule);
            REQUIRE(sampler.has_value());
            core::Random rng(0x51A0 + rounds);
            std::vector<std::uint8_t> bits;
            bool sawRandomSyndrome = false;
            for (int shot = 0; shot < 4; ++shot) {
                REQUIRE(sampler->run({}, rng, bits).has_value());
                const auto events = ex.detectionEvents(bits);
                REQUIRE(std::count(events.begin(), events.end(), 1) == 0);
                const auto logical = ex.observableValues(bits);
                REQUIRE(logical == std::vector<std::uint8_t>{0});
                sawRandomSyndrome =
                    sawRandomSyndrome || std::count(bits.begin(), bits.end(), 1) > 0;
            }
            // Projection onto the code space draws random first-round syndromes for the checks the
            // product state does not fix (every code here has some, except in its protected basis).
            if (id == "surface_rot_5")
                REQUIRE(sawRandomSyndrome);
        }
}

TEST_CASE("Sampler: single faults fire the detectors of T09 §5.3") {
    const MemoryExperiment ex = plan("surface_rot_3", 3);
    const auto sampler = TableauSampler::create(ex.schedule).value();
    core::Random rng(7);
    std::vector<std::uint8_t> bits;
    auto eventsOf = [&](const FaultEvent& f) {
        REQUIRE(sampler.run(std::span<const FaultEvent>(&f, 1), rng, bits).has_value());
        return std::pair{fired(ex.detectionEvents(bits)), ex.observableValues(bits)[0]};
    };
    // X on the centre data qubit before round 1: the two adjacent Z checks (generators 2 and 5)
    // fire in layer 1 and nowhere else; qubit 4 is outside Z̄ = Z2 Z5 Z8.
    FaultEvent bulk;
    bulk.afterOp = roundStartOp(ex, 1);
    bulk.qubitA = 4;
    bulk.pauliA = 'X';
    auto [d1, o1] = eventsOf(bulk);
    REQUIRE(d1 == std::vector<std::uint32_t>{ex.detectorAt(2, 1), ex.detectorAt(5, 1)});
    REQUIRE(o1 == 0);
    // X on qubit 2, the top-left corner at (1, 5): one Z check only — a boundary edge — and the
    // logical readout flips because the qubit lies on Z̄.
    FaultEvent corner = bulk;
    corner.qubitA = 2;
    auto [d2, o2] = eventsOf(corner);
    REQUIRE(d2 == std::vector<std::uint32_t>{ex.detectorAt(2, 1)});
    REQUIRE(o2 == 1);
    // Z on the centre qubit: the two adjacent X checks (generators 1 and 6); Z̄ readout untouched.
    FaultEvent phase = bulk;
    phase.pauliA = 'Z';
    auto [d3, o3] = eventsOf(phase);
    REQUIRE(d3 == std::vector<std::uint32_t>{ex.detectorAt(1, 1), ex.detectorAt(6, 1)});
    REQUIRE(o3 == 0);
    // A flipped syndrome bit of round 1 fires the same check in layers 1 and 2 (a time edge).
    FaultEvent flip;
    flip.afterOp = static_cast<std::uint32_t>(ex.schedule.ops.size() - 1);
    flip.flipBit = ex.syndromeBit(1, 5);
    auto [d4, o4] = eventsOf(flip);
    REQUIRE(d4 == std::vector<std::uint32_t>{ex.detectorAt(5, 1), ex.detectorAt(5, 2)});
    REQUIRE(o4 == 0);
    // Out-of-order faults are refused.
    const std::array<FaultEvent, 2> unordered = {flip, bulk};
    REQUIRE(sampler.run(unordered, rng, bits).error().code == err::BadOptions);
}

TEST_CASE("Sampler: noise plans hold the sites of spec 16 §4") {
    const MemoryExperiment ex = plan("surface_rot_3", 3);
    auto count = [](const NoisePlan& p, SiteKind k) {
        return std::count_if(p.sites.begin(), p.sites.end(),
                             [k](const NoiseSite& s) { return s.kind == k; });
    };
    NoiseParams n;
    n.p = 1e-3;
    n.setting = NoiseSetting::CodeCapacity;
    const NoisePlan cc = planNoise(ex, n).value();
    REQUIRE(cc.sites.size() == 9); // each data qubit once
    REQUIRE(cc.sites[0].px == Approx(1e-3 / 3));
    REQUIRE(cc.sites[0].total() == Approx(1e-3));
    n.setting = NoiseSetting::Phenomenological;
    const NoisePlan ph = planNoise(ex, n).value();
    REQUIRE(count(ph, SiteKind::Pauli1) == 9 * 3); // data errors every round
    REQUIRE(count(ph, SiteKind::RecordFlip) ==
            8 * 3); // q = p on every syndrome bit, none on the readout
    n.q = 0.0;
    REQUIRE(count(planNoise(ex, n).value(), SiteKind::RecordFlip) == 0);
    n.q = -1.0;
    n.setting = NoiseSetting::CircuitLevel;
    const NoisePlan cl = planNoise(ex, n).value();
    REQUIRE(count(cl, SiteKind::Depolarize2) == 24 * 3);     // every CNOT
    REQUIRE(count(cl, SiteKind::RecordFlip) == 8 * 3 + 9);   // every measurement
    REQUIRE(count(cl, SiteKind::Pauli1) == (8 + 8) * 3 + 9); // h and reset per round + data resets
    REQUIRE(std::is_sorted(cl.sites.begin(), cl.sites.end(),
                           [](const auto& a, const auto& b) { return a.afterOp < b.afterOp; }));
    // 15 two-qubit Paulis with p/15 each, 3 letters with p/3, resets flip with p (T09 §5.4).
    const auto faults = enumerateFaults(cl);
    REQUIRE(faults.size() == 24u * 3 * 15 + 8u * 3 * 3 + (8u * 3 + 9) * 1 + (8u * 3 + 9));
    double expectedFaults = 0.0;
    for (const auto& f : faults)
        expectedFaults += f.probability;
    REQUIRE(expectedFaults == Approx(1e-3 * double(cl.sites.size())).epsilon(1e-12));
    // Idle depolarizing (T09 §5.4) adds sites on the qubits a moment leaves untouched.
    n.pIdle = 1e-3;
    REQUIRE(planNoise(ex, n).value().sites.size() > cl.sites.size());
    n.p = 1.5;
    REQUIRE(planNoise(ex, n).error().code == err::BadOptions);
    REQUIRE(krausDepolarizingParameter(0.3, 1) == Approx(0.4));
    REQUIRE(krausDepolarizingParameter(0.15, 2) == Approx(0.16));
}

TEST_CASE("Sampler: fault draws follow the channel probabilities") {
    Schedule s;
    s.qubits = 2;
    s.ops = {{OpKind::CX, 0, 1}};
    NoiseParams n;
    n.p = 0.6;
    const NoisePlan plan = planCircuitNoise(s, n).value();
    REQUIRE(plan.sites.size() == 1);
    core::Random rng(0xD15EA5E);
    std::vector<FaultEvent> faults;
    std::map<std::string, int> histogram;
    const int shots = 60000;
    for (int i = 0; i < shots; ++i) {
        drawFaults(plan, rng, faults);
        if (!faults.empty())
            ++histogram[std::string{faults[0].pauliA, faults[0].pauliB}];
    }
    REQUIRE(histogram.size() == 15);
    REQUIRE(histogram.count("II") == 0);
    // Each pair has probability p/15 = 0.04: mean 2400, σ = sqrt(60000·0.04·0.96) = 48; 5σ band.
    for (const auto& [pair, k] : histogram) {
        INFO(pair);
        REQUIRE(std::abs(k - 2400) < 5 * 48);
    }
}

TEST_CASE("Sampler: the Pauli-frame engine reproduces the stabilizer backend shot by shot") {
    struct Case {
        const char* id;
        NoiseSetting setting;
        double p;
        LogicalBasis basis;
    };
    const Case cases[] = {{"surface_rot_3", NoiseSetting::CircuitLevel, 0.02, LogicalBasis::Z},
                          {"surface_rot_3", NoiseSetting::CircuitLevel, 0.02, LogicalBasis::X},
                          {"surface_rot_5", NoiseSetting::Phenomenological, 0.03, LogicalBasis::Z},
                          {"five_qubit", NoiseSetting::CircuitLevel, 0.03, LogicalBasis::X},
                          {"shor_9", NoiseSetting::CircuitLevel, 0.02, LogicalBasis::Z},
                          {"steane_7", NoiseSetting::CodeCapacity, 0.2, LogicalBasis::Z}};
    for (const Case& c : cases) {
        INFO(c.id << " " << noiseSettingName(c.setting));
        const MemoryExperiment ex = plan(c.id, 3, c.basis);
        NoiseParams n;
        n.setting = c.setting;
        n.p = c.p;
        n.pIdle = c.setting == NoiseSetting::CircuitLevel ? c.p / 2 : 0.0;
        const NoisePlan noise = planNoise(ex, n).value();
        const auto tableau = TableauSampler::create(ex.schedule).value();
        FrameSampler frame(ex.schedule);
        const core::Random master(0xFACADE);
        std::vector<FaultEvent> faults;
        std::vector<std::uint8_t> bits, flips;
        std::size_t shotsWithEvents = 0;
        for (std::uint64_t shot = 0; shot < 120; ++shot) {
            core::Random noiseRng = master.stream(2 * shot), measRng = master.stream(2 * shot + 1);
            drawFaults(noise, noiseRng, faults);
            REQUIRE(tableau.run(faults, measRng, bits).has_value());
            REQUIRE(frame.run(faults, flips).has_value());
            const auto events = ex.detectionEvents(bits);
            REQUIRE(events == ex.detectionEvents(flips));
            REQUIRE(ex.observableValues(bits) == ex.observableValues(flips));
            shotsWithEvents += std::count(events.begin(), events.end(), 1) > 0 ? 1 : 0;
        }
        REQUIRE(shotsWithEvents > 30); // the comparison is not vacuous
    }
}
