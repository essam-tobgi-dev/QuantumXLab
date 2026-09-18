// Spec 10 §6.6, T06 §6 — the Mølmer–Sørensen bichromatic drive on an ion pair.
#include "Pulse/Errors.hpp"
#include "Pulse/Library.hpp"
#include <cmath>
#include <format>
#include <numbers>
#include <set>

namespace qlab::pulse {
namespace {
constexpr double kPi = std::numbers::pi;

double jsonNumber(const core::Json& j, const char* key, double fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<double>() : fallback;
}

// The unit envelope as the DAC holds it: e_k = e(k·dt) on [k·dt, (k+1)·dt) (spec 10 §1).
std::vector<double> heldEnvelope(double T, double edge, double dt) {
    const auto n = static_cast<std::size_t>(std::llround(T / dt));
    std::vector<double> e(n);
    for (std::size_t k = 0; k < n; ++k) e[k] = gaussianSquareShape(static_cast<double>(k) * dt, T, edge / 2.0, edge);
    return e;
}

// R(δ) = Re[e^{−iδc} ∫ e(t) e^{iδt} dt] of the held envelope. With e_0 = 0 and e_k = e_{n−k} the
// held envelope is symmetric about c = (T + dt)/2, the imaginary part vanishes, and a zero of R is
// an exactly closed phase-space loop, α(τ) = 0 of T06 (11.1). Runs of equal samples telescope.
double closureResidual(const std::vector<double>& e, double dt, double delta, double c) {
    double acc = 0.0;
    std::size_t k = 0;
    while (k < e.size()) {
        std::size_t j = k + 1;
        while (j < e.size() && e[j] == e[k]) ++j;
        if (e[k] != 0.0) {
            const double a = delta * (static_cast<double>(k) * dt - c);
            const double b = delta * (static_cast<double>(j) * dt - c);
            acc += e[k] * 2.0 * std::cos(0.5 * (a + b)) * std::sin(0.5 * (b - a)) / delta;
        }
        k = j;
    }
    return acc;
}
} // namespace

// Closing the loop of the envelope actually played: for a square pulse the K-th zero of R is
// 2πK/τ (spec 10 §6.6); the Gaussian edges move it up by a few per cent, toward 2πK/(τ − 2·edge).
Result<PulseLibrary::MsShape> PulseLibrary::msShape(double durationS) const {
    const std::int64_t key = psFromSeconds(durationS).value;
    if (auto it = msShapes_.find(key); it != msShapes_.end()) return it->second;

    const core::Json& tpl = templates_.contains("ms_bichromatic") ? templates_["ms_bichromatic"] : core::Json::object();
    MsShape shape;
    shape.durationS = durationS;
    shape.edgeS = jsonNumber(tpl, "edge_us", 10.0) * 1e-6;
    shape.loops = static_cast<int>(jsonNumber(tpl, "K", 1.0));
    if (shape.loops < 1 || 2.0 * shape.edgeS >= durationS)
        return fail(kErrMsClosure, std::format("ms_bichromatic: K = {} and {} us edges do not fit a {} us gate",
                                               shape.loops, shape.edgeS * 1e6, durationS * 1e6));
    const double dt = secondsOf(dt_);
    const auto e = heldEnvelope(durationS, shape.edgeS, dt);
    const double c = 0.5 * (static_cast<double>(e.size()) * dt + dt);
    double lo = 2.0 * kPi * shape.loops / durationS;
    double hi = 2.0 * kPi * shape.loops / (durationS - 2.0 * shape.edgeS);
    double rLo = closureResidual(e, dt, lo, c);
    const double rHi = closureResidual(e, dt, hi, c);
    if (!(rLo * rHi < 0.0))
        return fail(kErrMsClosure, std::format("ms_bichromatic: no loop-closing detuning between {:.6g} and {:.6g} "
                                               "rad/s for a {} us gate", lo, hi, durationS * 1e6));
    for (int it = 0; it < 200 && hi - lo > 1e-15 * hi; ++it) {
        const double mid = 0.5 * (lo + hi);
        const double rMid = closureResidual(e, dt, mid, c);
        if ((rMid < 0.0) == (rLo < 0.0)) { lo = mid; rLo = rMid; }
        else hi = mid;
    }
    shape.delta = 0.5 * (lo + hi);
    const auto integrals = physics::msIntegrals(e, dt, shape.delta);
    shape.phaseIntegral = integrals.phi;
    double area = 0.0;
    for (double v : e) area += v * dt;
    if (std::abs(integrals.alpha) > 1e-9 * area || !(shape.phaseIntegral > 0.0))
        return fail(kErrMsClosure,
                    std::format("ms_bichromatic: the loop does not close at δ = {:.6g} rad/s (residual |α|/g = {:.3g} s "
                                "against an envelope area of {:.3g} s)", shape.delta, std::abs(integrals.alpha), area));
    return shape;
}

Result<void> PulseLibrary::prepareMsShapes() {
    msShapes_.clear();
    if (!isIonDevice() || !templates_.contains("ms_bichromatic")) return {};
    std::set<std::int64_t> durations;
    const double nominalS = jsonNumber(templates_["ms_bichromatic"], "T_us", 200.0) * 1e-6;
    for (auto const& [key, d] : defcals_) {
        if (d.ref != "ms_bichromatic") continue;
        double durationS = nominalS;
        if (!d.paramsFrom.empty()) {
            QXL_TRY_ASSIGN(const double ns, path(d.paramsFrom + ".duration_ns"));
            durationS = ns * 1e-9;
        }
        durations.insert(quantise(durationS, true).value);
    }
    for (auto ps : durations) {
        QXL_TRY_ASSIGN(MsShape shape, msShape(secondsOf(Picoseconds{ps})));
        msShapes_.emplace(ps, shape);
    }
    return {};
}

Result<MsParams> PulseLibrary::msParams(std::uint32_t i, std::uint32_t j, double theta) const {
    const std::uint32_t ab[2] = {i, j};
    const Defcal* d = find("ms", std::span<const std::uint32_t>(ab, 2));
    if (!d) return fail(kErrNoDefcal, std::format("no 'ms' defcal on ions {}-{}", i, j));
    if (!templates_.contains("ms_bichromatic")) return fail(kErrLibrary, "pulses.json has no 'ms_bichromatic' template");
    const core::Json& tpl = templates_["ms_bichromatic"];

    MsParams p;
    p.theta = theta;
    p.loops = static_cast<int>(jsonNumber(tpl, "K", 1.0));
    double durationS = jsonNumber(tpl, "T_us", 200.0) * 1e-6;
    if (!d->paramsFrom.empty()) {
        QXL_TRY_ASSIGN(const double ns, path(d->paramsFrom + ".duration_ns"));
        durationS = ns * 1e-9;
    }
    p.durationS = secondsOf(quantise(durationS, true));
    QXL_TRY_ASSIGN(const MsShape shape, msShape(p.durationS));
    p.edgeS = shape.edgeS;
    if (tpl.contains("mode")) {
        p.modeAxis = tpl["mode"].value("axis", std::string{"axial"});
        p.modeIndex = tpl["mode"].value("index", 0);
    }
    if (auto f = path(std::format("cal.motional.{}_modes_mhz.{}", p.modeAxis, p.modeIndex)); f)
        p.modeFrequencyHz = *f * 1e6;
    QXL_TRY_ASSIGN(p.etaI, path(std::format("cal.qubits.{}.lamb_dicke", i)));
    QXL_TRY_ASSIGN(p.etaJ, path(std::format("cal.qubits.{}.lamb_dicke", j)));
    const double eta2 = p.etaI * p.etaJ;
    if (!(eta2 > 0.0)) return fail(kErrLibrary, std::format("ms({},{}): Lamb–Dicke factors must be positive", i, j));

    // Square-envelope reference (spec 10 §6.6, T06 (6.6)): δτ = 2πK, Ω = √(θδ/(η_i η_j τ)).
    p.squareDetuningRadPerS = physics::msDetuningForDuration(p.durationS, p.loops);
    p.omegaSquareRadPerS = physics::msAmplitudeForAngle(kPi / 2.0, p.etaI, p.etaJ, p.durationS, p.squareDetuningRadPerS);
    // Played envelope: |θ| = η_i η_j Ω² J(δ) with the closed-loop δ (T06 (6.2), (6.5)).
    p.detuningRadPerS = theta >= 0.0 ? -shape.delta : shape.delta;
    p.omegaMaxRadPerS = std::sqrt((kPi / 2.0) / (eta2 * shape.phaseIntegral));
    p.omegaRadPerS = std::sqrt(std::abs(theta) / (eta2 * shape.phaseIntegral));
    p.amplitude = p.omegaRadPerS / p.omegaMaxRadPerS;
    return p;
}

// The table stores the square-envelope Ω at |θ| = π/2 per pair. A table built with the extra π of
// the superseded spec 10 §6.6 line is smaller by √π and is refused (regenerate with tools/gencal.py).
Result<void> PulseLibrary::checkMsTable() const {
    for (auto const& [key, d] : defcals_) {
        if (d.ref != "ms_bichromatic" || key.qubits.size() != 2) continue;
        const double stored = jsonNumber(d.extra, "omega_rad_s_at_pi_2", 0.0);
        if (stored <= 0.0) continue;
        QXL_TRY_ASSIGN(const MsParams p, msParams(key.qubits[0], key.qubits[1], kPi / 2.0));
        const double ratio = stored / p.omegaSquareRadPerS;
        if (std::abs(ratio - 1.0) > 1e-6)
            return fail(kErrLibrary, std::format("{}: stored omega_rad_s_at_pi_2 = {:.7g} rad/s but T06 (6.6) gives "
                                                 "{:.7g} rad/s (ratio {:.6f}); regenerate pulses.json",
                                                 key.toString(), stored, p.omegaSquareRadPerS, ratio));
    }
    return {};
}

Result<Schedule> PulseLibrary::buildMs(const Defcal& d, const ParamMap& params) const {
    if (d.key.qubits.size() != 2) return fail(kErrLibrary, std::format("defcal {} needs two ions", d.key.toString()));
    const auto it = params.find("theta");
    const double theta = it == params.end() ? kPi / 2.0 : it->second;
    QXL_TRY_ASSIGN(const MsParams p, msParams(d.key.qubits[0], d.key.qubits[1], theta));
    if (p.amplitude > 1.0 + 1e-12)
        return fail(kErrWaveform, std::format("ms({},{}) at θ = {:.4f} needs {:.3f}× the full-scale drive; |θ| ≤ π/2 "
                                              "is the calibrated range", d.key.qubits[0], d.key.qubits[1], theta,
                                              p.amplitude));
    Waveform wf = Waveform::gaussianSquare(p.durationS, p.edgeS / 2.0, p.edgeS, p.amplitude);
    QXL_TRY(wf.validate());
    // The ms[i,j] frame frequency is the symmetric tone offset μ/2π: tones at f_q ± μ/2π with
    // μ = ω_mode − δ, which puts the sign of θ (the sign of δ, T06 (6.5)) into the schedule.
    const ChannelId ch = ChannelId::bichromatic(d.key.qubits[0], d.key.qubits[1]);
    const double muHz = p.modeFrequencyHz - p.detuningRadPerS / (2.0 * kPi);
    Schedule s = emptySchedule();
    s.insert(FrameOp{ch, {}, FrameOp::Op::SetFrequency, muHz}, Picoseconds{0});
    s.insert(Play{ch, std::move(wf), {}}, Picoseconds{0});
    return s;
}

} // namespace qlab::pulse
