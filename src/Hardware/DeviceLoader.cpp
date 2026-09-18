#include "Hardware/Loader.hpp"
#include <format>

namespace qlab::hw {
using core::Json;
namespace {
struct Ctx {
    std::vector<std::string> bad;
    template <class T> T get(const Json& j, const char* key, const char* path, T def, bool required) {
        if (!j.contains(key)) { if (required) bad.push_back(std::format("missing field {}.{}", path, key)); return def; }
        try { return j.at(key).get<T>(); } catch (...) { bad.push_back(std::format("bad type at {}.{}", path, key)); return def; }
    }
    double num(const Json& j, const char* key, const char* path, double def, bool required = true) { return get<double>(j, key, path, def, required); }
};
} // namespace

Result<Device> parseDevice(const Json& d, const std::filesystem::path& dir) {
    Ctx c;
    Device dev;
    dev.directory = dir;
    dev.id = c.get<std::string>(d, "id", "data", "", true);
    dev.displayName = c.get<std::string>(d, "display_name", "data", dev.id, false);
    if (auto t = technologyFromName(c.get<std::string>(d, "technology", "data", "", true))) dev.technology = *t;
    else c.bad.push_back(t.error().message);
    dev.quditDimension = c.get<int>(d, "qudit_dimension", "data", isTransmon(dev.technology) ? 3 : 2, false);
    if (d.contains("qubits") && d["qubits"].is_array()) {
        for (const auto& q : d["qubits"]) {
            QubitInfo qi;
            qi.index = c.get<std::uint32_t>(q, "index", "qubits[]", 0, true);
            std::string kind = c.get<std::string>(q, "kind", "qubits[]", "data", false);
            qi.kind = kind == "coupler" ? QubitKind::Coupler : kind == "ancilla" ? QubitKind::Ancilla : QubitKind::Data;
            if (q.contains("pos") && q["pos"].is_array() && q["pos"].size() == 2) qi.pos = {q["pos"][0].get<double>(), q["pos"][1].get<double>()};
            dev.qubits.push_back(qi);
        }
    } else c.bad.push_back("missing field data.qubits");
    if (d.contains("edges") && d["edges"].is_array()) {
        for (const auto& e : d["edges"]) {
            EdgeInfo ei;
            std::string kind = c.get<std::string>(e, "kind", "edges[]", "fixed_capacitive", false);
            if (kind == "all_to_all" || (e.contains("a") && e["a"].is_string())) { ei.kind = EdgeKind::AllToAll; dev.allToAll = true; dev.edges.push_back(ei); continue; }
            ei.kind = kind == "tunable_coupler" ? EdgeKind::TunableCoupler : EdgeKind::FixedCapacitive;
            ei.a = c.get<std::uint32_t>(e, "a", "edges[]", 0, true);
            ei.b = c.get<std::uint32_t>(e, "b", "edges[]", 0, true);
            ei.directed = c.get<bool>(e, "directed", "edges[]", false, false);
            if (e.contains("coupler") && e["coupler"].is_number()) ei.coupler = e["coupler"].get<std::uint32_t>();
            dev.edges.push_back(ei);
        }
    } else c.bad.push_back("missing field data.edges");
    if (d.contains("native_gates")) {
        const Json& g = d["native_gates"];
        dev.gates.single = c.get<std::vector<std::string>>(g, "single", "native_gates", {}, true);
        dev.gates.two = c.get<std::vector<std::string>>(g, "two", "native_gates", {}, true);
        dev.gates.measure = c.get<std::string>(g, "measure", "native_gates", "dispersive", false);
        dev.gates.reset = c.get<std::string>(g, "reset", "native_gates", "", false);
    } else c.bad.push_back("missing field data.native_gates");
    if (d.contains("timing")) {
        const Json& t = d["timing"];
        dev.timing.dtPs = static_cast<std::int64_t>(c.num(t, "dt_ps", "timing", 222));
        dev.timing.granularitySamples = c.get<int>(t, "granularity_samples", "timing", 16, false);
        dev.timing.minPulseSamples = c.get<int>(t, "min_pulse_samples", "timing", 64, false);
        dev.timing.readout = units::Time(c.num(t, "readout_ns", "timing", 640) * 1e-9);
        dev.timing.readoutRingdown = units::Time(c.num(t, "readout_ringdown_ns", "timing", 160, false) * 1e-9);
        dev.timing.repetitionDelay = units::Time(c.num(t, "repetition_delay_us", "timing", 250, false) * 1e-6);
        dev.timing.feedforward = units::Time(c.num(t, "feedforward_ns", "timing", 200, false) * 1e-9);
    } else c.bad.push_back("missing field data.timing");
    if (d.contains("control")) {
        const Json& k = d["control"];
        dev.control.loadTime = units::Time(c.num(k, "load_time_ms", "control", 50) * 1e-3);
        dev.control.repOverhead = units::Time(c.num(k, "rep_overhead_us", "control", 1.0) * 1e-6);
        dev.control.feedbackLatency = units::Time(c.num(k, "feedback_latency_ns", "control", 200) * 1e-9);
        dev.control.maxProgramDuration = units::Time(c.num(k, "max_program_duration_ms", "control", 10, false) * 1e-3);
        if (k.contains("reset")) {
            const Json& r = k["reset"];
            std::string pol = c.get<std::string>(r, "policy", "control.reset", "active", true);
            dev.control.resetPolicy = pol == "passive" ? ResetPolicy::Passive : pol == "cooling" ? ResetPolicy::Cooling : ResetPolicy::Active;
            if (r.contains("active_duration_ns") && r["active_duration_ns"].is_number()) dev.control.activeResetDuration = units::Time(r["active_duration_ns"].get<double>() * 1e-9);
            dev.control.passiveMultiplier = c.num(r, "passive_multiplier", "control.reset", 5.0, false);
            dev.control.coolingTime = units::Time(c.num(r, "cooling_time_ms", "control.reset", 1.5, false) * 1e-3);
        } else c.bad.push_back("missing field data.control.reset");
    } else c.bad.push_back("missing field data.control");
    if (d.contains("qec")) dev.control.qecCycleTime = units::Time(c.num(d["qec"], "cycle_time_us", "qec", 1.0) * 1e-6);
    if (d.contains("readout")) {
        const Json& r = d["readout"];
        if (r.contains("resonator_f_ghz")) for (const auto& f : r["resonator_f_ghz"]) dev.readout.resonatorFrequencies.push_back(units::Frequency(f.get<double>() * 1e9));
        if (r.contains("feedlines")) for (const auto& f : r["feedlines"]) dev.readout.feedlines.push_back({c.get<int>(f, "id", "feedlines[]", 0, true), c.get<std::vector<std::uint32_t>>(f, "qubits", "feedlines[]", {}, true)});
        dev.readout.purcellFilter = c.get<bool>(r, "purcell_filter", "readout", true, false);
        dev.readout.resonatorSpacingMin = units::Frequency(c.num(r, "resonator_spacing_mhz_min", "readout", 40, false) * 1e6);
        dev.readout.method = c.get<std::string>(r, "method", "readout", dev.gates.measure, false);
        dev.readout.detectionWindow = units::Time(c.num(r, "detection_window_us", "readout", 0, false) * 1e-6);
        dev.readout.collectionEfficiency = c.num(r, "collection_efficiency", "readout", 0, false);
        dev.readout.brightRatePerUs = c.num(r, "bright_rate_per_us", "readout", 0, false);
        dev.readout.darkRatePerUs = c.num(r, "dark_rate_per_us", "readout", 0, false);
    }
    if (d.contains("frequency_plan")) {
        const Json& f = d["frequency_plan"];
        if (f.contains("band_ghz") && f["band_ghz"].size() == 2) { dev.frequencyPlan.bandLow = units::Frequency(f["band_ghz"][0].get<double>() * 1e9); dev.frequencyPlan.bandHigh = units::Frequency(f["band_ghz"][1].get<double>() * 1e9); }
        dev.frequencyPlan.minNeighbourDetuning = units::Frequency(c.num(f, "min_neighbour_detuning_mhz", "frequency_plan", 50, false) * 1e6);
        if (f.contains("coupler_band_ghz") && f["coupler_band_ghz"].size() == 2) dev.frequencyPlan.couplerBand = {units::Frequency(f["coupler_band_ghz"][0].get<double>() * 1e9), units::Frequency(f["coupler_band_ghz"][1].get<double>() * 1e9)};
    }
    if (d.contains("motional_modes")) {
        const Json& m = d["motional_modes"];
        MotionalModes mm;
        mm.axis = c.get<std::string>(m, "axis", "motional_modes", "axial", false);
        mm.cutoff = c.get<int>(m, "cutoff", "motional_modes", 8, false);
        mm.heatingQuantaPerS = c.num(m, "heating_quanta_per_s", "motional_modes", 50, false);
        mm.omegaZ = units::Frequency(c.num(m, "omega_z_mhz", "motional_modes", 0.3) * 1e6);
        mm.omegaR = units::Frequency(c.num(m, "omega_r_mhz", "motional_modes", 3.0) * 1e6);
        dev.motionalModes = mm;
    }
    if (d.contains("ion")) {
        const Json& i = d["ion"];
        IonInfo ii;
        ii.species = c.get<std::string>(i, "species", "ion", "171Yb+", false);
        ii.qubit = c.get<std::string>(i, "qubit", "ion", "hyperfine_clock", false);
        ii.fQubit = units::Frequency(c.num(i, "f_qubit_ghz", "ion", 12.642812118, false) * 1e9);
        ii.ramanWavelength = units::Length(c.num(i, "raman_wavelength_nm", "ion", 355, false) * 1e-9);
        ii.lambDickeNominal = c.num(i, "lamb_dicke_nominal", "ion", 0.08, false);
        dev.ion = ii;
    }
    if (d.contains("crosstalk") && d["crosstalk"].contains("drive"))
        for (const auto& x : d["crosstalk"]["drive"]) if (x.is_array() && x.size() >= 3)
            dev.crosstalk.push_back({x[0].get<std::uint32_t>(), x[1].get<std::uint32_t>(), x[2].get<double>(), x.size() > 3 ? x[3].get<double>() : 0.0});
    dev.leakageChannels = c.get<bool>(d, "leakage_channels", "data", false, false);
    if (!c.bad.empty()) {
        Error e(ErrorCode::Hardware_ + 1, std::format("device.json for '{}' has {} problem(s)", dev.id, c.bad.size()));
        for (auto& b : c.bad) e.notes.push_back(std::move(b));
        return std::unexpected(std::move(e));
    }
    return dev;
}

Result<Device> loadDeviceJson(const std::filesystem::path& file) {
    auto env = core::JsonEnvelope::load(file, "device");
    if (!env) return std::unexpected(env.error());
    return parseDevice(env->data, file.parent_path());
}
} // namespace qlab::hw
