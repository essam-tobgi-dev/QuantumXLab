#include "Hardware/Loader.hpp"
#include <format>

namespace qlab::hw {
using core::Json;
namespace {
struct CalCtx {
    std::vector<std::string> bad;
    // Reads a [value, sigma, source] triple (or a bare number) scaled by `scale` into SI.
    template <class T> Cal<T> triple(const Json& j, const char* key, const std::string& path, double scale, double def, bool required, const char* defSource = "default") {
        Cal<T> c; c.source = defSource;
        if (!j.contains(key)) { if (required) bad.push_back(std::format("missing field {}.{}", path, key)); c.value = T(def * scale); c.sigma = T(0.0); return c; }
        const Json& v = j.at(key);
        try {
            if (v.is_number()) { c.value = T(v.get<double>() * scale); c.sigma = T(0.0); c.source = "file"; }
            else if (v.is_array() && !v.empty()) {
                c.value = T(v[0].get<double>() * scale);
                c.sigma = T(v.size() > 1 && v[1].is_number() ? v[1].get<double>() * scale : 0.0);
                if (v.size() > 2 && v[2].is_string()) c.source = v[2].get<std::string>();
            } else bad.push_back(std::format("bad type at {}.{}", path, key));
        } catch (...) { bad.push_back(std::format("bad type at {}.{}", path, key)); }
        return c;
    }
    Cal<double> d(const Json& j, const char* key, const std::string& path, double def, bool required) { return triple<double>(j, key, path, 1.0, def, required); }
    Cal<units::Frequency> f(const Json& j, const char* key, const std::string& path, double scale, double def, bool required) { return triple<units::Frequency>(j, key, path, scale, def, required); }
    Cal<units::Time> t(const Json& j, const char* key, const std::string& path, double scale, double def, bool required) { return triple<units::Time>(j, key, path, scale, def, required); }
};
} // namespace

Result<Calibration> parseCalibration(const Json& d, const Device& dev) {
    CalCtx c;
    Calibration cal;
    cal.device = d.value("device", "");
    cal.timestamp = d.value("timestamp", "");
    if (cal.device != dev.id) c.bad.push_back(std::format("calibration is for '{}' but device is '{}'", cal.device, dev.id));
    cal.qubits.resize(dev.qubits.size());
    const double readoutDefNs = dev.timing.readout.v * 1e9;
    if (!d.contains("qubits") || !d["qubits"].is_object()) c.bad.push_back("missing field data.qubits");
    else {
        for (const auto& [key, q] : d["qubits"].items()) {
            std::uint32_t idx = 0;
            try { idx = static_cast<std::uint32_t>(std::stoul(key)); } catch (...) { c.bad.push_back("qubits key '" + key + "' is not an index"); continue; }
            if (!dev.hasQubit(idx)) { c.bad.push_back(std::format("calibration for non-existent qubit {}", idx)); continue; }
            const std::string p = "qubits." + key;
            QubitCal& qc = cal.qubits[idx];
            qc.f01 = c.f(q, "f01_ghz", p, 1e9, 0.0, true);
            qc.anharmonicity = c.f(q, "anharmonicity_mhz", p, 1e6, 0.0, isTransmon(dev.technology));
            qc.t1 = c.t(q, "t1_us", p, 1e-6, 0.0, true);
            qc.t2echo = c.t(q, "t2_echo_us", p, 1e-6, 0.0, true);
            qc.t2star = c.t(q, "t2_star_us", p, 1e-6, 0.0, true);
            qc.thermalPopulation = c.d(q, "thermal_population", p, 0.0, true);
            qc.gateError1q = c.d(q, "gate_error_1q", p, 0.0, true);
            qc.duration1q = c.t(q, "duration_1q_ns", p, 1e-9, 32.0, false);
            qc.coherentErrorFraction1q = c.d(q, "coherent_error_fraction_1q", p, 0.0, false);
            qc.resetError = c.d(q, "reset_error", p, 0.005, false);
            qc.readoutDuration = c.t(q, "readout_duration_ns", p, 1e-9, readoutDefNs, false);
            qc.readoutCrosstalkDephasing = c.d(q, "readout_crosstalk_dephasing", p, 0.0, false);
            qc.leakage1q = c.d(q, "leakage_1q", p, 0.0, false);
            if (q.contains("readout_f_ghz")) qc.readoutFrequency = c.f(q, "readout_f_ghz", p, 1e9, 0.0, false);
            if (q.contains("readout_chi_mhz")) qc.readoutChi = c.f(q, "readout_chi_mhz", p, 1e6, 0.0, false);
            if (q.contains("readout_kappa_mhz")) qc.readoutKappa = c.f(q, "readout_kappa_mhz", p, 1e6, 0.0, false);
            if (q.contains("lamb_dicke")) qc.lambDicke = c.d(q, "lamb_dicke", p, 0.08, false);
            if (q.contains("readout_assignment") && q["readout_assignment"].is_array() && q["readout_assignment"].size() == 2) {
                try { for (int r = 0; r < 2; ++r) for (int s = 0; s < 2; ++s) qc.readoutAssignment[r][s] = q["readout_assignment"][r][s].get<double>(); }
                catch (...) { c.bad.push_back("bad type at " + p + ".readout_assignment"); }
            } else c.bad.push_back("missing field " + p + ".readout_assignment");
        }
        for (std::size_t i = 0; i < dev.qubits.size(); ++i)
            if (dev.qubits[i].kind == QubitKind::Data && !d["qubits"].contains(std::to_string(i))) c.bad.push_back(std::format("no calibration entry for data qubit {}", i));
    }
    if (d.contains("edges") && d["edges"].is_object()) {
        for (const auto& [key, e] : d["edges"].items()) {
            EdgeCal ec;
            auto dash = key.find('-');
            try { ec.a = static_cast<std::uint32_t>(std::stoul(key.substr(0, dash))); ec.b = static_cast<std::uint32_t>(std::stoul(key.substr(dash + 1))); }
            catch (...) { c.bad.push_back("edges key '" + key + "' is not 'a-b'"); continue; }
            if (!dev.adjacent(ec.a, ec.b)) { c.bad.push_back(std::format("calibration for non-existent edge {}", key)); continue; }
            const std::string p = "edges." + key;
            ec.nativeGate = e.value("native_gate", dev.gates.two.empty() ? "" : dev.gates.two.front());
            ec.gateError2q = c.d(e, "gate_error_2q", p, 0.0, true);
            ec.coherentErrorFraction2q = c.d(e, "coherent_error_fraction_2q", p, 0.0, false);
            ec.leakage2q = c.d(e, "leakage_2q", p, 0.0, false);
            ec.zz = c.f(e, "zz_khz", p, 1e3, 0.0, false);
            ec.duration = c.t(e, "duration_ns", p, 1e-9, 0.0, true);
            ec.couplingG = c.f(e, "coupling_g_mhz", p, 1e6, 0.0, false);
            if (e.contains("coupler") && e["coupler"].is_number()) ec.coupler = e["coupler"].get<std::uint32_t>();
            for (const auto& g : dev.gates.two)
                if (g != ec.nativeGate && e.contains(g) && e[g].is_object())
                    ec.extraGates[g] = {c.d(e[g], "gate_error_2q", p + "." + g, ec.gateError2q.value, false), c.t(e[g], "duration_ns", p + "." + g, 1e-9, ec.duration.value.v * 1e9, false)};
            cal.edges[key] = std::move(ec);
        }
        if (!dev.allToAll)
            for (const auto& ed : dev.edges) if (!cal.edge(ed.a, ed.b)) c.bad.push_back(std::format("no calibration entry for edge {}-{}", ed.a, ed.b));
    } else c.bad.push_back("missing field data.edges");
    if (d.contains("motional") && d["motional"].is_object()) {
        MotionalCal m;
        const Json& mj = d["motional"];
        if (mj.contains("equilibrium_positions_l")) for (const auto& v : mj["equilibrium_positions_l"]) m.equilibriumPositions.push_back(v.get<double>());
        if (mj.contains("axial_modes_mhz")) for (const auto& v : mj["axial_modes_mhz"]) m.axialModes.push_back(units::Frequency(v.get<double>() * 1e6));
        if (mj.contains("radial_modes_mhz")) for (const auto& v : mj["radial_modes_mhz"]) m.radialModes.push_back(units::Frequency(v.get<double>() * 1e6));
        if (mj.contains("gate_mode") && mj["gate_mode"].is_object()) {
            const Json& gm = mj["gate_mode"];
            m.gateModeAxis = gm.value("axis", std::string{"axial"});
            if (m.gateModeAxis != "axial" && m.gateModeAxis != "radial")
                c.bad.push_back(std::format("data.motional.gate_mode.axis must be 'axial' or 'radial', found '{}'", m.gateModeAxis));
            m.gateModeIndex = gm.value("index", 0u);
            if (gm.contains("participation") && gm["participation"].is_array())
                for (const auto& v : gm["participation"]) m.gateModeParticipation.push_back(v.get<double>());
            if (!m.gateModeFrequency())
                c.bad.push_back(std::format("data.motional.gate_mode names {} mode {} but only {} are listed", m.gateModeAxis, m.gateModeIndex,
                                            (m.gateModeAxis == "radial" ? m.radialModes : m.axialModes).size()));
        }
        if (mj.contains("heating_quanta_per_s")) {
            const Json& h = mj["heating_quanta_per_s"]; // [value, sigma, source] triple or a bare number
            if (h.is_array() && !h.empty() && h[0].is_number()) m.heatingQuantaPerS = h[0].get<double>();
            else if (h.is_number()) m.heatingQuantaPerS = h.get<double>();
        }
        cal.motional = std::move(m);
    }
    if (!c.bad.empty()) {
        Error err(ErrorCode::Hardware_ + 2, std::format("calibration.json for '{}' has {} problem(s)", dev.id, c.bad.size()));
        for (auto& b : c.bad) err.notes.push_back(std::move(b));
        return std::unexpected(std::move(err));
    }
    return cal;
}

Result<Calibration> loadCalibrationJson(const std::filesystem::path& file, const Device& dev) {
    auto env = core::JsonEnvelope::load(file, "calibration");
    if (!env) return std::unexpected(env.error());
    return parseCalibration(env->data, dev);
}
} // namespace qlab::hw
