#include "Hardware/FrequencyPlan.hpp"
#include "Hardware/Loader.hpp"
#include "Hardware/Transmon.hpp"
#include <cmath>
#include <format>

namespace qlab::hw {
using core::Json;

Result<LoadedDevice> loadDevice(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir / "device.json")) return fail(ErrorCode::Hardware_ + 6, std::format("no device.json in {}", dir.string()));
    auto dev = loadDeviceJson(dir / "device.json");
    if (!dev) return std::unexpected(dev.error());
    auto cal = loadCalibrationJson(dir / "calibration.json", *dev);
    if (!cal) return std::unexpected(cal.error());
    std::vector<std::string> bad;
    if (auto v = dev->validate(); !v) { bad.push_back(v.error().message); for (auto& n : v.error().notes) bad.push_back(n); }
    if (auto v = cal->validate(); !v) { bad.push_back(v.error().message); for (auto& n : v.error().notes) bad.push_back(n); }
    LoadedDevice out{std::move(*dev), std::move(*cal), {}};
    if (isTransmon(out.device.technology)) {
        // Spec 09 §6: collisions are reported with fidelity class Model (red edges on the chip,
        // spec 17 §3.5), never a load failure — a fabricated device with a marginal pair still runs.
        FrequencyPlan plan(out.device, out.calibration);
        for (const auto& c : plan.check())
            out.warnings.push_back(std::format("frequency collision [{}]: {}", collisionName(c.kind), c.message));
        // Model-vs-calibration ZZ comparison (spec 09 §5.3): warning above 3σ, never an error.
        for (const auto& [key, e] : out.calibration.edges) {
            if (e.couplingG.value.v <= 0.0) continue;
            const auto* qa = out.calibration.qubit(e.a); const auto* qb = out.calibration.qubit(e.b);
            if (!qa || !qb) continue;
            double zeta = transmon::staticZZ(e.couplingG.value, qa->f01.value, qb->f01.value, qa->anharmonicity.value, qb->anharmonicity.value).v;
            double sig = std::max(e.zz.sigma.v, 1.0);
            if (std::abs(zeta - e.zz.value.v) > 3.0 * sig)
                out.warnings.push_back(std::format("edge {}: model ZZ {:.1f} kHz vs calibrated {:.1f} ± {:.1f} kHz", key, zeta / 1e3, e.zz.value.v / 1e3, e.zz.sigma.v / 1e3));
        }
    }
    if (!bad.empty()) {
        Error e(ErrorCode::Hardware_ + 4, std::format("device '{}' failed to load with {} violation(s)", out.device.id, bad.size()));
        for (auto& b : bad) e.notes.push_back(std::move(b));
        return std::unexpected(std::move(e));
    }
    return out;
}

std::vector<std::filesystem::path> listDeviceDirs(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> v;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(root, ec))
        if (e.is_directory() && std::filesystem::exists(e.path() / "device.json")) v.push_back(e.path());
    std::sort(v.begin(), v.end());
    return v;
}

Json deviceToJson(const Device& d) {
    Json j;
    j["id"] = d.id; j["display_name"] = d.displayName; j["technology"] = std::string(technologyName(d.technology));
    j["qudit_dimension"] = d.quditDimension;
    for (const auto& q : d.qubits) j["qubits"].push_back({{"index", q.index}, {"kind", q.kind == QubitKind::Coupler ? "coupler" : q.kind == QubitKind::Ancilla ? "ancilla" : "data"}, {"pos", {q.pos[0], q.pos[1]}}});
    j["edges"] = Json::array();
    for (const auto& e : d.edges) {
        if (e.kind == EdgeKind::AllToAll) { j["edges"].push_back({{"a", "*"}, {"b", "*"}, {"kind", "all_to_all"}, {"directed", false}}); continue; }
        Json ej = {{"a", e.a}, {"b", e.b}, {"kind", e.kind == EdgeKind::TunableCoupler ? "tunable_coupler" : "fixed_capacitive"}, {"directed", e.directed}};
        if (e.coupler) ej["coupler"] = *e.coupler;
        j["edges"].push_back(ej);
    }
    j["native_gates"] = {{"single", d.gates.single}, {"two", d.gates.two}, {"measure", d.gates.measure}, {"reset", d.gates.reset}};
    j["timing"] = {{"dt_ps", d.timing.dtPs}, {"granularity_samples", d.timing.granularitySamples}, {"min_pulse_samples", d.timing.minPulseSamples},
                   {"readout_ns", d.timing.readout.v * 1e9}, {"readout_ringdown_ns", d.timing.readoutRingdown.v * 1e9}, {"repetition_delay_us", d.timing.repetitionDelay.v * 1e6}};
    Json reset = {{"policy", d.control.resetPolicy == ResetPolicy::Active ? "active" : d.control.resetPolicy == ResetPolicy::Passive ? "passive" : "cooling"}, {"passive_multiplier", d.control.passiveMultiplier}};
    if (d.control.activeResetDuration) reset["active_duration_ns"] = d.control.activeResetDuration->v * 1e9; else reset["active_duration_ns"] = nullptr;
    if (d.control.resetPolicy == ResetPolicy::Cooling) reset["cooling_time_ms"] = d.control.coolingTime.v * 1e3;
    j["control"] = {{"load_time_ms", d.control.loadTime.v * 1e3}, {"rep_overhead_us", d.control.repOverhead.v * 1e6}, {"feedback_latency_ns", d.control.feedbackLatency.v * 1e9},
                    {"reset", reset}, {"max_program_duration_ms", d.control.maxProgramDuration.v * 1e3}};
    j["qec"] = {{"cycle_time_us", d.control.qecCycleTime.v * 1e6}};
    return j;
}

