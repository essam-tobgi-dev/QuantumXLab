// Spec 16 §7, T09 §7–§9, T12 §6 — surface-code resource estimation: scaling law (7.1), its
// inversion (7.2), the T09 §9 recipe with 15-to-1 factories, and the sequential-T model of spec 16
// §7.
#include "QEC/Resource.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qec {
namespace {
constexpr std::uint32_t kMaxDistance = 2001;
constexpr double kSlack = 1.0 + 1e-9; // floating-point slack on "p_L(d) ≤ target"

Status requirePositive(double v, std::string_view what) {
    if (!(std::isfinite(v) && v > 0.0))
        return fail(err::BadOptions,
                    std::format("{} must be positive and finite, got {}", what, v));
    return {};
}
} // namespace

double ResourceEstimator::logicalErrorPerCycle(std::uint32_t d, double p) const {
    return model_.A * std::pow(p / model_.pThreshold, (double(d) + 1.0) / 2.0);
}

double ResourceEstimator::factoryQubits(std::uint32_t dF) const {
    return model_.factoryQubitsPerD2 * double(dF) * double(dF);
}
double ResourceEstimator::factoryCyclesPerT(std::uint32_t dF) const {
    return model_.factoryCyclesPerD * double(dF);
}

double ResourceEstimator::distilledError(double pIn, std::uint32_t levels) const {
    double p = pIn;
    for (std::uint32_t l = 0; l < levels; ++l)
        p = std::min(1.0, model_.distillationPrefactor * p * p * p);
    return p;
}

Result<std::uint32_t> ResourceEstimator::requiredDistance(double target, double p) const {
    QXL_TRY(requirePositive(target, "target logical error"));
    QXL_TRY(requirePositive(p, "physical error rate"));
    if (p >= model_.pThreshold)
        return fail(err::AboveThreshold,
                    std::format("physical error rate {} is not below the threshold {}: no distance "
                                "reaches the target (T09 §7)",
                                p, model_.pThreshold));
    // (7.2): d = 2⌈ln(target/A) / ln(p/p_th)⌉ − 1, at least 3.
    const double exponent = std::log(target / model_.A) / std::log(p / model_.pThreshold);
    const double half = std::max(2.0, std::ceil(exponent - 1e-9));
    if (half > (kMaxDistance + 1) / 2.0)
        return fail(err::TooLarge,
                    std::format("target {} needs a distance above {}", target, kMaxDistance));
    return 2u * static_cast<std::uint32_t>(half) - 1u;
}

Result<ResourceEstimate> ResourceEstimator::estimate(const ResourceInput& in) const {
    QXL_TRY(requirePositive(in.physicalErrorRate, "physical error rate"));
    QXL_TRY(requirePositive(in.cycleTimeS, "cycle time"));
    QXL_TRY(requirePositive(in.logicalQubits, "logical qubit count"));
    QXL_TRY(requirePositive(in.failureBudget, "failure budget"));
    QXL_TRY(requirePositive(in.tParallelism, "T-layer parallelism"));
    if (in.tCount < 0.0 || in.cliffordDepth < 0.0 || !(in.tCount + in.cliffordDepth > 0.0))
        return fail(err::BadOptions, "the algorithm needs a positive T count or Clifford depth");
    if (in.physicalErrorRate >= model_.pThreshold)
        return fail(err::AboveThreshold,
                    std::format("physical error rate {} is not below the threshold {}",
                                in.physicalErrorRate, model_.pThreshold));
    ResourceEstimate r;
    // N_cyc may depend on d through the factory throughput, so the smallest consistent odd d is
    // found by scanning: p_L(d) falls with d while the target ε/(Q_L N_cyc(d)) does not.
    for (std::uint32_t d = 3; d <= kMaxDistance; d += 2) {
        const std::uint32_t dF = in.factoryDistance != 0 ? in.factoryDistance : d;
        const double perT = factoryCyclesPerT(dF); // syndrome cycles per |T⟩ per factory
        const auto automatic = static_cast<std::uint32_t>(std::ceil(perT / double(d) - 1e-12));
        const std::uint32_t nF =
            in.factories != 0 ? in.factories : std::max<std::uint32_t>(1u, automatic);
        const double algorithmic = in.tCount + in.cliffordDepth;               // T09 §9 step 1
        const double throughput = in.tCount * perT / (double(nF) * double(d)); // T12 (6.3)
        const double cycles = std::max(algorithmic, throughput) / in.tParallelism;
        const double target =
            in.failureBudget / (in.logicalQubits * cycles); // union bound, T12 (6.1)
        if (logicalErrorPerCycle(d, in.physicalErrorRate) > target * kSlack)
            continue;
        r.distance = d;
        r.factoryDistance = dF;
        r.factories = nF;
        r.logicalCycles = cycles;
        r.targetLogicalError = target;
        r.factoryLimited = throughput > algorithmic;
        break;
    }
    if (r.distance == 0)
        return fail(err::TooLarge,
                    std::format("no distance up to {} meets the failure budget", kMaxDistance));
    r.logicalErrorPerCycle = logicalErrorPerCycle(r.distance, in.physicalErrorRate);
    r.syndromeCycles =
        r.logicalCycles * double(r.distance); // one logical cycle = d rounds (T09 §8.2)
    r.qubitsPerLogical = physicalQubitsPerLogical(r.distance);
    r.dataQubits = model_.routingFactor * in.logicalQubits * r.qubitsPerLogical; // T09 §9 step 3
    r.factoryQubits = in.factoryQubitsOverride
                          ? *in.factoryQubitsOverride
                          : double(r.factories) * factoryQubits(r.factoryDistance);
    r.physicalQubits = r.dataQubits + r.factoryQubits; // step 5
    r.wallTimeS = r.syndromeCycles * in.cycleTimeS;    // step 6
    r.totalFailureProbability = in.logicalQubits * r.logicalCycles * r.logicalErrorPerCycle;
    r.assumptions = {
        "rotated surface code, 2d² − 1 physical qubits per logical qubit",
        std::format("(7.1) heuristic scaling p_L = A (p/p_th)^((d+1)/2) with A = {}, p_th = {} "
                    "(MWPM-class decoding)",
                    model_.A, model_.pThreshold),
        "union bound over logical qubits and logical cycles; one logical cycle = d syndrome rounds",
        std::format("routing overhead factor {} on the data block", model_.routingFactor),
        in.factoryQubitsOverride
            ? std::string("factory space taken from a reference layout")
            : std::format("15-to-1 factories of {} d_f² qubits, one T per {} d_f cycles (T09 §8.3)",
                          model_.factoryQubitsPerD2, model_.factoryCyclesPerD),
        "cycle time from the device (qec.cycle_time_us); errors independent, p = mean two-qubit "
        "gate error"};
    return r;
}

