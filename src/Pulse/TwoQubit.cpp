// Spec 10 §6.3–§6.5 — the parametrised two-qubit templates: echoed cross-resonance and the
// flux CZ/iSWAP. The Mølmer–Sørensen drive (§6.6) is in Ms.cpp.
#include "Pulse/Errors.hpp"
#include "Pulse/Library.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::pulse {
namespace {
constexpr double kPi = std::numbers::pi;

double jsonNumber(const core::Json& j, const char* key, double fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<double>() : fallback;
}
} // namespace

// ---- 6.3 echoed cross-resonance ---------------------------------------------------------

Result<CrParams> PulseLibrary::crParams(std::uint32_t c, std::uint32_t t) const {
    const std::uint32_t pair[2] = {c, t};
    const std::span<const std::uint32_t> edge(pair, 2);
    // `ecr` ships without the tomography amplitudes; they live on the `cx` defcal of the edge.
    const Defcal* amps = find("cx", edge);
    const Defcal* any = amps ? amps : find("ecr", edge);
    if (!any)
        return fail(kErrNoDefcal, std::format("no cross-resonance defcal on edge {}-{}", c, t));

    CrParams p;
    p.control = c;
    p.target = t;
    const std::string from =
        any->paramsFrom.empty() ? std::format("cal.edges.{}-{}", c, t) : any->paramsFrom;
    QXL_TRY_ASSIGN(const double durNs, path(from + ".duration_ns"));
    p.calibratedDurationS = durNs * 1e-9;

    const core::Json& tpl =
        templates_.contains("cr_echo") ? templates_["cr_echo"] : core::Json::object();
    p.sigmaS = jsonNumber(tpl, "sigma_ns", 16.0) * 1e-9;
    p.riseS = jsonNumber(tpl, "rise_ns", 32.0) * 1e-9;
    if (amps) {
        p.ampCr = jsonNumber(amps->extra, "amp_cr", 0.0);
        p.ampCancel = jsonNumber(amps->extra, "amp_cancel", 0.0);
        p.phaseCancel = jsonNumber(amps->extra, "phase_cancel_rad", 0.0);
    }

    QXL_TRY_ASSIGN(const Waveform echo, singleQubitWaveform("x", c));
    p.echoDuration = quantise(echo.duration, true);
    // spec 10 §6.3: T_CR = cal.edges.c-t.duration_ns/2 − T_x, so 2·T_CR + 2·T_x is the calibrated
    // gate length, to within the granule the grid of §1 imposes on each half.
    p.crDuration = quantise(p.calibratedDurationS / 2.0 - secondsOf(p.echoDuration), true);
    p.totalDuration = Picoseconds{2 * p.crDuration.value + 2 * p.echoDuration.value};
    if (p.calibratedDurationS / 2.0 - secondsOf(p.echoDuration) <
        dt_.value * 1e-12 * minPulseSamples_)
        return fail(
            Error(kErrMin,
                  std::format("edge {}-{}: the calibrated {:.1f} ns leaves no room for two "
                              "{}-sample CR halves beside the {:.3f} ns echo pulses",
                              c, t, durNs, minPulseSamples_, secondsOf(p.echoDuration) * 1e9))
                .withId("E_PULSE_MIN"));
    return p;
}

