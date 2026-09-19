// Spec 16 §7, T09 §7–§9, T12 §6; spec 25 §3.8 — the resource estimator reproduces the hand-computed
// numbers of the theory documents and of the spec table to the digits they state.
#include "QEC/Resource.hpp"
#include "Core/Paths.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace qlab;
using namespace qlab::qec;
using Catch::Approx;

namespace {
// x rounded to `digits` significant digits — the precision a document states a number with.
double sig(double x, int digits) {
    if (x == 0.0)
        return 0.0;
    const double scale = std::pow(10.0, digits - 1 - int(std::floor(std::log10(std::abs(x)))));
    return std::round(x * scale) / scale;
}
constexpr double kHour = 3600.0, kYear = 365.25 * 24 * 3600.0;
} // namespace

TEST_CASE("Resource: scaling law (7.1), its inversion (7.2) and the T09 §7 table") {
    const ResourceEstimator est;
    REQUIRE(est.model().A == 0.1);
    REQUIRE(est.model().pThreshold == 1e-2);
    // T09 §7 worked numbers at p = 1e-3: p_L per cycle and 2d² − 1.
    struct Row {
        std::uint32_t d;
        double pL;
        double qubits;
    };
    const Row table[] = {{3, 1e-3, 17},    {5, 1e-4, 49},     {7, 1e-5, 97},    {9, 1e-6, 161},
                         {11, 1e-7, 241},  {13, 1e-8, 337},   {15, 1e-9, 449},  {17, 1e-10, 577},
                         {21, 1e-12, 881}, {25, 1e-14, 1249}, {27, 1e-15, 1457}};
    for (const Row& r : table) {
        INFO("d = " << r.d);
        REQUIRE(est.logicalErrorPerCycle(r.d, 1e-3) == Approx(r.pL).epsilon(1e-9));
        REQUIRE(ResourceEstimator::physicalQubitsPerLogical(r.d) == r.qubits);
        // (7.2) inverts (7.1): the table's own p_L needs exactly its d, a slightly smaller target d
        // + 2.
        REQUIRE(est.requiredDistance(r.pL, 1e-3).value() == r.d);
        REQUIRE(est.requiredDistance(r.pL * 0.99, 1e-3).value() == r.d + 2);
        REQUIRE(est.requiredDistance(r.pL * 9.9, 1e-3).value() == std::max(3u, r.d));
    }
    REQUIRE(est.requiredDistance(0.5, 1e-3).value() == 3); // never below d = 3
    REQUIRE(est.requiredDistance(1e-15, 2e-2).error().code == err::AboveThreshold);
    REQUIRE(est.requiredDistance(-1.0, 1e-3).error().code == err::BadOptions);
    // T09 §8.3: 15-to-1 distillation, p = 1e-3 → 3.5e-8 after one level, 1.5e-21 after two.
    REQUIRE(sig(est.distilledError(1e-3, 1), 2) == Approx(3.5e-8));
    REQUIRE(sig(est.distilledError(1e-3, 2), 2) == Approx(1.5e-21));
    // A d_f = 9 factory: 72 · 81 = 5 832 qubits, one T every 54 cycles.
    REQUIRE(est.factoryQubits(9) == 5832.0);
    REQUIRE(est.factoryCyclesPerT(9) == 54.0);
    REQUIRE(ResourceEstimator::tCountFromToffoli(2.7e9) ==
            Approx(1.89e10)); // 7 T per Toffoli (spec 16 §7)
    REQUIRE(ResourceEstimator::tCountFromToffoli(2.7e9, true) == Approx(1.08e10));
}

