// Spec 15 §9 — the estimate record: the JSON schema shown in the spec, verbatim keys, and the
// envelope the report exports.
#include "Runtime/Estimate.hpp"
#include "Data/Fidelity.hpp"

namespace qlab::runtime {
namespace {
constexpr std::string_view kKind = "qlab.estimate";

core::Json qecJson(const qec::ResourceEstimate& q) {
    core::Json j;
    j["distance"] = q.distance;
    j["factory_distance"] = q.factoryDistance;
    j["factories"] = q.factories;
    j["target_logical_error"] = q.targetLogicalError;
    j["logical_error_per_cycle"] = q.logicalErrorPerCycle;
    j["logical_cycles"] = q.logicalCycles;
    j["syndrome_cycles"] = q.syndromeCycles;
    j["data_qubits"] = q.dataQubits;
    j["factory_qubits"] = q.factoryQubits;
    j["physical_qubits"] = q.physicalQubits;
    j["wall_time_s"] = q.wallTimeS;
    j["total_failure_probability"] = q.totalFailureProbability;
    j["factory_limited"] = q.factoryLimited;
    j["class"] = std::string(data::fidelityName(q.cls));
    j["assumptions"] = q.assumptions;
    return j;
}
} // namespace

core::Json Estimate::toJson() const {
    core::Json j;
    j["device"] = device;
    j["calibration_timestamp"] = calibrationTimestamp;
    j["class"] = std::string(data::fidelityName(cls));

    core::Json terms;
    terms["load"] = wallTime.loadS;
    terms["per_shot_s"] = wallTime.perShotS;
    terms["shots"] = wallTime.shots;
    terms["reset"] = wallTime.resetS;
    terms["circuit"] = wallTime.circuitS;
    terms["readout"] = wallTime.readoutS;
    terms["gap"] = wallTime.gapS;
    terms["feedback_total"] = wallTime.feedbackTotalS;
    j["wall_time"] = {{"value_s", wallTime.valueS},
                      {"min_s", wallTime.minS},
                      {"max_s", wallTime.maxS},
                      {"reset_policy", wallTime.resetPolicy == hw::ResetPolicy::Active    ? "active"
                                       : wallTime.resetPolicy == hw::ResetPolicy::Passive ? "passive"
                                                                                          : "cooling"},
                      {"terms", std::move(terms)}};

    core::Json fid;
    fid["fast"] = fidelity.fast;
    fid["min"] = fidelity.low;
    fid["max"] = fidelity.high;
    fid["sigma_log"] = fidelity.sigmaLog;
    fid["gate_product"] = fidelity.gateProduct;
    fid["idle_product"] = fidelity.idleProduct;
    fid["readout_product"] = fidelity.readoutProduct;
    fid["caveats"] = fidelity.caveats;
    if (fidelity.simulated) {
        core::Json s;
        s["classical"] = fidelity.simulated->classical;
        s["hellinger"] = fidelity.simulated->hellinger;
        if (fidelity.simulated->state) s["state"] = *fidelity.simulated->state;
        if (fidelity.simulated->successProbability) s["success_probability"] = *fidelity.simulated->successProbability;
        s["class"] = std::string(data::fidelityName(fidelity.simulated->cls));
        fid["simulated"] = std::move(s);
    } else {
        fid["simulated"] = nullptr;
    }
    j["fidelity"] = std::move(fid);

    j["resources"] = {{"qubits", resources.qubits},
                      {"depth", resources.depth},
                      {"two_qubit", resources.twoQubit},
                      {"t_count", resources.tCount},
                      {"rotations", resources.rotations},
                      {"swaps", resources.swaps},
                      {"classical_bits", resources.classicalBits},
                      {"branches", resources.branches},
                      {"circuit_time_s", resources.circuitTimeS}};

    j["classical_cost"] = {{"statevector_bytes", classicalCost.stateVectorBytes},
                           {"densitymatrix_bytes", classicalCost.densityMatrixBytes},
                           {"stabilizer_bytes", classicalCost.stabilizerBytes},
                           {"estimated_time_s", classicalCost.estimatedTimeS},
                           {"gate_cost_s", classicalCost.gateCostS},
                           {"host_max_qubits", classicalCost.hostMaxQubits},
                           {"crossover_qubits", classicalCost.crossoverQubits}};

    j["qec"] = qec ? qecJson(*qec) : core::Json(nullptr);
    j["assumptions"] = assumptions;

    core::Json comp = core::Json::array();
    for (const DeviceComparison& c : comparison)
        comp.push_back({{"device", c.device}, {"wall_time_s", c.wallTimeS}, {"fidelity_fast", c.fidelityFast}});
    j["comparison"] = std::move(comp);
    return j;
}

std::string Estimate::serialize() const { return core::JsonEnvelope::serialize(kKind, toJson()); }

} // namespace qlab::runtime