Json calibrationToJson(const Calibration& c) {
    auto tri = [](double v, double s, const std::string& src) { return Json::array({v, s, src}); };
    Json j; j["device"] = c.device; j["timestamp"] = c.timestamp; j["qubits"] = Json::object(); j["edges"] = Json::object();
    for (std::size_t i = 0; i < c.qubits.size(); ++i) {
        const auto& q = c.qubits[i];
        if (q.f01.value.v <= 0.0) continue;
        Json qj;
        qj["f01_ghz"] = tri(q.f01.value.v / 1e9, q.f01.sigma.v / 1e9, q.f01.source);
        qj["anharmonicity_mhz"] = tri(q.anharmonicity.value.v / 1e6, q.anharmonicity.sigma.v / 1e6, q.anharmonicity.source);
        qj["t1_us"] = tri(q.t1.value.v * 1e6, q.t1.sigma.v * 1e6, q.t1.source);
        qj["t2_echo_us"] = tri(q.t2echo.value.v * 1e6, q.t2echo.sigma.v * 1e6, q.t2echo.source);
        qj["t2_star_us"] = tri(q.t2star.value.v * 1e6, q.t2star.sigma.v * 1e6, q.t2star.source);
        qj["thermal_population"] = tri(q.thermalPopulation.value, q.thermalPopulation.sigma, q.thermalPopulation.source);
        qj["gate_error_1q"] = tri(q.gateError1q.value, q.gateError1q.sigma, q.gateError1q.source);
        qj["duration_1q_ns"] = tri(q.duration1q.value.v * 1e9, 0.0, q.duration1q.source);
        qj["reset_error"] = tri(q.resetError.value, q.resetError.sigma, q.resetError.source);
        qj["readout_assignment"] = {{q.readoutAssignment[0][0], q.readoutAssignment[0][1]}, {q.readoutAssignment[1][0], q.readoutAssignment[1][1]}};
        qj["readout_duration_ns"] = tri(q.readoutDuration.value.v * 1e9, 0.0, q.readoutDuration.source);
        qj["leakage_1q"] = tri(q.leakage1q.value, q.leakage1q.sigma, q.leakage1q.source);
        if (q.readoutFrequency) qj["readout_f_ghz"] = tri(q.readoutFrequency->value.v / 1e9, q.readoutFrequency->sigma.v / 1e9, q.readoutFrequency->source);
        if (q.readoutChi) qj["readout_chi_mhz"] = tri(q.readoutChi->value.v / 1e6, q.readoutChi->sigma.v / 1e6, q.readoutChi->source);
        if (q.readoutKappa) qj["readout_kappa_mhz"] = tri(q.readoutKappa->value.v / 1e6, q.readoutKappa->sigma.v / 1e6, q.readoutKappa->source);
        j["qubits"][std::to_string(i)] = qj;
    }
    for (const auto& [k, e] : c.edges) {
        Json ej;
        ej["gate_error_2q"] = tri(e.gateError2q.value, e.gateError2q.sigma, e.gateError2q.source);
        ej["coherent_error_fraction_2q"] = tri(e.coherentErrorFraction2q.value, 0.0, e.coherentErrorFraction2q.source);
        ej["leakage_2q"] = tri(e.leakage2q.value, e.leakage2q.sigma, e.leakage2q.source);
        ej["zz_khz"] = tri(e.zz.value.v / 1e3, e.zz.sigma.v / 1e3, e.zz.source);
        ej["duration_ns"] = tri(e.duration.value.v * 1e9, 0.0, e.duration.source);
        ej["coupling_g_mhz"] = tri(e.couplingG.value.v / 1e6, e.couplingG.sigma.v / 1e6, e.couplingG.source);
        if (!e.nativeGate.empty()) ej["native_gate"] = e.nativeGate;
        if (e.coupler) ej["coupler"] = *e.coupler;
        j["edges"][k] = ej;
    }
    return j;
}
} // namespace qlab::hw
