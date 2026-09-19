// Spec 16 §6, §9; spec 25 §3.8 — logical-error-rate experiments: single faults are corrected on the
// stabilizer backend, the repetition code reproduces the exact majority-vote rate, p_L falls with
// distance below threshold (circuit level, p = 1e-3) and rises above it, runs are reproducible.
#include "QEC/Experiment.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace qlab;
using namespace qlab::qec;
using Catch::Approx;

namespace {
StabilizerCode shipped(const std::string& id) {
    return loadShippedCode(id).value();
}

std::uint32_t roundStartOp(const MemoryExperiment& ex, std::uint32_t round) {
    for (std::uint32_t i = 0; i < ex.schedule.ops.size(); ++i)
        if (ex.schedule.ops[i].kind == OpKind::RoundStart && ex.schedule.ops[i].a == round)
            return i;
    FAIL("round start not found");
    return 0;
}

// Exact logical error of majority voting over d independent flips of probability p (spec 16 §9).
double majorityVote(std::uint32_t d, double p) {
    double total = 0.0;
    for (std::uint32_t j = d / 2 + 1; j <= d; ++j) {
        double binom = 1.0;
        for (std::uint32_t i = 0; i < j; ++i)
            binom = binom * double(d - i) / double(i + 1);
        total += binom * std::pow(p, j) * std::pow(1.0 - p, d - j);
    }
    return total;
}
} // namespace

TEST_CASE("Experiment: every single-qubit error is corrected on the stabilizer backend (d = 3 "
          "surface code)") {
    const StabilizerCode code = shipped("surface_rot_3");
    for (LogicalBasis basis : {LogicalBasis::Z, LogicalBasis::X})
        for (const char* decoder : {"union_find", "lookup"}) {
            ExtractionOptions x;
            x.rounds = 2;
            x.basis = basis;
            NoiseParams n;
            n.setting = NoiseSetting::Phenomenological; // data errors before each round, q = 0:
                                                        // reliable syndromes
            n.p = 0.01;
            n.q = 0.0;
            const auto runner = ExperimentRunner::create(code, x, n, decoder);
            REQUIRE(runner.has_value());
            auto worker = runner->worker();
            core::Random rng(11);
            for (std::uint32_t round : {0u, 1u})
                for (std::uint32_t q = 0; q < code.n; ++q)
                    for (char letter : {'X', 'Y', 'Z'}) {
                        FaultEvent f;
                        f.afterOp = roundStartOp(runner->experiment(), round);
                        f.qubitA = q;
                        f.pauliA = letter;
                        for (SampleEngine engine :
                             {SampleEngine::Stabilizer, SampleEngine::PauliFrame}) {
                            const auto shot = worker.shotWithFaults({f}, rng, engine);
                            INFO(decoder << " basis " << basisName(basis) << " round " << round
                                         << ": " << letter << " on " << q);
                            REQUIRE(shot.has_value());
                            REQUIRE_FALSE(shot->decoderRejected);
                            REQUIRE_FALSE(shot->failure);
                            // The error is visible unless it acts trivially on the memory basis.
                            const bool visible =
                                letter == 'Y' || (letter == 'X') == (basis == LogicalBasis::Z);
                            if (visible)
                                REQUIRE(std::count(shot->events.begin(), shot->events.end(), 1) >
                                        0);
                        }
                    }
        }
    // Unknown decoders are refused by name, union-find on codes that are not matching-type.
    REQUIRE(ExperimentRunner::create(code, {}, {}, "mwpm").error().code == ErrorCode::Unsupported);
    REQUIRE(ExperimentRunner::create(shipped("steane_7"), {}, {}, "union_find").error().code ==
            err::NotMatchable);
}

