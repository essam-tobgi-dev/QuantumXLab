// Spec 15 §6, §9 / T12 (1.1), (1.3), §7, §9 — the hardware wall-time estimate, the resource
// summary and the assumption table rendered verbatim under every estimate.
#include "Runtime/Estimate.hpp"
#include "Compiler/Pass.hpp"
#include "Runtime/Plan.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::runtime {
namespace {

// T12 §9, in order. The keys are what `Estimate::assumptions` carries; the sentences are the
// ones the panel renders (spec 15 §9 keeps the same text in Assets/Lang/estimate_assumptions.json).
struct Assumption { std::string_view key, text; };
constexpr std::array kAssumptions{
    Assumption{"calibration_static",
               "Gate durations, error rates, coherence times and readout parameters are those of the "
               "selected device's calibration file at its timestamp; drift is not modelled."},
    Assumption{"asap_critical_path",
               "Circuit time is the ASAP critical path of the compiled circuit; the control system is "
               "assumed to execute parallel gates simultaneously."},
    Assumption{"reset_policy",
               "Reset uses the device's declared method; passive reset waits 5 T1."},
    Assumption{"queue_time_excluded",
               "Job overhead and inter-shot gap are fixed constants from the device file; queue time is "
               "not included."},
    Assumption{"single_job_no_batching",
               "The shots are one job on an idle control system; no batching with other circuits."},
    Assumption{"independent_errors",
               "Errors are independent; gate errors are depolarizing at the randomized-benchmarking rate; "
               "idle errors follow the thermal-relaxation average infidelity; readout errors use the "
               "assignment matrix diagonal."},
    Assumption{"average_gate_fidelities",
               "Gate errors are average (RB) infidelities, so state-dependent and coherent errors are not "
               "resolved."},
    Assumption{"no_crosstalk",
               "Coherent, correlated and leakage errors are not captured by the product model; the "
               "simulated fidelity, when shown, includes only those present in the noise model."},
    Assumption{"qec_surface_code",
               "Error-corrected figures use the rotated surface code, MWPM-class decoding, A = 0.1, "
               "p_th = 1 %, one logical cycle = d syndrome rounds, 15-to-1 (or Toffoli) factories with the "
               "footprint and rate of T09 §8.3, and a routing overhead factor 1.5."},
    Assumption{"classical_bandwidth_model",
               "Classical simulation time uses the memory-bandwidth model of T11 §11 with this "
               "workstation's measured gate cost."},
};

// Calibration triples carry (value, 1σ); T12 §7 takes the favourable / unfavourable ends at ∓2σ.
enum class End { Typical, Favourable, Unfavourable };
double at(double value, double sigma, End end, bool largerIsWorse) {
    if (end == End::Typical) return value;
    const bool up = (end == End::Unfavourable) == largerIsWorse;
    return std::max(0.0, value + (up ? 2.0 : -2.0) * sigma);
}

double duration1q(const hw::Calibration& cal, std::span<const std::uint32_t> qubits, End end) {
    double t = 0.0;
    for (std::uint32_t q : qubits)
        if (const hw::QubitCal* c = cal.qubit(q))
            t = std::max(t, at(c->duration1q.value.v, c->duration1q.sigma.v, end, true));
    return t;
}

double maxT1(const hw::Calibration& cal, std::span<const std::uint32_t> qubits, End end) {
    double t = 0.0;
    for (std::uint32_t q : qubits)
        if (const hw::QubitCal* c = cal.qubit(q)) t = std::max(t, at(c->t1.value.v, c->t1.sigma.v, end, true));
    return t;
}

// Spec 15 §6 T_ro: max over the measured qubits of the calibrated readout duration, plus the
// device's ring-down.
double readoutTime(const hw::Device& dev, const hw::Calibration& cal, std::span<const std::uint32_t> measured, End end) {
    if (measured.empty()) return 0.0;
    double t = 0.0;
    for (std::uint32_t q : measured)
        if (const hw::QubitCal* c = cal.qubit(q))
            t = std::max(t, at(c->readoutDuration.value.v, c->readoutDuration.sigma.v, end, true));
    if (t == 0.0) t = dev.timing.readout.v;
    return t + dev.timing.readoutRingdown.v;
}

// T12 §1.3: the three reset methods.
double resetTime(const hw::Device& dev, const hw::Calibration& cal, hw::ResetPolicy policy,
                 std::span<const std::uint32_t> used, End end) {
    switch (policy) {
    case hw::ResetPolicy::Active:
        return dev.activeResetDuration(units::Time{duration1q(cal, used, end)}).v;
    case hw::ResetPolicy::Passive:
        return std::max(dev.control.passiveMultiplier * maxT1(cal, used, end), dev.timing.repetitionDelay.v);
    case hw::ResetPolicy::Cooling: return dev.control.coolingTime.v;
    }
    return 0.0;
}

WallTimeEstimate evaluate(const EstimateInput& in, hw::ResetPolicy policy, double circuitS, End end) {
    const hw::Device& dev = *in.device;
    const hw::Calibration& cal = *in.calibration;
    WallTimeEstimate w;
    w.shots = in.shots;
    w.resetPolicy = policy;
    w.loadS = dev.control.loadTime.v;
    w.resetS = resetTime(dev, cal, policy, in.usedQubits, end);
    w.circuitS = circuitS;
    w.readoutS = readoutTime(dev, cal, in.measuredQubits, end);
    w.gapS = dev.control.repOverhead.v;
    w.perShotS = w.resetS + w.circuitS + w.readoutS + w.gapS;
    w.shotsS = w.perShotS * static_cast<double>(in.shots);
    // (6.1) last term: the feedback latency of every executed Branch / Loop iteration.
    w.feedbackTotalS = in.branchesPerShot * static_cast<double>(in.shots) * dev.control.feedbackLatency.v;
    w.valueS = w.loadS + w.shotsS + w.feedbackTotalS;
    return w;
}
} // namespace

