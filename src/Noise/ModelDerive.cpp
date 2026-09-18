// Spec 08 §4.1 — per-gate error budget (depolarizing = d r/(d−1) minus the relaxation and coherent
// parts), edges, and the channel objects handed out by the queries.
#include "Noise/Catalogue.hpp"
#include "Noise/Model.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::noise {
namespace {
using Replacements = std::map<std::string, double>;

std::optional<double> replacement(const Replacements* rep, const char* key) {
    if (!rep) return std::nullopt;
    auto it = rep->find(key);
    return it == rep->end() ? std::nullopt : std::optional<double>(it->second);
}
Status checkRange(double v, double lo, double hi, const std::string& path) {
    if (!(v >= lo && v <= hi)) return fail(err::InvalidParameter, std::format("{} = {} is outside [{}, {}]", path, v, lo, hi));
    return {};
}
// Edge replacements may be keyed "a-b" or "b-a"; both are merged, the stored orientation winning.
Replacements edgeReplacements(const Overrides& o, std::uint32_t a, std::uint32_t b) {
    Replacements merged;
    if (auto it = o.replaceEdge.find(std::format("{}-{}", b, a)); it != o.replaceEdge.end()) merged = it->second;
    if (auto it = o.replaceEdge.find(std::format("{}-{}", a, b)); it != o.replaceEdge.end())
        for (const auto& [field, v] : it->second) merged[field] = v;
    return merged;
}

// Depolarizing equivalent of thermal_relaxation on every target over `t` (spec 08 §4.1, T10 §2.3).
// Per qubit 1 − F_e = (3/2)(1 − F_avg) with T04 (4.6); F_e multiplies over the tensor product and
// p = d²(1 − F_e)/(d² − 1), accumulated as complements so that p ≈ 1e-4 keeps full precision.
double relaxationDepolarizing(std::span<const std::uint32_t> targets, double t, std::span<const QubitNoise> qubits) {
    if (!(t > 0.0)) return 0.0;
    double complement = 0.0; // 1 − Π F_e,q
    for (std::uint32_t q : targets) {
        const double e = 1.5 * channels::thermalRelaxationInfidelity(qubits[q].t1S, qubits[q].t2S, t);
        complement = complement + e - complement * e;
    }
    const double d2 = std::ldexp(1.0, 2 * static_cast<int>(targets.size()));
    return d2 / (d2 - 1.0) * complement;
}

// Generator of the calibrated rotation, in target order (T04 §9.2): cross-resonance Z⊗X with Z on
// the control, cz ZZ, Mølmer–Sørensen and iSWAP-family XX.
std::string_view overRotationAxis(std::string_view gate, std::size_t arity) {
    if (arity == 1) return (gate == "ry" || gate == "y") ? "Y" : "X";
    if (gate == "cz") return "ZZ";
    if (gate == "ms" || gate == "rxx" || gate == "siswap" || gate == "iswap") return "XX";
    return "ZX";
}
} // namespace