TEST_CASE("Resource: T09 §9 worked example B (phase estimation)") {
    ResourceInput in;
    in.logicalQubits = 14;
    in.tCount = 2e4;
    in.cliffordDepth = 1e4;
    in.physicalErrorRate = 1e-3;
    in.failureBudget = 1e-2;
    in.factoryDistance = 9;
    in.cycleTimeS = 1e-6;
    const auto r = ResourceEstimator().estimate(in);
    REQUIRE(r.has_value());
    REQUIRE(r->logicalCycles == 3e4);
    REQUIRE(sig(r->targetLogicalError, 2) == Approx(2.4e-8));
    REQUIRE(r->distance == 13);
    REQUIRE(r->logicalErrorPerCycle == Approx(1e-8).epsilon(1e-9));
    REQUIRE(r->qubitsPerLogical == 337);
    REQUIRE(r->dataQubits == Approx(14 * 337 * 1.5));
    REQUIRE(sig(r->dataQubits, 2) == 7100); // "≈ 7 100 qubits"
    REQUIRE(r->factories == 5);             // a T every 54 cycles, one needed every 13
    REQUIRE(r->factoryQubits == 29160);
    REQUIRE(sig(r->physicalQubits, 2) == 36000);  // "≈ 36 000 physical qubits"
    REQUIRE(sig(r->wallTimeS, 1) == Approx(0.4)); // 3e4 × 13 × 1 µs
    REQUIRE(r->wallTimeS == Approx(0.39).epsilon(1e-12));
    REQUIRE_FALSE(r->factoryLimited);
    REQUIRE(r->totalFailureProbability <= in.failureBudget);
    REQUIRE(r->cls == FidelityClass::Model);
    REQUIRE(r->assumptions.size() >= 5);
    // A single factory cannot keep up: 54/13 cycles per T stretch the run by that factor.
    in.factories = 1;
    const auto starved = ResourceEstimator().estimate(in).value();
    REQUIRE(starved.factoryLimited);
    REQUIRE(starved.logicalCycles == Approx(2e4 * 54.0 / starved.distance));
    REQUIRE(starved.factoryQubits == 5832);
}

TEST_CASE("Resource: RSA-2048 worked example of T09 §9 and T12 §6") {
    // Inputs as stated: Q_L = 6 189, N_Toffoli = 2.7e9 treated as N_cyc, ε = 0.06.
    ResourceInput in;
    in.logicalQubits = 6189;
    in.tCount = 2.7e9;
    in.failureBudget = 0.06;
    in.physicalErrorRate = 1e-3;
    in.cycleTimeS = 1e-6;
    const ResourceEstimator est;
    const auto transmon = est.estimate(in).value();
    REQUIRE(sig(transmon.targetLogicalError, 2) == Approx(3.6e-15)); // T12: 3.6e-15 (T09: ≈ 4e-15)
    REQUIRE(sig(transmon.targetLogicalError, 1) == Approx(4e-15));
    REQUIRE(transmon.distance == 27);
    REQUIRE(transmon.logicalErrorPerCycle == Approx(1e-15).epsilon(1e-9));
    REQUIRE(sig(transmon.dataQubits, 3) == Approx(1.35e7));    // 1.5 × 6189 × 1457
    REQUIRE(sig(transmon.wallTimeS, 2) == Approx(7.3e4));      // 2.7e9 × 27 × 1 µs
    REQUIRE(sig(transmon.wallTimeS / kHour, 2) == Approx(20)); // ≈ 20 h
    REQUIRE(transmon.factories == 6);                          // ⌈6 d_f / d⌉ with d_f = d
    REQUIRE(transmon.factoryQubits == 6 * 72 * 27 * 27);
    // With the reference's Toffoli-factory space (≈ 5e6 qubits): ∼ 2e7 physical qubits.
    in.factoryQubitsOverride = 5e6;
    REQUIRE(sig(est.estimate(in).value().physicalQubits, 1) == Approx(2e7));
    // The reference's 2.5 Toffolis per cycle window bring 20 h down to ≈ 8 h.
    in.tParallelism = 2.5;
    REQUIRE(sig(est.estimate(in).value().wallTimeS / kHour, 1) == Approx(8));
    // Ion-trap class: p = 1e-4 gives d = 13; t_c = 1 ms stretches the same cycle count to ≈ 1.1 yr.
    in.tParallelism = 1.0;
    in.physicalErrorRate = 1e-4;
    in.cycleTimeS = 1e-3;
    in.factoryQubitsOverride =
        5e6 * (13.0 * 13.0) / (27.0 * 27.0); // reference footprint scaled with d²
    const auto ion = est.estimate(in).value();
    REQUIRE(ion.distance == 13);
    REQUIRE(sig(ion.physicalQubits, 1) == Approx(4e6));
    REQUIRE(sig(ion.wallTimeS, 2) == Approx(3.5e7));
    REQUIRE(sig(ion.wallTimeS / kYear, 2) == Approx(1.1));
    in.physicalErrorRate = 2e-2;
    REQUIRE(est.estimate(in).error().code == err::AboveThreshold);
}

