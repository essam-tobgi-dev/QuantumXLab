// Spec 08 §4.1–4.2 — overrides, data access, and the transactional rebuild: stored calibration
// (document units) → effective per-qubit parameters in SI units.
#include "Noise/Model.hpp"
#include "Noise/Catalogue.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::noise {
namespace {
using Replacements = std::map<std::string, double>;

double pick(const Replacements* rep, const char* key, double fallback) {
    if (!rep)
        return fallback;
    auto it = rep->find(key);
    return it == rep->end() ? fallback : it->second;
}
bool has(const Replacements* rep, const char* key) {
    return rep && rep->contains(key);
}

Status checkRange(double v, double lo, double hi, const std::string& path) {
    if (!(v >= lo && v <= hi))
        return fail(err::InvalidParameter,
                    std::format("{} = {} is outside [{}, {}]", path, v, lo, hi));
    return {};
}
Status checkLifetime(double v, const std::string& path) { // +inf = absent process
    if (!(v > 0.0))
        return fail(err::InvalidParameter, std::format("{} = {} must be positive", path, v));
    return {};
}
Status checkScale(double v, bool allowZero, const char* name) {
    if (!std::isfinite(v) || v < 0.0 || (!allowZero && v == 0.0))
        return fail(err::InvalidParameter, std::format("overrides.{} = {} must be finite and {}",
                                                       name, v, allowZero ? ">= 0" : "> 0"));
    return {};
}
} // namespace

bool Overrides::disabled(std::string_view channelId) const {
    return std::find(disable.begin(), disable.end(), channelId) != disable.end();
}
void Overrides::setEnabled(std::string_view channelId, bool on) {
    auto it = std::find(disable.begin(), disable.end(), channelId);
    if (on && it != disable.end())
        disable.erase(it);
    if (!on && it == disable.end())
        disable.emplace_back(channelId);
}

std::string NoiseModel::targetKey(std::span<const std::uint32_t> qubits) {
    std::string s;
    for (std::size_t i = 0; i < qubits.size(); ++i)
        s += (i ? "-" : "") + std::to_string(qubits[i]);
    return s;
}
std::string NoiseModel::edgeKey(std::uint32_t a, std::uint32_t b) {
    return std::format("{}-{}", std::min(a, b), std::max(a, b));
}

const QubitNoise* NoiseModel::qubit(QubitIndex q) const {
    return q.get() < qubits_.size() ? &qubits_[q.get()] : nullptr;
}

const GateNoise* NoiseModel::gate(std::string_view gateName,
                                  std::span<const QubitIndex> qubits) const {
    const GateEntry* e = findGate(gateName, qubits);
    return e ? &e->noise : nullptr;
}

std::vector<const GateNoise*> NoiseModel::gates() const {
    std::vector<const GateNoise*> out;
    for (const auto& [name, byKey] : gates_)
        for (const auto& [key, entry] : byKey)
            out.push_back(&entry.noise);
    return out;
}

const EdgeNoise* NoiseModel::edge(std::uint32_t a, std::uint32_t b) const {
    if (auto it = edges_.find(std::format("{}-{}", a, b)); it != edges_.end())
        return &it->second.noise;
    if (auto it = edges_.find(std::format("{}-{}", b, a)); it != edges_.end())
        return &it->second.noise;
    return nullptr;
}

const NoiseModel::GateEntry* NoiseModel::findGate(std::string_view gateName,
                                                  std::span<const QubitIndex> qubits) const {
    std::vector<std::uint32_t> ids;
    for (auto q : qubits)
        ids.push_back(q.get());
    const std::string key = targetKey(ids);
    auto lookup = [&](std::string_view name) -> const GateEntry* {
        auto g = gates_.find(std::string(name));
        if (g == gates_.end())
            return nullptr;
        if (auto e = g->second.find(key); e != g->second.end())
            return &e->second;
        if (ids.size() == 2) { // a two-qubit entry is stored in calibration order
            const std::uint32_t flipped[2] = {ids[1], ids[0]};
            if (auto e = g->second.find(targetKey(flipped)); e != g->second.end())
                return &e->second;
        }
        return nullptr;
    };
    if (const GateEntry* e = lookup(gateName))
        return e;
    const std::string& fallback = ids.size() == 1   ? fallback1q_
                                  : ids.size() == 2 ? fallback2q_
                                                    : std::string{};
    if (!fallback.empty() && fallback != gateName)
        return lookup(fallback); // spec 08 §8 arity default
    return nullptr;
}