Status NoiseModel::deriveGates(const Overrides& o, Derived& out) const {
    for (const auto& [name, byKey] : rawGates_) {
        for (const auto& [key, rec] : byKey) {
            const std::string path = std::format("gates.{}.{}", name, key);
            const std::size_t m = rec.qubits.size();
            if (m == 0 || m > 2) return fail(err::InvalidParameter, std::format("{}: gates act on 1 or 2 qubits, {} given", path, m));
            for (std::uint32_t q : rec.qubits)
                if (q >= out.qubits.size()) return fail(err::UnknownTarget, std::format("{}: qubit {} is not in the model", path, q));
            Replacements edgeFields;
            const Replacements* rep = nullptr;
            if (m == 1) {
                if (auto it = o.replaceQubit.find(rec.qubits[0]); it != o.replaceQubit.end()) rep = &it->second;
            } else {
                edgeFields = edgeReplacements(o, rec.qubits[0], rec.qubits[1]);
                rep = &edgeFields;
            }
            const bool one = m == 1;
            GateEntry entry;
            GateNoise& g = entry.noise;
            g.gate = name;
            g.qubits = rec.qubits;
            double r = replacement(rep, one ? "gate_error_1q" : "gate_error_2q").value_or(rec.error * o.scaleGateError);
            if (r > 1.0 && std::isfinite(r)) {
                out.warnings.push_back(std::format("warn: gate error {:.4g} for {} on {} exceeds 1; clamped", r, name, key));
                r = 1.0;
            }
            QXL_TRY(checkRange(r, 0.0, 1.0, path + ".error"));
            g.errorR = r;
            const double ns = replacement(rep, one ? "duration_1q_ns" : "duration_ns").value_or(rec.durationNs);
            if (!std::isfinite(ns) || ns < 0.0)
                return fail(err::InvalidParameter, std::format("{}.duration_ns = {} must be finite and >= 0", path, ns));
            g.durationS = ns * 1e-9;
            g.coherentFraction = replacement(rep, one ? "coherent_fraction_1q" : "coherent_fraction_2q").value_or(rec.coherentFraction);
            QXL_TRY(checkRange(g.coherentFraction, 0.0, 1.0, path + ".coherent_fraction"));
            g.leakage = replacement(rep, one ? "leakage_1q" : "leakage_2q").value_or(rec.leakage);
            g.seepage = rec.seepage.value_or(g.leakage); // p_S = p_L unless given (spec 08 §4.1)
            QXL_TRY(checkRange(g.leakage, 0.0, 1.0, path + ".leakage"));
            QXL_TRY(checkRange(g.seepage, 0.0, 1.0, path + ".seepage"));

            const auto arity = static_cast<std::uint32_t>(m);
            const double rCoherent = g.coherentFraction * r;
            g.pTotal = channels::depolarizingFromGateError(r, arity);
            g.pRelaxation = relaxationDepolarizing(g.qubits, g.durationS, out.qubits);
            g.pCoherent = channels::depolarizingFromGateError(rCoherent, arity);
            g.overRotationRad = channels::overRotationAngleForInfidelity(rCoherent, arity);
            const double residual = g.pTotal - g.pRelaxation - g.pCoherent;
            g.clamped = residual < 0.0;
            g.depolarizing = std::max(0.0, residual);
            if (g.pRelaxation > g.pTotal)
                out.warnings.push_back(std::format("warn: relaxation alone exceeds reported gate error for {} on {} "
                                                   "(p_relax = {:.4e} > p = {:.4e}); depolarizing set to 0",
                                                   name, key, g.pRelaxation, g.pTotal));
            else if (g.clamped)
                out.warnings.push_back(std::format("warn: relaxation plus coherent error exceed reported gate error for {} on {} "
                                                   "(p_relax + p_coherent = {:.4e} > p = {:.4e}); depolarizing set to 0",
                                                   name, key, g.pRelaxation + g.pCoherent, g.pTotal));
            out.gates[name][key] = std::move(entry);
        }
    }
    static constexpr const char* kEdgeFields[] = {"zz_hz", "gate_error_2q", "duration_ns", "coherent_fraction_2q", "leakage_2q"};
    for (const auto& [key, fields] : o.replaceEdge)
        for (const auto& [field, v] : fields)
            if (std::find(std::begin(kEdgeFields), std::end(kEdgeFields), field) == std::end(kEdgeFields))
                out.warnings.push_back(std::format("warn: overrides.replace.edges.{}.{} is not a known field; ignored", key, field));
    for (const auto& [key, rec] : rawEdges_) {
        if (rec.a >= out.qubits.size() || rec.b >= out.qubits.size())
            return fail(err::UnknownTarget, std::format("edges.{}: qubit outside the model", key));
        const Replacements fields = edgeReplacements(o, rec.a, rec.b);
        const double zz = replacement(&fields, "zz_hz").value_or(rec.zzHz);
        if (!std::isfinite(zz)) return fail(err::InvalidParameter, std::format("edges.{}.zz_hz must be finite", key));
        out.edges[key] = EdgeEntry{EdgeNoise{rec.a, rec.b, zz}, nullptr};
    }
    return {};
}

