// Spec 08 §5 — writing the `qlab.noise/1` document through core::JsonEnvelope. Values are the
// stored document-unit records, so parse(serialize()) reproduces the model bit for bit; unknown
// fields read with the document are written back (spec 23 §1).
#include "Core/Json.hpp"
#include "Noise/Model.hpp"
#include <cmath>

namespace qlab::noise {
namespace {
constexpr const char* kKind = "qlab.noise";
constexpr const char* kSchema = "qlab.noise/1";
struct Registrar {
    Registrar() { core::JsonEnvelope::registerKind(kKind, 1); }
} registrar;

core::Json base(const core::Json& extra) {
    return extra.is_object() ? extra : core::Json::object();
}
core::Json assignmentJson(const Assignment2& a) {
    return core::Json::array(
        {core::Json::array({a[0][0], a[0][1]}), core::Json::array({a[1][0], a[1][1]})});
}
// JSON has no infinity (spec 23 §1): an absent process is an omitted field.
void putLifetime(core::Json& j, const char* key, double us) {
    if (std::isfinite(us))
        j[key] = us;
}
core::Json replacementsJson(const std::map<std::string, double>& fields) {
    core::Json j = core::Json::object();
    for (const auto& [k, v] : fields)
        j[k] = v;
    return j;
}
} // namespace

core::Json NoiseModel::toJson() const {
    core::Json d = base(extra_);
    d["schema"] = kSchema;
    core::Json source = base(sourceExtra_);
    source["device"] = device_;
    source["calibration"] = calibrationStamp_;
    d["source"] = std::move(source);
    d["levels"] = levels_;

    core::Json qubits = core::Json::object();
    for (std::size_t q = 0; q < rawQubits_.size(); ++q) {
        const QubitRecord& r = rawQubits_[q];
        core::Json j = base(r.extra);
        putLifetime(j, "t1_us", r.t1Us);
        putLifetime(j, "t2_us", r.t2Us);
        putLifetime(j, "t2_star_us", r.t2StarUs);
        j["frequency_ghz"] = r.frequencyGhz;
        core::Json readout = base(r.readoutExtra);
        readout["assignment"] = assignmentJson(r.assignment);
        readout["duration_ns"] = r.readoutDurationNs;
        readout["crosstalk_dephasing"] = r.readoutDephasing;
        j["readout"] = std::move(readout);
        j["reset_error"] = r.resetError;
        j["p_thermal"] = r.pThermal;
        j["line_photon_number"] = r.linePhotons;
        qubits[std::to_string(q)] = std::move(j);
    }
    d["qubits"] = std::move(qubits);

    core::Json gates = core::Json::object();
    for (const auto& [name, byKey] : rawGates_) {
        core::Json g = core::Json::object();
        for (const auto& [key, r] : byKey) {
            core::Json j = base(r.extra);
            j["error"] = r.error;
            j["duration_ns"] = r.durationNs;
            j["coherent_fraction"] = r.coherentFraction;
            j["leakage"] = r.leakage;
            if (r.seepage)
                j["seepage"] = *r.seepage;
            g[key] = std::move(j);
        }
        gates[name] = std::move(g);
    }
    d["gates"] = std::move(gates);

    core::Json edges = core::Json::object();
    for (const auto& [key, r] : rawEdges_) {
        core::Json j = base(r.extra);
        j["zz_hz"] = r.zzHz;
        edges[key] = std::move(j);
    }
    d["edges"] = std::move(edges);

    core::Json groups = core::Json::array();
    for (std::size_t i = 0; i < readoutGroups_.size(); ++i) {
        const ReadoutGroup& g = readoutGroups_[i];
        core::Json j =
            base(i < readoutGroupExtra_.size() ? readoutGroupExtra_[i] : core::Json::object());
        j["qubits"] = g.qubits;
        if (g.assignment) {
            core::Json rows = core::Json::array();
            for (std::size_t r = 0; r < g.assignment->rows; ++r) {
                core::Json row = core::Json::array();
                for (std::size_t c = 0; c < g.assignment->cols; ++c)
                    row.push_back((*g.assignment)(r, c));
                rows.push_back(std::move(row));
            }
            j["assignment"] = std::move(rows);
        } else {
            j["assignment"] = nullptr;
        }
        groups.push_back(std::move(j));
    }
    d["readout_groups"] = std::move(groups);
    d["feedlines"] = feedlines_;
    d["fallback_gates"] = {{"one_qubit", fallback1q_}, {"two_qubit", fallback2q_}};

    core::Json o = base(overridesExtra_);
    o["scale_t1"] = overrides_.scaleT1;
    o["scale_t2"] = overrides_.scaleT2;
    o["scale_gate_error"] = overrides_.scaleGateError;
    o["scale_readout_error"] = overrides_.scaleReadoutError;
    o["disable"] = overrides_.disable;
    core::Json replaceQubits = core::Json::object(), replaceEdges = core::Json::object();
    for (const auto& [q, fields] : overrides_.replaceQubit)
        replaceQubits[std::to_string(q)] = replacementsJson(fields);
    for (const auto& [key, fields] : overrides_.replaceEdge)
        replaceEdges[key] = replacementsJson(fields);
    core::Json replace = o.contains("replace")
                             ? base(o["replace"])
                             : core::Json::object(); // unknown keys kept by the reader
    replace["qubits"] = std::move(replaceQubits);
    replace["edges"] = std::move(replaceEdges);
    o["replace"] = std::move(replace);
    d["overrides"] = std::move(o);
    return d;
}

std::string NoiseModel::serialize() const {
    return core::JsonEnvelope::serialize(kKind, toJson());
}

Status NoiseModel::save(const std::filesystem::path& file) const {
    return core::JsonEnvelope::save(file, kKind, toJson());
}

Result<NoiseModel> NoiseModel::parse(const std::string& text) {
    QXL_TRY_ASSIGN(auto envelope, core::JsonEnvelope::parse(text, kKind));
    return fromJson(envelope.data);
}

Result<NoiseModel> NoiseModel::load(const std::filesystem::path& file) {
    QXL_TRY_ASSIGN(auto envelope, core::JsonEnvelope::load(file, kKind));
    return fromJson(envelope.data);
}

} // namespace qlab::noise
