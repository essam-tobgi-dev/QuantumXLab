// Spec 08 §5 — reading the `qubits`, `gates` and `edges` sections of `qlab.noise/1`. Targets are
// encoded in the keys ("0", "0-1"); missing optional fields take the §4.1 defaults.
#include "Noise/JsonFields.hpp"
#include "Noise/Model.hpp"

namespace qlab::noise {
namespace {
constexpr double kDefaultResetError = 0.005; // spec 08 §4.1
}
using namespace json;

Status NoiseModel::readQubits(const Json& data) {
    auto qs = data.find("qubits");
    if (qs == data.end())
        return {};
    QXL_TRY(objectAt(*qs, "noise.qubits"));
    for (auto it = qs->begin(); it != qs->end(); ++it) {
        const std::string path = "noise.qubits." + it.key();
        QXL_TRY_ASSIGN(const std::uint32_t q, indexFrom(it.key(), "noise.qubits"));
        const Json& e = it.value();
        QXL_TRY(objectAt(e, path));
        QubitRecord r;
        r.extra = unknownFields(e, {"t1_us", "t2_us", "t2_star_us", "frequency_ghz", "readout",
                                    "reset_error", "p_thermal", "line_photon_number"});
        QXL_TRY_ASSIGN(r.t1Us, number(e, path, "t1_us", kAbsent));
        QXL_TRY_ASSIGN(r.t2Us, number(e, path, "t2_us", kAbsent));
        QXL_TRY_ASSIGN(r.t2StarUs, number(e, path, "t2_star_us", kAbsent));
        QXL_TRY_ASSIGN(r.frequencyGhz, number(e, path, "frequency_ghz", 0.0));
        QXL_TRY_ASSIGN(r.resetError, number(e, path, "reset_error", kDefaultResetError));
        QXL_TRY_ASSIGN(r.pThermal, number(e, path, "p_thermal", 0.0));
        QXL_TRY_ASSIGN(r.linePhotons, number(e, path, "line_photon_number", 0.0));
        if (auto ro = e.find("readout"); ro != e.end()) {
            const std::string rpath = path + ".readout";
            QXL_TRY(objectAt(*ro, rpath));
            r.readoutExtra =
                unknownFields(*ro, {"assignment", "duration_ns", "crosstalk_dephasing"});
            if (auto a = ro->find("assignment"); a != ro->end()) {
                QXL_TRY_ASSIGN(const num::RealMatrix m, realMatrix(*a, rpath + ".assignment"));
                if (m.rows != 2)
                    return fail(err::BadJson,
                                std::format("{}.assignment: expected a 2x2 matrix", rpath));
                r.assignment = {{{m(0, 0), m(0, 1)}, {m(1, 0), m(1, 1)}}};
            }
            QXL_TRY_ASSIGN(r.readoutDurationNs, number(*ro, rpath, "duration_ns", 0.0));
            QXL_TRY_ASSIGN(r.readoutDephasing, number(*ro, rpath, "crosstalk_dephasing", 0.0));
        }
        if (q >= rawQubits_.size())
            rawQubits_.resize(static_cast<std::size_t>(q) + 1);
        rawQubits_[q] = std::move(r);
    }
    return {};
}

Status NoiseModel::readGates(const Json& data) {
    if (auto gates = data.find("gates"); gates != data.end()) {
        QXL_TRY(objectAt(*gates, "noise.gates"));
        for (auto g = gates->begin(); g != gates->end(); ++g) {
            QXL_TRY(objectAt(g.value(), "noise.gates." + g.key()));
            for (auto t = g->begin(); t != g->end(); ++t) {
                const std::string path = "noise.gates." + g.key() + "." + t.key();
                const Json& e = t.value();
                QXL_TRY(objectAt(e, path));
                GateRecord r;
                QXL_TRY_ASSIGN(r.qubits, targetsFrom(t.key(), path, 2));
                r.extra = unknownFields(
                    e, {"error", "duration_ns", "coherent_fraction", "leakage", "seepage"});
                QXL_TRY_ASSIGN(r.error, number(e, path, "error", 0.0));
                QXL_TRY_ASSIGN(r.durationNs, number(e, path, "duration_ns", 0.0));
                QXL_TRY_ASSIGN(r.coherentFraction, number(e, path, "coherent_fraction", 0.0));
                QXL_TRY_ASSIGN(r.leakage, number(e, path, "leakage", 0.0));
                if (e.contains("seepage")) {
                    QXL_TRY_ASSIGN(const double s, number(e, path, "seepage", 0.0));
                    r.seepage = s;
                }
                rawGates_[g.key()][targetKey(r.qubits)] = std::move(r);
            }
        }
    }
    if (auto edges = data.find("edges"); edges != data.end()) {
        QXL_TRY(objectAt(*edges, "noise.edges"));
        for (auto it = edges->begin(); it != edges->end(); ++it) {
            const std::string path = "noise.edges." + it.key();
            QXL_TRY(objectAt(it.value(), path));
            QXL_TRY_ASSIGN(const auto ends, targetsFrom(it.key(), path, 2));
            if (ends.size() != 2)
                return fail(err::BadJson,
                            std::format("{}: an edge key names two qubits 'a-b'", path));
            EdgeRecord r{ends[0], ends[1], 0.0, unknownFields(it.value(), {"zz_hz"})};
            QXL_TRY_ASSIGN(r.zzHz, number(it.value(), path, "zz_hz", 0.0));
            rawEdges_[targetKey(ends)] = std::move(r);
        }
    }
    return {};
}

} // namespace qlab::noise