Status NoiseModel::setOverrides(const Overrides& o) {
    QXL_TRY_ASSIGN(Derived d, derive(o));
    overrides_ = o;
    commit(std::move(d));
    return {};
}

Status NoiseModel::setLinePhotonNumbers(std::span<const double> photons) {
    if (photons.size() > rawQubits_.size())
        return fail(err::UnknownTarget, std::format("{} line photon numbers for {} qubits",
                                                    photons.size(), rawQubits_.size()));
    for (std::size_t q = 0; q < photons.size(); ++q)
        if (!std::isfinite(photons[q]) || photons[q] < 0.0)
            return fail(err::InvalidParameter,
                        std::format("line photon number {} of qubit {} must be finite and >= 0",
                                    photons[q], q));
    std::vector<double> previous;
    for (std::size_t q = 0; q < rawQubits_.size(); ++q) {
        previous.push_back(rawQubits_[q].linePhotons);
        rawQubits_[q].linePhotons = q < photons.size() ? photons[q] : 0.0;
    }
    auto d = derive(overrides_);
    if (!d) {
        for (std::size_t q = 0; q < rawQubits_.size(); ++q)
            rawQubits_[q].linePhotons = previous[q];
        return std::unexpected(std::move(d.error()));
    }
    commit(std::move(*d));
    return {};
}

Status NoiseModel::rebuild() {
    QXL_TRY_ASSIGN(Derived d, derive(overrides_));
    commit(std::move(d));
    return {};
}

Result<NoiseModel::Derived> NoiseModel::derive(const Overrides& o) const {
    QXL_TRY(checkScale(o.scaleT1, false, "scale_t1"));
    QXL_TRY(checkScale(o.scaleT2, false, "scale_t2"));
    QXL_TRY(checkScale(o.scaleGateError, true, "scale_gate_error"));
    QXL_TRY(checkScale(o.scaleReadoutError, true, "scale_readout_error"));
    Derived d;
    QXL_TRY(deriveQubits(o, d));
    QXL_TRY(deriveGates(o, d));
    QXL_TRY(buildChannels(d));
    return d;
}

void NoiseModel::commit(Derived&& d) {
    qubits_ = std::move(d.qubits);
    qubitChannels_ = std::move(d.channels);
    gates_ = std::move(d.gates);
    edges_ = std::move(d.edges);
    warnings_ = std::move(d.warnings);
}

