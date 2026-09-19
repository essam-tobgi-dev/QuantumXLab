// Spec 08 §4.1 — the calibration → noise-model mapping. Every calibration entry is a
// [value, sigma, source] triple (spec 09 §3); only `value` is read here, the sigma feeds the
// estimator's uncertainty propagation (T12 §7). Values are stored in document units (µs, ns, GHz).
#include "Noise/Model.hpp"
#include <cmath>
#include <format>

namespace qlab::noise {
namespace {
// A calibration slot that was never measured (couplers, spec 09 §2) must not manufacture noise.
double lifetimeUs(const units::Time& t) {
    const double s = t.si();
    return s > 0.0 && std::isfinite(s) ? s * 1e6 : std::numeric_limits<double>::infinity();
}
} // namespace

Result<NoiseModel> NoiseModel::fromCalibration(const hw::Device& dev, const hw::Calibration& cal,
                                               const NoiseOptions& options) {
    if (options.levels != 2 && options.levels != 3)
        return fail(
            err::LevelsMismatch,
            std::format("noise models support 2 or 3 levels, {} requested", options.levels));
    const std::size_t n = std::max(cal.qubits.size(), dev.qubitCount());
    if (options.linePhotonNumbers.size() > n)
        return fail(err::UnknownTarget, std::format("{} line photon numbers for {} qubits",
                                                    options.linePhotonNumbers.size(), n));
    NoiseModel m;
    m.device_ = dev.id;
    m.calibrationStamp_ = cal.timestamp;
    m.levels_ = options.levels;
    m.overrides_ = options.overrides;

    m.rawQubits_.resize(n);
    for (std::size_t q = 0; q < n; ++q) {
        QubitRecord& r = m.rawQubits_[q];
        if (q < options.linePhotonNumbers.size())
            r.linePhotons = options.linePhotonNumbers[q];
        const hw::QubitCal* c = cal.qubit(static_cast<std::uint32_t>(q));
        if (!c || dev.isCoupler(static_cast<std::uint32_t>(q)))
            continue; // noiseless slot
        r.t1Us = lifetimeUs(c->t1.value);
        r.t2Us = lifetimeUs(c->t2echo.value);
        r.t2StarUs = lifetimeUs(c->t2star.value);
        r.frequencyGhz = c->f01.value.si() * 1e-9;
        r.pThermal =
            c->thermalPopulation.value; // the line value is folded in by rebuild (§4.1, T08 §7)
        r.resetError = c->resetError.value;
        r.assignment = c->readoutAssignment; // used as stored (§4.1)
        r.readoutDurationNs = c->readoutDuration.value.si() * 1e9;
        r.readoutDephasing = c->readoutCrosstalkDephasing.value;
    }

    // Gate table: every native gate of the device on every calibrated target (spec 08 §5 "gates").
    auto addGate = [&](const std::string& name, std::vector<std::uint32_t> targets,
                       const std::string& key, double coherentFraction, double leakage) -> Status {
        QXL_TRY_ASSIGN(const units::Time t, cal.gateDuration(name, targets));
        QXL_TRY_ASSIGN(const double r, cal.gateError(name, targets));
        if (t.si() <= 0.0 && r <= 0.0)
            return {}; // virtual gate (rz, id): no noise, no entry
        GateRecord g;
        g.qubits = std::move(targets);
        g.error = r;
        g.durationNs = t.si() * 1e9;
        g.coherentFraction = coherentFraction;
        g.leakage = leakage;
        m.rawGates_[name][key] = std::move(g);
        return {};
    };
    for (const auto& name : dev.gates.single) {
        for (std::uint32_t q = 0; q < dev.qubitCount(); ++q) {
            const hw::QubitCal* c = cal.qubit(q);
            if (dev.isCoupler(q) || !c)
                continue;
            QXL_TRY(addGate(name, {q}, std::to_string(q), c->coherentErrorFraction1q.value,
                            c->leakage1q.value));
        }
        if (m.fallback1q_.empty() && m.rawGates_.contains(name))
            m.fallback1q_ = name;
    }
    for (const auto& name : dev.gates.two) {
        for (const auto& [key, e] : cal.edges)
            QXL_TRY(addGate(name, {e.a, e.b}, std::format("{}-{}", e.a, e.b),
                            e.coherentErrorFraction2q.value, e.leakage2q.value));
        if (m.fallback2q_.empty() && m.rawGates_.contains(name))
            m.fallback2q_ = name;
    }
    for (const auto& [key, e] : cal.edges)
        m.rawEdges_[std::format("{}-{}", e.a, e.b)] =
            EdgeRecord{e.a, e.b, e.zz.value.si(), core::Json::object()};
    for (const auto& line : dev.readout.feedlines)
        m.feedlines_.push_back(line.qubits);

    QXL_TRY(m.rebuild());
    return m;
}

} // namespace qlab::noise