TEST_CASE("Resource: the spec 16 §7 table (sequential T consumption, 30 d² factories)") {
    SequentialInput in;
    in.logicalQubits = 6250; // Q = 3n + 100
    in.tCount = 1.9e10;      // 2.7e9 Toffolis × 7 T
    in.factories = 10;
    in.failureBudget = 0.01;
    const ResourceEstimator est;
    REQUIRE(sig(ResourceEstimator::tCountFromToffoli(2.7e9), 2) == Approx(1.9e10));
    in.physicalErrorRate = 1e-3;
    in.cycleTimeS = 1e-6;
    const auto transmon = est.estimateSequential(in).value();
    REQUIRE(transmon.distance == 31);
    REQUIRE(transmon.logicalErrorPerCycle == Approx(1e-17).epsilon(1e-9));
    REQUIRE(transmon.dataQubits == 6250.0 * 1921.0);
    REQUIRE(transmon.factoryQubits == 10.0 * 28830.0);
    REQUIRE(sig(transmon.physicalQubits, 3) == Approx(1.23e7));
    REQUIRE(sig(transmon.wallTimeS, 2) == Approx(5.9e4));
    REQUIRE(sig(transmon.wallTimeS / kHour, 2) == Approx(16));
    REQUIRE(transmon.totalFailureProbability <= 0.01);
    in.physicalErrorRate = 1e-4;
    in.cycleTimeS = 1e-3;
    const auto ion = est.estimateSequential(in).value();
    REQUIRE(ion.distance == 15);
    REQUIRE(ion.logicalErrorPerCycle == Approx(1e-17).epsilon(1e-9));
    REQUIRE(ion.dataQubits == 6250.0 * 449.0);
    REQUIRE(ion.factoryQubits == 10.0 * 6750.0);
    REQUIRE(sig(ion.physicalQubits, 2) == Approx(2.9e6));
    REQUIRE(sig(ion.wallTimeS, 2) == Approx(2.9e7));
    REQUIRE(sig(ion.wallTimeS / kYear, 1) == Approx(0.9));
    REQUIRE(ion.assumptions.size() == 7); // the seven sentences of spec 16 §7
    // d = 29 (resp. 13) would overrun the budget: the chosen distance is the smallest one.
    REQUIRE(6250.0 * 1.9e10 * 29 / 10 * est.logicalErrorPerCycle(29, 1e-3) > 0.01);
    REQUIRE(6250.0 * 1.9e10 * 13 / 10 * est.logicalErrorPerCycle(13, 1e-4) > 0.01);
}

TEST_CASE("Resource: cycle time comes from the device file") {
    const auto devices = core::assetDir() / "Devices";
    REQUIRE(loadDeviceCycleTime(devices / "sc_tunable_grid_54" / "device.json").value() ==
            Approx(0.8e-6));
    REQUIRE(loadDeviceCycleTime(devices / "sc_heavyhex_27" / "device.json").value() ==
            Approx(1.0e-6));
    REQUIRE(loadDeviceCycleTime(devices / "ion_chain_11" / "device.json").value() ==
            Approx(5.0e-3));
    const auto missing = cycleTimeFromDevice(core::Json::object());
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().message.find("data.qec") != std::string::npos);
    REQUIRE(cycleTimeFromDevice(core::Json{{"qec", {{"cycle_time_us", 2.5}}}}).value() ==
            Approx(2.5e-6));
}