std::string_view assumptionText(std::string_view key) {
    const std::size_t colon = key.find(':'); // "reset_policy:active" renders the reset_policy sentence
    const std::string_view base = colon == std::string_view::npos ? key : key.substr(0, colon);
    for (const Assumption& a : kAssumptions)
        if (a.key == base) return a.text;
    return {};
}

std::vector<std::string> assumptionKeys() {
    std::vector<std::string> keys;
    keys.reserve(kAssumptions.size());
    for (const Assumption& a : kAssumptions) keys.emplace_back(a.key);
    return keys;
}

Result<WallTimeEstimate> estimateWallTime(const EstimateInput& in) {
    if (!in.device || !in.calibration || !in.program)
        return fail(err::Unsupported, "the wall-time estimate needs a device, a calibration and a compiled program");
    const hw::ResetPolicy policy = in.resetPolicy.value_or(in.device->control.resetPolicy);
    // T_circ from the compiled schedule (T12 (1.2)); measurements are charged through T_ro.
    const double circuitS =
        static_cast<double>(circuitCriticalPath(in.program->circuit, in.program->timing).get()) * 1e-12;
    WallTimeEstimate w = evaluate(in, policy, circuitS, End::Typical);
    w.minS = evaluate(in, policy, circuitS, End::Favourable).valueS;
    w.maxS = evaluate(in, policy, circuitS, End::Unfavourable).valueS;
    return w;
}

ResourceSummary summarizeResources(const EstimateInput& in) {
    ResourceSummary r;
    if (!in.program) return r;
    const compiler::CompileOutput& p = *in.program;
    r.qubits = static_cast<std::uint32_t>(in.usedQubits.empty() ? usedQubits(p.circuit).size() : in.usedQubits.size());
    r.depth = p.metrics.depth;
    r.twoQubit = p.metrics.twoQubitCount;
    r.swaps = p.metrics.swapCount;
    r.classicalBits = p.circuit.clbitCount();
    r.circuitTimeS = static_cast<double>(circuitCriticalPath(p.circuit, p.timing).get()) * 1e-12;

    // Spec 15 §8: T count on the PRE-decomposition circuit (`t`, `tdg`, R_z(±π/4)); every other
    // non-Clifford rotation angle is reported separately.
    const double quarter = std::numbers::pi / 4.0;
    const auto walk = [&](const ir::Circuit& c, auto&& self) -> void {
        for (ir::NodeId id : c.topologicalOrder()) {
            const ir::Node& n = c.node(id);
            if (const auto* g = std::get_if<ir::Gate>(&n)) {
                if (g->name == "t" || g->name == "tdg") {
                    ++r.tCount;
                } else if (g->params.size() == 1 && (g->name == "rz" || g->name == "p" || g->name == "u1")) {
                    const double k = g->params[0] / quarter;
                    const double rounded = std::round(k);
                    if (std::abs(k - rounded) > 1e-9) ++r.rotations;                       // arbitrary angle
                    else if (std::abs(std::fmod(std::abs(rounded), 2.0) - 1.0) < 1e-9) ++r.tCount; // ±π/4 (mod π/2)
                } else if (!g->params.empty() && (g->name == "rx" || g->name == "ry")) {
                    const double k = g->params[0] / (std::numbers::pi / 2.0);
                    if (std::abs(k - std::round(k)) > 1e-9) ++r.rotations;
                }
            } else if (std::holds_alternative<ir::Branch>(n) || std::holds_alternative<ir::Loop>(n)) {
                ++r.branches;
                if (const auto* b = std::get_if<ir::Branch>(&n)) {
                    if (b->thenBody.present()) self(*b->thenBody, self);
                    if (b->elseBody.present()) self(*b->elseBody, self);
                } else if (const auto* l = std::get_if<ir::Loop>(&n)) {
                    if (l->body.present()) self(*l->body, self);
                }
            } else if (const auto* bx = std::get_if<ir::Box>(&n)) {
                if (bx->body.present()) self(*bx->body, self);
            }
        }
    };
    walk(p.source.nodeCount() > 0 ? p.source : p.circuit, walk);
    return r;
}

} // namespace qlab::runtime