TEST_CASE("Experiment: repetition code beats the physical error rate and matches majority voting") {
    // Brief: p = 1e-2, seeded Monte-Carlo on the stabilizer backend, Wilson interval.
    LogicalErrorExperiment e;
    e.code = shipped("repetition_bitflip_3");
    e.noise = NoiseSetting::CodeCapacity;
    e.dataError = DataErrorKind::BitFlip;
    e.p = {1e-2};
    e.samples = 100000;
    e.seed = 20260917;
    for (const char* decoder : {"lookup", "union_find"}) {
        e.decoder = decoder;
        const auto points = runLogicalErrorExperiment(e);
        REQUIRE(points.has_value());
        const LogicalErrorPoint& pt = points->front();
        INFO(decoder << ": failures " << pt.failures << " of " << pt.samples);
        REQUIRE(pt.samples == 100000);
        REQUIRE(pt.rounds == 1);
        REQUIRE(pt.decoderFailures == 0);
        REQUIRE(pt.pL_hi < 1e-2); // whole 95 % interval below the physical rate
        REQUIRE(pt.pL_lo > 0.0);
        // Exact value 3p²(1 − p) + p³ = 2.98e-4 inside the 95 % Wilson interval (spec 16 §9).
        const double exact = majorityVote(3, 1e-2);
        REQUIRE(exact == Approx(2.98e-4).epsilon(1e-12));
        REQUIRE(pt.pL_lo <= exact);
        REQUIRE(exact <= pt.pL_hi);
        REQUIRE(pt.perRound == Approx(pt.pL).epsilon(1e-12)); // one round
    }
    // Union-find on the generated d = 5 and d = 7 codes at p = 5 %.
    for (std::uint32_t d : {5u, 7u}) {
        e.code = makeRepetitionCode(d).value();
        e.decoder = "union_find";
        e.p = {0.05};
        e.engine = SampleEngine::PauliFrame;
        const auto pt = runLogicalErrorExperiment(e).value().front();
        const double exact = majorityVote(d, 0.05);
        INFO("d = " << d << ": " << pt.failures << " failures, exact " << exact);
        REQUIRE(pt.pL_lo <= exact);
        REQUIRE(exact <= pt.pL_hi);
    }
    // The phase-flip code protects the X memory against Z errors in the same way.
    e.code = shipped("repetition_phaseflip_3");
    e.basis = LogicalBasis::X;
    e.dataError = DataErrorKind::PhaseFlip;
    e.p = {1e-2};
    e.decoder = "lookup";
    const auto phase = runLogicalErrorExperiment(e).value().front();
    REQUIRE(phase.pL_hi < 1e-2);
    REQUIRE(phase.pL_lo <= majorityVote(3, 1e-2));
    REQUIRE(majorityVote(3, 1e-2) <= phase.pL_hi);
}

TEST_CASE("Experiment: both engines and both schedules of the workers give identical failures") {
    LogicalErrorExperiment e;
    e.code = shipped("surface_rot_3");
    e.noise = NoiseSetting::CircuitLevel;
    e.p = {8e-3};
    e.samples = 1500;
    e.seed = 77;
    const auto tableau = runLogicalErrorExperiment(e).value().front();
    e.engine = SampleEngine::PauliFrame;
    const auto frame = runLogicalErrorExperiment(e).value().front();
    e.parallel = false;
    const auto serial = runLogicalErrorExperiment(e).value().front();
    REQUIRE(tableau.failures > 0);
    REQUIRE(tableau.failures == frame.failures);
    REQUIRE(frame.failures == serial.failures);
    REQUIRE(tableau.rounds == 3); // one logical cycle = d rounds (T09 §5.3)
    // Another seed is another sample.
    e.seed = 78;
    REQUIRE(runLogicalErrorExperiment(e).value().front().failures != frame.failures);
    // Per-round rate, spec 16 §6: ε = ½(1 − (1 − 2 p_L)^{1/r}).
    REQUIRE(perRoundErrorRate(0.1, 2) == Approx(0.5 * (1.0 - std::sqrt(0.8))).epsilon(1e-14));
    REQUIRE(perRoundErrorRate(0.3, 1) == Approx(0.3).epsilon(1e-14));
    REQUIRE(perRoundErrorRate(0.7, 5) == 0.5);
    REQUIRE(runLogicalErrorExperiment(LogicalErrorExperiment{}).error().code == err::BadOptions);
}

TEST_CASE("Experiment: surface code d = 5 beats d = 3 under circuit-level noise at p = 1e-3") {
    // Spec 25 §3.8: p_L(d = 5) < p_L(d = 3) < p. One logical cycle (d rounds) per shot, union-find
    // on the detector error model, 10^5 seeded shots per distance on the Pauli-frame engine.
    LogicalErrorExperiment e;
    e.noise = NoiseSetting::CircuitLevel;
    e.p = {1e-3};
    e.samples = 100000;
    e.seed = 1603;
    e.decoder = "union_find";
    e.engine = SampleEngine::PauliFrame;
    LogicalErrorPoint point[2];
    const std::uint32_t distance[2] = {3, 5};
    for (int i = 0; i < 2; ++i) {
        e.code = shipped("surface_rot_" + std::to_string(distance[i]));
        point[i] = runLogicalErrorExperiment(e).value().front();
        REQUIRE(point[i].rounds == distance[i]);
        REQUIRE(point[i].decoderFailures == 0);
        // The same seeded series on the stabilizer backend fails in exactly the same shots: checked
        // on its first 3000 shots, which keeps the run time bounded.
        LogicalErrorExperiment head = e;
        head.samples = 3000;
        const auto frame = runLogicalErrorExperiment(head).value().front();
        head.engine = SampleEngine::Stabilizer;
        const auto tableau = runLogicalErrorExperiment(head).value().front();
        REQUIRE(tableau.failures == frame.failures);
    }
    INFO("d = 3: " << point[0].failures << " failures, d = 5: " << point[1].failures << " of "
                   << e.samples);
    REQUIRE(point[0].failures > 0);
    REQUIRE(point[1].pL_hi < point[0].pL_lo);       // 95 % Wilson intervals are disjoint
    REQUIRE(point[1].perRound < point[0].perRound); // Λ = ε_L(3)/ε_L(5) > 1
    REQUIRE(perRoundErrorRate(point[0].pL_hi, 3) <
            1e-3); // per round, d = 3 already beats the physical rate
}
