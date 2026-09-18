#pragma once
// Spec 16 §7, T09 §7–§9, T12 §6 — fault-tolerant resource estimation with the rotated surface code.
// Every figure is of class Model and carries its assumption list.
#include "Core/Json.hpp"
#include "QEC/Types.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace qlab::qec {

// Constants of the scaling model; user-editable, sources in T09 §7 and §8.3.
struct SurfaceCodeModel {
    double A = 0.1;                       // prefactor of (7.1)
    double pThreshold = 1.0e-2;           // p_th
    double routingFactor = 1.5;           // data-block routing space, T09 §9 step 3
    double factoryQubitsPerD2 = 72.0;     // 15-to-1 factory ≈ 12 d_f × 6 d_f physical qubits (T09 §8.3)
    double factoryCyclesPerD = 6.0;       // one |T⟩ every 6 d_f syndrome cycles
    double distillationPrefactor = 35.0;  // p_out ≈ 35 p_in³, T09 (8.1)
};

// T09 §9 recipe / T12 (6.1)–(6.3).
struct ResourceInput {
    double physicalErrorRate = 1.0e-3;    // p: the device's mean two-qubit gate error (spec 15 §8)
    double cycleTimeS = 1.0e-6;           // t_c: one syndrome round, from the device's `qec.cycle_time_us`
    double logicalQubits = 1.0;           // Q_L
    double tCount = 0.0;                  // N_T (or the Toffoli count when Toffoli factories are assumed)
    double cliffordDepth = 0.0;           // D_C
    double failureBudget = 1.0e-2;        // ε
    std::uint32_t factoryDistance = 0;    // d_f; 0 = the data distance d
    std::uint32_t factories = 0;          // N_F; 0 = ⌈6 d_f / d⌉, one T per logical cycle
    double tParallelism = 1.0;            // T gates per logical cycle (T12 (6.3))
    // Factory space taken from a reference layout instead of N_F · 72 d_f² (T12 §6 quotes ≈ 5·10^6
    // physical qubits of Toffoli factories for RSA-2048).
    std::optional<double> factoryQubitsOverride;
};

// Spec 16 §7 model: k factories consumed sequentially at one T per d syndrome cycles each, factory
// footprint 30 d², no routing overhead, V · p_L(d) ≤ P_fail with V in logical-qubit-cycles.
struct SequentialInput {
    double physicalErrorRate = 1.0e-3;
    double cycleTimeS = 1.0e-6;
    double logicalQubits = 1.0;           // Q
    double tCount = 0.0;                  // N_T
    std::uint32_t factories = 10;         // k
    double failureBudget = 1.0e-2;        // P_fail
    double factoryQubitsPerD2 = 30.0;
};

struct ResourceEstimate {
    std::uint32_t distance = 0;
    double targetLogicalError = 0.0;      // p_L^target = ε / (Q_L N_cyc); sequential model: P_fail / V
    double logicalErrorPerCycle = 0.0;    // (7.1) at the chosen distance
    double logicalCycles = 0.0;           // N_cyc, each d syndrome rounds (sequential model: N_T / k)
    double syndromeCycles = 0.0;          // rounds of duration t_c
    std::uint32_t factoryDistance = 0, factories = 0;
    double qubitsPerLogical = 0.0;        // 2d² − 1
    double dataQubits = 0.0, factoryQubits = 0.0, physicalQubits = 0.0;
    double wallTimeS = 0.0;
    double totalFailureProbability = 0.0; // union bound at the chosen distance
    bool factoryLimited = false;          // the factories, not the algorithm, set N_cyc
    FidelityClass cls = FidelityClass::Model;
    std::vector<std::string> assumptions; // displayed verbatim with the estimate
};

class ResourceEstimator {
public:
    explicit ResourceEstimator(SurfaceCodeModel model = {}) : model_(model) {}
    const SurfaceCodeModel& model() const { return model_; }

    // (7.1): p_L(d) ≈ A (p / p_th)^{(d+1)/2} per logical qubit per logical cycle, d odd.
    double logicalErrorPerCycle(std::uint32_t d, double p) const;
    // (7.2): smallest odd d ≥ 3 with p_L(d) ≤ target. err::AboveThreshold when p ≥ p_th.
    Result<std::uint32_t> requiredDistance(double targetLogicalError, double p) const;
    static double physicalQubitsPerLogical(std::uint32_t d) { return 2.0 * d * d - 1.0; }   // T09 §5.1
    double factoryQubits(std::uint32_t factoryDistance) const;          // 72 d_f²
    double factoryCyclesPerT(std::uint32_t factoryDistance) const;      // 6 d_f
    // (8.1) applied `levels` times: 35 p³, then 35 (35 p³)³, …
    double distilledError(double pIn, std::uint32_t levels = 1) const;
    // Spec 16 §7 step 6: 7 T per Toffoli, 4 with CCZ-state catalysis.
    static double tCountFromToffoli(double toffolis, bool cczCatalysis = false) { return toffolis * (cczCatalysis ? 4.0 : 7.0); }

    Result<ResourceEstimate> estimate(const ResourceInput& input) const;
    Result<ResourceEstimate> estimateSequential(const SequentialInput& input) const;

private:
    SurfaceCodeModel model_;
};

// t_c in seconds from the `data` object of a device file: `qec.cycle_time_us` (spec 09 §2).
Result<double> cycleTimeFromDevice(const core::Json& deviceData);
Result<double> loadDeviceCycleTime(const std::filesystem::path& deviceJson);

} // namespace qlab::qec
