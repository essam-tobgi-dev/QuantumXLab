// Spec 08 §5 — `qlab.noise/1` entry point: header fields, readout groups, feedlines, overrides.
// No exception crosses the module boundary (spec 02 §1): a stray nlohmann exception becomes BadJson.
#include "Noise/JsonFields.hpp"
#include "Noise/Model.hpp"
#include <exception>

namespace qlab::noise {
namespace {
constexpr const char* kSchema = "qlab.noise/1";
}
using namespace json;

Status NoiseModel::readReadout(const Json& data) {
    if (auto groups = data.find("readout_groups"); groups != data.end()) {
        if (!groups->is_array()) return fail(err::BadJson, "noise.readout_groups: expected an array");
        for (std::size_t i = 0; i < groups->size(); ++i) {
            const std::string path = std::format("noise.readout_groups[{}]", i);
            const Json& g = (*groups)[i];
            QXL_TRY(objectAt(g, path));
            ReadoutGroup group;
            if (!g.contains("qubits")) return fail(err::BadJson, path + ".qubits: missing field");
            QXL_TRY_ASSIGN(group.qubits, indexList(g["qubits"], path + ".qubits"));
            const std::size_t k = group.qubits.size();
            if (k == 0 || k > 4) return fail(err::BadReadout, std::format("{}.qubits: a readout group holds 1..4 qubits (spec 08 §3)", path));
            for (std::size_t a = 0; a < k; ++a)
                for (std::size_t b = a + 1; b < k; ++b)
                    if (group.qubits[a] == group.qubits[b]) return fail(err::BadJson, std::format("{}.qubits: repeated qubit", path));
            if (auto a = g.find("assignment"); a != g.end() && !a->is_null()) {
                QXL_TRY_ASSIGN(num::RealMatrix m, realMatrix(*a, path + ".assignment"));
                if (m.rows != (std::size_t{1} << k))
                    return fail(err::BadReadout, std::format("{}.assignment: {} qubits need a {}x{} matrix", path, k, 1u << k, 1u << k));
                QXL_TRY(validateAssignment(m, path + ".assignment"));
                group.assignment = std::move(m);
            }
            readoutGroups_.push_back(std::move(group));
            readoutGroupExtra_.push_back(unknownFields(g, {"qubits", "assignment"}));
        }
    }
    if (auto lines = data.find("feedlines"); lines != data.end()) {
        if (!lines->is_array()) return fail(err::BadJson, "noise.feedlines: expected an array");
        for (std::size_t i = 0; i < lines->size(); ++i) {
            QXL_TRY_ASSIGN(auto line, indexList((*lines)[i], std::format("noise.feedlines[{}]", i)));
            feedlines_.push_back(std::move(line));
        }
    }
    if (auto fb = data.find("fallback_gates"); fb != data.end()) {
        QXL_TRY(objectAt(*fb, "noise.fallback_gates"));
        QXL_TRY_ASSIGN(fallback1q_, text(*fb, "noise.fallback_gates", "one_qubit"));
        QXL_TRY_ASSIGN(fallback2q_, text(*fb, "noise.fallback_gates", "two_qubit"));
    }
    return {};
}

Status NoiseModel::readOverrides(const Json& data) {
    auto it = data.find("overrides");
    if (it == data.end()) return {};
    const Json& o = *it;
    const std::string path = "noise.overrides";
    QXL_TRY(objectAt(o, path));
    overridesExtra_ = unknownFields(o, {"scale_t1", "scale_t2", "scale_gate_error", "scale_readout_error", "disable", "replace"});
    QXL_TRY_ASSIGN(overrides_.scaleT1, number(o, path, "scale_t1", 1.0));
    QXL_TRY_ASSIGN(overrides_.scaleT2, number(o, path, "scale_t2", 1.0));
    QXL_TRY_ASSIGN(overrides_.scaleGateError, number(o, path, "scale_gate_error", 1.0));
    QXL_TRY_ASSIGN(overrides_.scaleReadoutError, number(o, path, "scale_readout_error", 1.0));
    if (auto d = o.find("disable"); d != o.end()) {
        if (!d->is_array()) return fail(err::BadJson, path + ".disable: expected an array of channel ids");
        for (std::size_t i = 0; i < d->size(); ++i) {
            if (!(*d)[i].is_string()) return fail(err::BadJson, std::format("{}.disable[{}]: expected a channel id", path, i));
            overrides_.disable.push_back((*d)[i].get<std::string>());
        }
    }
    if (auto rep = o.find("replace"); rep != o.end()) {
        QXL_TRY(objectAt(*rep, path + ".replace"));
        Json unknown = unknownFields(*rep, {"qubits", "edges"});
        if (!unknown.empty()) overridesExtra_["replace"] = std::move(unknown);
        if (auto qs = rep->find("qubits"); qs != rep->end()) {
            QXL_TRY(objectAt(*qs, path + ".replace.qubits"));
            for (auto q = qs->begin(); q != qs->end(); ++q) {
                QXL_TRY_ASSIGN(const std::uint32_t index, indexFrom(q.key(), path + ".replace.qubits"));
                QXL_TRY_ASSIGN(overrides_.replaceQubit[index], numberMap(q.value(), path + ".replace.qubits." + q.key()));
            }
        }
        if (auto es = rep->find("edges"); es != rep->end()) {
            QXL_TRY(objectAt(*es, path + ".replace.edges"));
            for (auto e = es->begin(); e != es->end(); ++e) {
                const std::string epath = path + ".replace.edges." + e.key();
                QXL_TRY_ASSIGN(const auto ends, targetsFrom(e.key(), epath, 2));
                if (ends.size() != 2) return fail(err::BadJson, epath + ": an edge key names two qubits 'a-b'");
                QXL_TRY_ASSIGN(overrides_.replaceEdge[targetKey(ends)], numberMap(e.value(), epath));
            }
        }
    }
    return {};
}

Result<NoiseModel> NoiseModel::fromJson(const Json& data) {
    try {
        QXL_TRY(objectAt(data, "noise"));
        NoiseModel m;
        m.extra_ = unknownFields(data, {"schema", "source", "levels", "qubits", "gates", "edges", "readout_groups", "feedlines",
                                        "fallback_gates", "overrides"});
        QXL_TRY_ASSIGN(const std::string schema, text(data, "noise", "schema"));
        if (!schema.empty() && schema != kSchema)
            return fail(err::BadJson, std::format("noise.schema: expected '{}', found '{}'", kSchema, schema));
        if (auto s = data.find("source"); s != data.end()) {
            QXL_TRY(objectAt(*s, "noise.source"));
            m.sourceExtra_ = unknownFields(*s, {"device", "calibration"});
            QXL_TRY_ASSIGN(m.device_, text(*s, "noise.source", "device"));
            QXL_TRY_ASSIGN(m.calibrationStamp_, text(*s, "noise.source", "calibration"));
        }
        if (auto l = data.find("levels"); l != data.end()) {
            if (!l->is_number_unsigned() || (l->get<std::uint64_t>() != 2 && l->get<std::uint64_t>() != 3))
                return fail(err::LevelsMismatch, std::format("noise.levels: 2 or 3 expected, found {}", l->dump()));
            m.levels_ = static_cast<std::uint32_t>(l->get<std::uint64_t>());
        }
        QXL_TRY(m.readQubits(data));
        QXL_TRY(m.readGates(data));
        QXL_TRY(m.readReadout(data));
        QXL_TRY(m.readOverrides(data));
        // Qubits referenced by gates, edges, groups or feedlines but not listed are noiseless slots.
        std::size_t needed = m.rawQubits_.size();
        auto cover = [&](std::uint32_t q) { needed = std::max(needed, static_cast<std::size_t>(q) + 1); };
        for (const auto& [name, byKey] : m.rawGates_)
            for (const auto& [key, g] : byKey)
                for (auto q : g.qubits) cover(q);
        for (const auto& [key, e] : m.rawEdges_) { cover(e.a); cover(e.b); }
        for (const auto& g : m.readoutGroups_)
            for (auto q : g.qubits) cover(q);
        for (const auto& line : m.feedlines_)
            for (auto q : line) cover(q);
        m.rawQubits_.resize(needed);
        QXL_TRY(m.rebuild());
        return m;
    } catch (const std::exception& e) {
        return fail(err::BadJson, std::format("noise: malformed document ({})", e.what()));
    }
}

} // namespace qlab::noise
