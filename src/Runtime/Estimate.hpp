#pragma once
// Spec 15 §6–§9, T12 — what the same program would cost on the physical device: wall time (6.1),
// fidelity (7.1) and the simulated comparison, resources, classical simulation cost and the QEC
// hook. Every figure is class `Model` and carries the assumption list of T12 §9 verbatim.
#include "Compiler/Types.hpp"
#include "Core/Json.hpp"
#include "Data/Fidelity.hpp"
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include "Hardware/Hardware.hpp"
#include "Lang/Sema.hpp"
#include "QEC/Resource.hpp"
#include "Runtime/Types.hpp"
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace qlab::compiler {
struct CompileOutput;
struct CompileOptions;
} // namespace qlab::compiler

namespace qlab::runtime {

// T12 (1.1) term by term, in seconds. `perShotS` = reset + circuit + readout + gap.
struct WallTimeEstimate {
    double valueS = 0.0;
    double loadS = 0.0, resetS = 0.0, circuitS = 0.0, readoutS = 0.0, gapS = 0.0;
    double perShotS = 0.0, shotsS = 0.0, feedbackTotalS = 0.0;
    std::uint64_t shots = 0;
    hw::ResetPolicy resetPolicy = hw::ResetPolicy::Active;
    double minS = 0.0, maxS = 0.0; // T12 §7 favourable / unfavourable ends
};

// §7 (ii): the noisy-versus-ideal comparison of the output distributions and of the state.
struct SimulatedFidelity {
    double classical = 0.0;      // F_c = (Σ √(p_ideal p_noisy))²   (4.4)
    double hellinger = 0.0;      // H = √(1 − √F_c)
    std::optional<double> state; // ⟨ψ_ideal|ρ_noisy|ψ_ideal⟩ before measurement (Simulator-only)
    std::optional<double> successProbability;
    data::FidelityClass cls = data::FidelityClass::Numerical;
};

struct FidelityEstimate {
    double fast = 1.0;            // (7.1)
    double low = 1.0, high = 1.0; // min–max interval of T12 §7
    double sigmaLog = 0.0;        // 1σ band propagated in the log domain
    double gateProduct = 1.0, idleProduct = 1.0, readoutProduct = 1.0;
    std::optional<SimulatedFidelity> simulated;
    std::vector<std::string> caveats; // spec 15 §7 / T12 §4.2, rendered with the number
    data::FidelityClass cls = data::FidelityClass::Model;
};

// Spec 15 §8 resources, from `PassMetrics` and the pre-decomposition circuit.
struct ResourceSummary {
    std::uint32_t qubits = 0, depth = 0, twoQubit = 0, tCount = 0, rotations = 0;
    std::uint32_t swaps = 0, classicalBits = 0, branches = 0;
    double circuitTimeS = 0.0;
};

// Spec 15 §8 / T12 §3: what the same circuit costs on this workstation.
struct ClassicalCost {
    double stateVectorBytes = 0.0, densityMatrixBytes = 0.0;
    double stabilizerBytes = 0.0;
    double estimatedTimeS = 0.0;     // N_gates · 2^n · c_gate
    double gateCostS = 0.0;          // c_gate: seconds per amplitude update, measured on this host
    std::uint32_t hostMaxQubits = 0; // largest n the state vector fits in memory
    double crossoverQubits = 0.0;    // n* of T12 (3.2), 0 when it does not apply
};

struct DeviceComparison {
    std::string device;
    double wallTimeS = 0.0;
    double fidelityFast = 0.0;
};

// Spec 15 §9 record.
struct Estimate {
    std::string device, calibrationTimestamp;
    data::FidelityClass cls = data::FidelityClass::Model;
    WallTimeEstimate wallTime;
    FidelityEstimate fidelity;
    ResourceSummary resources;
    ClassicalCost classicalCost;
    std::optional<qec::ResourceEstimate> qec;
    std::vector<std::string> assumptions; // keys; `assumptionText` renders each in full
    std::vector<DeviceComparison> comparison;

    core::Json toJson() const;     // the §9 schema
    std::string serialize() const; // core::JsonEnvelope, kind "qlab.estimate"
};

// The sentence rendered under an estimate for an assumption key (T12 §9). Spec 15 §9 puts these in
// `Assets/Lang/estimate_assumptions.json`; the table lives here so the runtime needs no asset.
std::string_view assumptionText(std::string_view key);
std::vector<std::string> assumptionKeys(); // every key of the table, in T12 §9 order

// ---------------------------------------------------------------- inputs
// What the estimators read. `usedQubits`/`measuredQubits` are DEVICE qubit indices.
struct EstimateInput {
    const hw::Device* device = nullptr;
    const hw::Calibration* calibration = nullptr;
    const compiler::CompileOutput* program = nullptr;
    std::uint64_t shots = 1024;
    std::vector<std::uint32_t> usedQubits, measuredQubits;
    double branchesPerShot = 0.0;               // executed feedforward points, mean over shots
    std::optional<hw::ResetPolicy> resetPolicy; // override the device's declared method
    bool includeQec = true;    // §8 hook when the fast fidelity falls below `qecThreshold`
    double qecThreshold = 0.5; // T12 §6 default
    double qecFailureBudget = 1.0e-2;
};

// §6: T_run = T_load + N(T_reset + T_circ + T_ro + T_gap) + N_branches τ_fb  (6.1).
Result<WallTimeEstimate> estimateWallTime(const EstimateInput& in);
// §7 (i): the product model (7.1) with the schedule's idle intervals and the readout fidelities.
Result<FidelityEstimate> estimateFidelityFast(const EstimateInput& in);
// §8: resources from the metrics and the pre-decomposition circuit (T count, non-Clifford
// rotations).
ResourceSummary summarizeResources(const EstimateInput& in);
// §8: classical simulation cost of `qubits` qubits and `gates` gate applications on this host.
ClassicalCost classicalCost(std::uint32_t qubits, std::uint64_t gates, double perShotS = 0.0,
                            std::uint64_t shots = 0);
// Seconds per amplitude update, measured once per process by a short micro-benchmark (T12 §3).
double hostGateCost();
// §8: the fault-tolerant row. `p` is the mean two-qubit error of the used qubits.
Result<qec::ResourceEstimate> estimateQec(const EstimateInput& in, const ResourceSummary& res);

// Everything but the simulated fidelity, which needs a run (spec 15 §7 (ii)).
Result<Estimate> estimate(const EstimateInput& in);

// §9 comparison table: the same program recompiled for every shipped device except `skip`. Devices
// that cannot accept the circuit are left out of the table, not reported as errors.
Result<std::vector<DeviceComparison>> compareDevices(const lang::Program& program,
                                                     const compiler::CompileOptions& options,
                                                     std::uint64_t shots, std::string_view skip,
                                                     std::stop_token stop = {});

} // namespace qlab::runtime