Status NoiseModel::deriveQubits(const Overrides& o, Derived& out) const {
    static constexpr const char* kKnown[] = {"t1_us",
                                             "t2_us",
                                             "t2_star_us",
                                             "p_thermal",
                                             "reset_error",
                                             "readout_e01",
                                             "readout_e10",
                                             "readout_duration_ns",
                                             "readout_crosstalk_dephasing",
                                             "gate_error_1q",
                                             "duration_1q_ns",
                                             "coherent_fraction_1q",
                                             "leakage_1q"};
    for (const auto& [q, fields] : o.replaceQubit) {
        if (q >= rawQubits_.size())
            out.warnings.push_back(std::format(
                "warn: overrides.replace.qubits.{} names a qubit outside the model; ignored", q));
        for (const auto& [key, v] : fields)
            if (std::find(std::begin(kKnown), std::end(kKnown), key) == std::end(kKnown))
                out.warnings.push_back(std::format(
                    "warn: overrides.replace.qubits.{}.{} is not a known field; ignored", q, key));
    }
    out.qubits.resize(rawQubits_.size());
    for (std::size_t q = 0; q < rawQubits_.size(); ++q) {
        const QubitRecord& rec = rawQubits_[q];
        auto it = o.replaceQubit.find(static_cast<std::uint32_t>(q));
        const Replacements* rep = it == o.replaceQubit.end() ? nullptr : &it->second;
        const std::string path = std::format("qubits.{}", q);
        QubitNoise& qn = out.qubits[q];

        const double t1Us = pick(rep, "t1_us", rec.t1Us * o.scaleT1);
        const double t2Us = pick(rep, "t2_us", rec.t2Us * o.scaleT2);
        const double t2StarUs = pick(rep, "t2_star_us", rec.t2StarUs * o.scaleT2);
        QXL_TRY(checkLifetime(t1Us, path + ".t1_us"));
        QXL_TRY(checkLifetime(t2Us, path + ".t2_us"));
        QXL_TRY(checkLifetime(t2StarUs, path + ".t2_star_us"));
        qn.t1S = t1Us * 1e-6;
        qn.t2S = t2Us * 1e-6;
        qn.t2StarS = t2StarUs * 1e-6;
        if (qn.t2S > 2.0 * qn.t1S) { // spec 08 §2.3: clamp and warn, never an error
            if (std::isfinite(qn.t2S) && qn.t2S > 2.0 * qn.t1S * (1.0 + 1e-9))
                out.warnings.push_back(std::format(
                    "warn: T2 = {:.6g} us exceeds 2 T1 = {:.6g} us on qubit {}; clamped to 2 T1",
                    t2Us, 2.0 * t1Us, q));
            qn.t2S = 2.0 * qn.t1S; // an absent T2 means no pure dephasing
        }
        if (qn.t2StarS > qn.t2S) { // T2* <= T2 (spec 09 §3)
            if (std::isfinite(qn.t2StarS) && qn.t2StarS > qn.t2S * (1.0 + 1e-9))
                out.warnings.push_back(std::format(
                    "warn: T2* = {:.6g} us exceeds T2 on qubit {}; clamped to T2", t2StarUs, q));
            qn.t2StarS = qn.t2S;
        }
        qn.driftSigmaHz = driftSigmaFromCalibration(qn.t2StarS, qn.t2S); // spec 08 §4.1, (2.5)
        qn.frequencyHz = rec.frequencyGhz * 1e9;

        qn.pThermalCalibration = pick(rep, "p_thermal", rec.pThermal);
        QXL_TRY(checkRange(qn.pThermalCalibration, 0.0, 0.5, path + ".p_thermal"));
        if (!std::isfinite(rec.linePhotons) || rec.linePhotons < 0.0)
            return fail(err::InvalidParameter,
                        std::format("{}.line_photon_number = {} must be finite and >= 0", path,
                                    rec.linePhotons));
        qn.pThermalLine = channels::thermalPopulationFromPhotons(rec.linePhotons);
        qn.pThermal = std::max(qn.pThermalCalibration, qn.pThermalLine); // T08 §7

        qn.resetError = pick(rep, "reset_error", rec.resetError);
        QXL_TRY(checkRange(qn.resetError, 0.0, 1.0, path + ".reset_error"));
        qn.readoutAssignment = rec.assignment;
        if (has(rep, "readout_e01")) {
            const double e = pick(rep, "readout_e01", 0.0);
            qn.readoutAssignment[0] = {1.0 - e, e};
        }
        if (has(rep, "readout_e10")) {
            const double e = pick(rep, "readout_e10", 0.0);
            qn.readoutAssignment[1] = {e, 1.0 - e};
        }
        if (o.scaleReadoutError != 1.0)
            for (std::size_t r = 0; r < 2; ++r) { // scale the error (off-diagonal) of each row
                const double e =
                    std::clamp(qn.readoutAssignment[r][1 - r] * o.scaleReadoutError, 0.0, 1.0);
                qn.readoutAssignment[r][1 - r] = e;
                qn.readoutAssignment[r][r] = 1.0 - e;
            }
        QXL_TRY(
            validateAssignment(toRealMatrix(qn.readoutAssignment), path + ".readout.assignment"));
        const double roNs = pick(rep, "readout_duration_ns", rec.readoutDurationNs);
        if (!std::isfinite(roNs) || roNs < 0.0)
            return fail(
                err::InvalidParameter,
                std::format("{}.readout.duration_ns = {} must be finite and >= 0", path, roNs));
        qn.readoutDurationS = roNs * 1e-9;
        qn.readoutDephasing = pick(rep, "readout_crosstalk_dephasing", rec.readoutDephasing);
        QXL_TRY(checkRange(qn.readoutDephasing, 0.0, 1.0, path + ".readout.crosstalk_dephasing"));
    }
    return {};
}

} // namespace qlab::noise