Status NoiseModel::buildChannels(Derived& d) const {
    auto set = [](ChannelPtr& dst, Result<ChannelPtr> r) -> Status {
        if (!r) return std::unexpected(std::move(r.error()));
        dst = std::move(*r);
        return {};
    };
    d.channels.assign(d.qubits.size(), QubitChannels{});
    for (std::size_t q = 0; q < d.qubits.size(); ++q) {
        const QubitNoise& qn = d.qubits[q];
        QubitChannels& c = d.channels[q];
        if (std::isfinite(qn.t1S) || std::isfinite(qn.t2S))
            QXL_TRY(set(c.relaxation, thermalRelaxationChannel(qn.t1S, qn.t2S, qn.pThermal)));
        if (qn.driftSigmaHz > 0.0) QXL_TRY(set(c.drift, driftChannel(qn.driftSigmaHz)));
        if (qn.pThermal > 0.0) QXL_TRY(set(c.preparation, thermalPreparationChannel(qn.pThermal)));
        if (qn.resetError > 0.0) QXL_TRY(set(c.reset, resetErrorChannel(qn.resetError)));
        if (qn.readoutDephasing > 0.0 && std::isfinite(qn.t2S))
            QXL_TRY(set(c.measurement, measurementDephasingChannel(qn.t2S, qn.readoutDephasing)));
    }
    for (auto& [name, byKey] : d.gates)
        for (auto& [key, e] : byKey) {
            const GateNoise& g = e.noise;
            const auto arity = static_cast<std::uint32_t>(g.qubits.size());
            if (g.depolarizing > 0.0) QXL_TRY(set(e.depolarizing, depolarizingChannel(arity, g.depolarizing)));
            if (g.overRotationRad != 0.0)
                QXL_TRY(set(e.overRotation, overRotationChannel(overRotationAxis(name, arity), g.overRotationRad)));
            if (levels_ == 3 && (g.leakage > 0.0 || g.seepage > 0.0)) QXL_TRY(set(e.leakage, leakageChannel(g.leakage, g.seepage)));
        }
    for (auto& [key, e] : d.edges)
        if (e.noise.zzHz != 0.0) QXL_TRY(set(e.zz, zzCrosstalkChannel(e.noise.zzHz)));
    return {};
}

bool NoiseModel::isPauliOnly() const {
    // Depolarizing, reset, preparation and every dephasing channel (phase damping, the shot-averaged
    // drift) are Pauli channels. Amplitude damping (finite T1), coherent rotations, ZZ and leakage
    // are not, so a model carrying any of them needs the Model-class twirl (spec 08 §7.3).
    if (!overrides_.disabled(id::ThermalRelaxation))
        for (const auto& qn : qubits_)
            if (std::isfinite(qn.t1S)) return false;
    for (const auto& [name, byKey] : gates_)
        for (const auto& [key, e] : byKey) {
            if (e.overRotation && !overrides_.disabled(id::OverRotation)) return false;
            if (e.leakage && !overrides_.disabled(id::Leakage)) return false;
        }
    if (!overrides_.disabled(id::ZzCrosstalk))
        for (const auto& [key, e] : edges_)
            if (e.zz) return false;
    return true;
}

std::vector<double> NoiseModel::drawShotDetunings(core::Random& rng) const {
    std::vector<double> out(qubits_.size(), 0.0);
    for (std::size_t q = 0; q < qubits_.size(); ++q) out[q] = sampleDetuning(qubits_[q].driftSigmaHz, rng);
    return out;
}

} // namespace qlab::noise