Result<ResourceEstimate> ResourceEstimator::estimateSequential(const SequentialInput& in) const {
    QXL_TRY(requirePositive(in.physicalErrorRate, "physical error rate"));
    QXL_TRY(requirePositive(in.cycleTimeS, "cycle time"));
    QXL_TRY(requirePositive(in.logicalQubits, "logical qubit count"));
    QXL_TRY(requirePositive(in.tCount, "T count"));
    QXL_TRY(requirePositive(in.failureBudget, "failure budget"));
    if (in.factories == 0)
        return fail(err::BadOptions, "the sequential model needs at least one factory");
    if (in.physicalErrorRate >= model_.pThreshold)
        return fail(err::AboveThreshold,
                    std::format("physical error rate {} is not below the threshold {}",
                                in.physicalErrorRate, model_.pThreshold));
    ResourceEstimate r;
    for (std::uint32_t d = 3; d <= kMaxDistance; d += 2) {
        const double cycles = in.tCount * double(d) / double(in.factories); // spec 16 (7.2)
        const double volume = in.logicalQubits * cycles;
        if (volume * logicalErrorPerCycle(d, in.physicalErrorRate) > in.failureBudget * kSlack)
            continue; // step 3
        r.distance = d;
        r.syndromeCycles = cycles;
        r.targetLogicalError = in.failureBudget / volume;
        break;
    }
    if (r.distance == 0)
        return fail(err::TooLarge,
                    std::format("no distance up to {} meets the failure budget", kMaxDistance));
    r.factoryDistance = r.distance;
    r.factories = in.factories;
    r.logicalCycles = in.tCount / double(in.factories);
    r.logicalErrorPerCycle = logicalErrorPerCycle(r.distance, in.physicalErrorRate);
    r.qubitsPerLogical = physicalQubitsPerLogical(r.distance);
    r.dataQubits = in.logicalQubits * r.qubitsPerLogical; // step 4
    r.factoryQubits =
        double(in.factories) * in.factoryQubitsPerD2 * double(r.distance) * double(r.distance);
    r.physicalQubits = r.dataQubits + r.factoryQubits;
    r.wallTimeS = r.syndromeCycles * in.cycleTimeS; // step 5
    r.totalFailureProbability = in.logicalQubits * r.syndromeCycles * r.logicalErrorPerCycle;
    r.factoryLimited = true;
    r.assumptions = {"surface code, rotated, threshold 1 %",
                     "(7.1) heuristic scaling",
                     std::format("sequential T consumption, {} factories", in.factories),
                     std::format("factory footprint {}d²", in.factoryQubitsPerD2),
                     "no routing overhead beyond factories",
                     "T-count from the given Toffoli count",
                     "cycle time from device"};
    return r;
}

Result<double> cycleTimeFromDevice(const core::Json& deviceData) {
    const auto qec = deviceData.find("qec");
    if (qec == deviceData.end() || !qec->is_object())
        return fail(err::BadJson, "device: field 'data.qec' is missing or is not an object");
    const auto t = qec->find("cycle_time_us");
    if (t == qec->end() || !t->is_number() || !(t->get<double>() > 0.0))
        return fail(
            err::BadJson,
            "device: field 'data.qec.cycle_time_us' is missing or is not a positive number");
    return t->get<double>() * 1e-6;
}

Result<double> loadDeviceCycleTime(const std::filesystem::path& deviceJson) {
    QXL_TRY_ASSIGN(const core::Envelope env, core::JsonEnvelope::load(deviceJson, "device"));
    return cycleTimeFromDevice(env.data);
}

} // namespace qlab::qec