Result<Schedule> PulseLibrary::buildCrEcho(const Defcal& d, bool bare) const {
    if (d.key.qubits.size() != 2)
        return fail(kErrLibrary, std::format("defcal {} needs two qubits", d.key.toString()));
    const std::uint32_t c = d.key.qubits[0], t = d.key.qubits[1];
    QXL_TRY_ASSIGN(const CrParams p, crParams(c, t));
    QXL_TRY_ASSIGN(const Waveform echo, singleQubitWaveform("x", c));

    const double tcr = secondsOf(p.crDuration);
    const ChannelId cr = ChannelId::control(c, t);
    const ChannelId driveT = ChannelId::drive(t);
    const ChannelId driveC = ChannelId::drive(c);
    auto crPulse = [&](double sign) {
        return Waveform::gaussianSquare(tcr, p.sigmaS, p.riseS, sign * p.ampCr);
    };
    auto cancelPulse = [&](double sign) {
        return Waveform::gaussianSquare(tcr, p.sigmaS, p.riseS, sign * p.ampCancel, p.phaseCancel);
    };
    QXL_TRY(crPulse(1.0).validate());
    QXL_TRY(cancelPulse(1.0).validate());

    Schedule s = emptySchedule();
    const Picoseconds tx = p.echoDuration;
    const Picoseconds half = p.crDuration;
    const Picoseconds secondEcho{2 * half.value + tx.value};
    // 1. CR tone on the control line at the target frequency plus the IX/IY cancellation (T05 §8).
    s.insert(Play{cr, crPulse(+1.0), {}}, Picoseconds{0});
    s.insert(Play{driveT, cancelPulse(+1.0), {}}, Picoseconds{0});
    // 2. echo π pulse on the control.
    s.insert(Play{driveC, echo, {}}, half);
    // 3. the same pair with reversed sign.
    s.insert(Play{cr, crPulse(-1.0), {}}, half + tx);
    s.insert(Play{driveT, cancelPulse(-1.0), {}}, half + tx);
    // 4. second echo.
    s.insert(Play{driveC, echo, {}}, secondEcho);
    if (bare)
        return s; // `ecr`: steps 1–4 only (spec 10 §6.3).

    // 5. CNOT = e^{iπ/4} Rz_c(−π/2) · [Rz_t(π) sx_t Rz_t(π)] · ZX(π/2)  (T05 (8.3), T02 §2.3).
    // A virtual Rz(θ) on q is shift_phase(−θ) on d[q] and on every CR frame targeting q (spec 10
    // §3). The target correction commutes with the control's echo, so it plays beside step 4 and
    // the gate keeps the calibrated duration 2·T_CR + 2·T_x (the length the circuit scheduler of
    // spec 14 §9 assigns to cx).
    auto virtualZ = [&](std::uint32_t q, double theta, Picoseconds at) {
        s.insert(FrameOp{ChannelId::drive(q), {}, FrameOp::Op::ShiftPhase, -theta}, at);
        for (auto const& [name, f] : frames_) {
            (void)name;
            if (f.channel.kind == ChannelKind::Control && f.channel.b == q)
                s.insert(FrameOp{f.channel, {}, FrameOp::Op::ShiftPhase, -theta}, at);
        }
    };
    QXL_TRY_ASSIGN(const Waveform sx, singleQubitWaveform("sx", t));
    const Picoseconds sxLength = quantise(sx.duration, true);
    virtualZ(t, kPi, secondEcho);
    s.insert(Play{driveT, sx, {}}, secondEcho);
    virtualZ(t, kPi, secondEcho + sxLength);
    virtualZ(c, -kPi / 2.0, std::max(p.totalDuration, secondEcho + sxLength));
    return s;
}

// ---- 6.4 / 6.5 flux gates ---------------------------------------------------------------

Result<Schedule> PulseLibrary::buildFluxGate(const Defcal& d, std::string_view templateName) const {
    const std::string tplName(templateName);
    if (!templates_.contains(tplName))
        return fail(kErrLibrary, std::format("pulses.json has no '{}' template", tplName));
    const core::Json& tpl = templates_[tplName];

    const std::string chName = d.extra.value("flux_channel", std::string{});
    if (chName.empty())
        return fail(
            Error(kErrNoChannel, std::format("defcal {} has no 'flux_channel'", d.key.toString()))
                .withId("E_NO_CHANNEL"));
    QXL_TRY_ASSIGN(const ChannelId ch, parseChannel(chName));
    QXL_TRY_ASSIGN(const WaveformKind kind,
                   waveformKindFromName(tpl.value("wf", std::string{"slepian"})));

    double durationS = jsonNumber(tpl, "T_ns", 40.0) * 1e-9;
    if (!d.paramsFrom.empty()) {
        QXL_TRY_ASSIGN(const double calNs, path(d.paramsFrom + ".duration_ns"));
        durationS = calNs * 1e-9;
    }
    const Picoseconds dur = quantise(durationS, true);

    Waveform wf;
    wf.kind = kind;
    wf.duration = secondsOf(dur);
    wf.amplitude = jsonNumber(d.extra, "amp", jsonNumber(tpl, "amp", 0.0));
    wf.lambda1 = jsonNumber(tpl, "lambda1", 0.0);
    wf.rise = jsonNumber(tpl, "edge_ns", jsonNumber(tpl, "rise_ns", 0.0)) * 1e-9;
    wf.sigma = jsonNumber(tpl, "sigma_ns", 0.0) * 1e-9;
    if (kind == WaveformKind::GaussianSquare && wf.sigma <= 0.0)
        wf.sigma = wf.rise / 2.0;
    QXL_TRY(wf.validate());

    Schedule s = emptySchedule();
    s.insert(Play{ch, std::move(wf), {}}, Picoseconds{0});
    return s;
}

} // namespace qlab::pulse
