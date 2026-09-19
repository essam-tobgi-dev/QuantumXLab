// Spec 16 §6, §9 — threshold behaviour and small-code experiments: below threshold distance helps,
// above it hurts; distance-3 codes with the lookup decoder beat the physical rate; a custom noise
// plan (per-site probabilities, spec 16 §4 "device calibration") drives the same machinery.
#include "QEC/Experiment.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qlab;
using namespace qlab::qec;
using Catch::Approx;

namespace {
StabilizerCode shipped(const std::string& id) {
    return loadShippedCode(id).value();
}
} // namespace

TEST_CASE("Experiment: code-capacity threshold of the surface code with union-find") {
    // Independent X flips (the model behind the 10.3 % / 9.9 % references of spec 16 §6). Below
    // threshold distance helps, above it hurts, and the crossings of consecutive distances climb
    // towards the union-find threshold from below (finite-size drift).
    ThresholdSweep sweep;
    for (std::uint32_t d : {3u, 5u, 7u})
        sweep.codes.push_back(shipped("surface_rot_" + std::to_string(d)));
    sweep.settings.noise = NoiseSetting::CodeCapacity;
    sweep.settings.dataError = DataErrorKind::BitFlip;
    sweep.settings.p = {0.04, 0.07, 0.09, 0.11, 0.14};
    sweep.settings.samples = 20000;
    sweep.settings.seed = 99;
    sweep.settings.decoder = "union_find";
    sweep.settings.engine = SampleEngine::PauliFrame;
    const auto result = runThresholdSweep(sweep);
    REQUIRE(result.has_value());
    REQUIRE(result->curves.size() == 3);
    const auto& d3 = result->curves[0].points;
    const auto& d5 = result->curves[1].points;
    const auto& d7 = result->curves[2].points;
    REQUIRE(result->curves[2].d == 7);
    // p = 4 %: intervals ordered d7 < d5 < d3. p = 14 %: reversed.
    REQUIRE(d7.front().pL_hi < d5.front().pL_lo);
    REQUIRE(d5.front().pL_hi < d3.front().pL_lo);
    REQUIRE(d7.back().pL_lo > d5.back().pL_hi);
    REQUIRE(d5.back().pL_lo > d3.back().pL_hi);
    for (const auto& curve : result->curves)
        for (std::size_t i = 1; i < curve.points.size(); ++i)
            REQUIRE(curve.points[i].pL > curve.points[i - 1].pL);
    REQUIRE(result->crossing.has_value());
    INFO("crossing " << *result->crossing);
    REQUIRE(*result->crossing > 0.07);
    REQUIRE(*result->crossing < 0.103); // below the MWPM reference
    REQUIRE(result->reference == Approx(0.103));
    REQUIRE(result->cls == FidelityClass::Statistical);
    // Depolarizing data errors put 2p/3 on the X component: same curve at p → 1.5 p.
    sweep.settings.dataError = DataErrorKind::Depolarizing;
    sweep.settings.p = {0.06};
    sweep.codes.resize(2);
    const auto depolarizing = runThresholdSweep(sweep).value();
    REQUIRE(depolarizing.reference == Approx(0.1545));
    const auto& dep3 = depolarizing.curves[0].points.front();
    REQUIRE(dep3.pL_lo < d3.front().pL_hi); // p_X = 0.04 in both
    REQUIRE(d3.front().pL_lo < dep3.pL_hi);
}

TEST_CASE("Experiment: small codes with the lookup decoder beat the physical error rate (code "
          "capacity)") {
    // Depolarizing p = 1 %: a distance-3 code fails only on ≥ 2 errors, p_L = O(p²) ≪ p. The
    // five-qubit code (not CSS) is prepared by its explicit encoder; the others by projection.
    for (const char* id : {"five_qubit", "steane_7", "shor_9"})
        for (LogicalBasis basis : {LogicalBasis::Z, LogicalBasis::X}) {
            LogicalErrorExperiment e;
            e.code = shipped(id);
            e.noise = NoiseSetting::CodeCapacity;
            e.p = {1e-2};
            e.samples = 40000;
            e.seed = 4242;
            e.basis = basis;
            e.engine = SampleEngine::PauliFrame;
            const auto pt = runLogicalErrorExperiment(e).value().front();
            INFO(id << " basis " << basisName(basis) << ": " << pt.failures << " failures");
            REQUIRE(pt.decoderFailures == 0);
            REQUIRE(pt.failures > 0);
            REQUIRE(pt.pL_hi < 1e-2 / 4);
        }
}

TEST_CASE("Experiment: a custom noise plan drives the runner (per-site probabilities)") {
    // Only one site is noisy: the syndrome bit of check 5 in round 1 flips with probability 1/2.
    const StabilizerCode code = shipped("surface_rot_3");
    ExtractionOptions x;
    x.rounds = 3;
    MemoryExperiment ex = planMemoryExperiment(code, x).value();
    NoisePlan plan;
    for (std::uint32_t i = 0; i < ex.schedule.ops.size(); ++i)
        if (ex.schedule.ops[i].kind == OpKind::Measure &&
            ex.schedule.ops[i].bit == ex.syndromeBit(1, 5))
            plan.sites.push_back({i, SiteKind::RecordFlip, ex.syndromeBit(1, 5), 0, 0.5});
    REQUIRE(plan.sites.size() == 1);
    const auto runner = ExperimentRunner::fromPlan(code, ex, plan, "union_find").value();
    REQUIRE(runner.graph()->edges.size() == 1); // one time-like edge, probability 1/2
    REQUIRE(runner.graph()->edges[0].probability == Approx(0.5));
    auto worker = runner.worker();
    const core::Random master(31);
    std::size_t flipped = 0;
    for (std::uint64_t i = 0; i < 400; ++i) {
        const ShotResult shot = worker.shot(master, i, SampleEngine::Stabilizer).value();
        REQUIRE_FALSE(shot.failure); // a measurement fault never touches the logical readout
        REQUIRE(shot.faults.size() == (shot.correction.edges.empty() ? 0u : 1u));
        flipped += shot.faults.size();
    }
    REQUIRE(flipped > 150); // Binomial(400, 1/2): mean 200, σ = 10
    REQUIRE(flipped < 250);
    // A plan made for another schedule is refused.
    NoisePlan foreign;
    foreign.sites.push_back({1u << 30, SiteKind::RecordFlip, 0, 0, 0.1});
    REQUIRE(ExperimentRunner::fromPlan(code, ex, foreign, "union_find").error().code ==
            err::BadOptions);
    REQUIRE(noiseSettingFromName("phenomenological") == NoiseSetting::Phenomenological);
    REQUIRE_FALSE(noiseSettingFromName("thermal").has_value());
}
