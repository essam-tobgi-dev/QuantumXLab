// Spec 10 §6 — turning a defcal's `instructions` array into a schedule block.
#include "Pulse/Errors.hpp"
#include "Pulse/Library.hpp"
#include <algorithm>
#include <format>
#include <numbers>
#include <optional>

namespace qlab::pulse {
namespace {
constexpr double kPi = std::numbers::pi;

// A JSON field that may be a number or an expression string; absent → `fallback`.
Result<double> field(const core::Json& j, const char* key, const ParamMap& p, const PathResolver& r,
                     double fallback) {
    if (!j.contains(key)) return fallback;
    return evalJsonValue(j[key], p, r);
}

struct BuildCtx {
    const ParamMap& params;
    PathResolver res;
    Picoseconds dt;
    int granularity;
    int minPulseSamples;
    Picoseconds grid(double seconds, bool isPlay) const {
        return quantise(seconds, dt, granularity, isPlay ? minPulseSamples : 0);
    }
};

// `{"play": {"ch", "wf", "T_ns", "sigma_ns", "rise_ns"|"edge_ns", "beta_ns", "amp", "phase"}}`
Result<Play> playFromJson(const core::Json& j, const BuildCtx& ctx) {
    const std::string chName = j.value("ch", std::string{});
    if (chName.empty()) return fail(kErrLibrary, "play instruction has no 'ch'");
    QXL_TRY_ASSIGN(const ChannelId ch, parseChannel(chName));
    QXL_TRY_ASSIGN(const WaveformKind kind, waveformKindFromName(j.value("wf", std::string{"constant"})));

    QXL_TRY_ASSIGN(const double tNs, field(j, "T_ns", ctx.params, ctx.res, 0.0));
    const Picoseconds dur = ctx.grid(tNs * 1e-9, true);
    if (dur.value <= 0)
        return fail(kErrWaveform, std::format("play on {} has no positive duration", chName));

    Waveform wf;
    wf.kind = kind;
    wf.duration = secondsOf(dur);
    QXL_TRY_ASSIGN(const double riseNs,
                   field(j, j.contains("edge_ns") ? "edge_ns" : "rise_ns", ctx.params, ctx.res, 0.0));
    wf.rise = riseNs * 1e-9;
    // spec 10 §2: the gaussian_square edge is a gaussian of width σ spread over the rise t_r;
    // the shipped tables always store σ = t_r/2, which is the default when σ is absent.
    QXL_TRY_ASSIGN(const double sigmaNs, field(j, "sigma_ns", ctx.params, ctx.res, 0.0));
    wf.sigma = sigmaNs > 0.0 ? sigmaNs * 1e-9 : (kind == WaveformKind::GaussianSquare ? wf.rise / 2.0 : 0.0);
    QXL_TRY_ASSIGN(const double betaNs, field(j, "beta_ns", ctx.params, ctx.res, 0.0));
    wf.beta = betaNs * 1e-9;
    QXL_TRY_ASSIGN(const double tauNs, field(j, "tau_ns", ctx.params, ctx.res, 0.0));
    wf.tau = tauNs * 1e-9;
    QXL_TRY_ASSIGN(wf.lambda1, field(j, "lambda1", ctx.params, ctx.res, 0.0));
    QXL_TRY_ASSIGN(wf.amplitude, field(j, "amp", ctx.params, ctx.res, 1.0));
    QXL_TRY_ASSIGN(wf.phase, field(j, "phase", ctx.params, ctx.res, 0.0));
    QXL_TRY(wf.validate());
    return Play{ch, std::move(wf), Picoseconds{0}};
}

Result<FrameOp> frameOpFromJson(const core::Json& j, FrameOp::Op op, const BuildCtx& ctx) {
    const std::string chName = j.value("ch", std::string{});
    if (chName.empty()) return fail(kErrLibrary, "frame instruction has no 'ch'");
    QXL_TRY_ASSIGN(const ChannelId ch, parseChannel(chName));
    double v = 0.0;
    if (j.contains("value_ghz")) {
        QXL_TRY_ASSIGN(const double ghz, evalJsonValue(j["value_ghz"], ctx.params, ctx.res));
        v = ghz * 1e9;
    } else if (j.contains("value")) {
        QXL_TRY_ASSIGN(v, evalJsonValue(j["value"], ctx.params, ctx.res));
    }
    return FrameOp{ch, Picoseconds{0}, op, v};
}

Result<Acquire> acquireFromJson(const core::Json& j, const BuildCtx& ctx) {
    const std::string chName = j.value("ch", std::string{});
    if (chName.empty()) return fail(kErrLibrary, "acquire instruction has no 'ch'");
    QXL_TRY_ASSIGN(const ChannelId ch, parseChannel(chName));
    QXL_TRY_ASSIGN(const double tNs, field(j, "T_ns", ctx.params, ctx.res, 0.0));
    Acquire a;
    a.ch = ch;
    a.length = ctx.grid(tNs * 1e-9, false);
    a.weights = j.value("weights", std::string{"matched"});
    a.kind = j.value("kind", std::string{"integrate"}) == "photon_count" ? AcquireKind::PhotonCount
                                                                        : AcquireKind::Integrate;
    a.memorySlot = j.value("memory_slot", 0);
    return a;
}
} // namespace

// ---- explicit instruction lists --------------------------------------------------------

Result<Schedule> PulseLibrary::buildFromInstructions(const Defcal& d, const ParamMap& params) const {
    Schedule s = emptySchedule();
    const BuildCtx ctx{params, resolver(), dt_, granularity_, minPulseSamples_};
    if (!d.instructions.is_array())
        return fail(kErrLibrary, std::format("defcal {} has a non-array 'instructions'", d.key.toString()));

    std::optional<Picoseconds> lastPlayStart; // the tone an `acquire` window is referred to
    for (auto const& item : d.instructions) {
        if (!item.is_object() || item.size() == 0)
            return fail(kErrLibrary, std::format("defcal {}: malformed instruction", d.key.toString()));
        const auto it = item.begin();
        const std::string op = it.key();
        const core::Json& body = it.value();

        if (op == "play") {
            QXL_TRY_ASSIGN(auto p, playFromJson(body, ctx));
            lastPlayStart = s.channelEnd(p.ch);
            s.append(std::move(p));
        } else if (op == "delay") {
            QXL_TRY_ASSIGN(const ChannelId ch, parseChannel(body.value("ch", std::string{})));
            QXL_TRY_ASSIGN(const double tNs, field(body, "T_ns", params, ctx.res, 0.0));
            s.append(Delay{ch, Picoseconds{0}, ctx.grid(tNs * 1e-9, false)});
        } else if (op == "shift_phase" || op == "set_phase" || op == "shift_frequency" ||
                   op == "set_frequency") {
            const FrameOp::Op kind = op == "shift_phase"     ? FrameOp::Op::ShiftPhase
                                     : op == "set_phase"     ? FrameOp::Op::SetPhase
                                     : op == "shift_frequency" ? FrameOp::Op::ShiftFrequency
                                                               : FrameOp::Op::SetFrequency;
            QXL_TRY_ASSIGN(auto f, frameOpFromJson(body, kind, ctx));
            s.append(std::move(f));
        } else if (op == "acquire") {
            QXL_TRY_ASSIGN(auto a, acquireFromJson(body, ctx));
            QXL_TRY_ASSIGN(const double delayNs, field(body, "delay_ns", params, ctx.res, 0.0));
            // spec 10 §6.7: the window (length T_ro) opens `acquire_delay` after the readout tone
            // starts — the cavity ring-up plus line delay — not after it ends.
            const Picoseconds base = std::max(lastPlayStart.value_or(Picoseconds{0}), s.channelEnd(a.ch));
            const Picoseconds t0 = base + ctx.grid(delayNs * 1e-9, false);
            s.insert(std::move(a), t0);
        } else if (op == "barrier") {
            std::vector<ChannelId> chs;
            for (auto const& c : body) {
                QXL_TRY_ASSIGN(const ChannelId ch, parseChannel(c.get<std::string>()));
                chs.push_back(ch);
            }
            s.barrier(std::move(chs));
        } else {
            return fail(kErrLibrary,
                        std::format("defcal {}: unknown instruction '{}'", d.key.toString(), op));
        }
    }
    return s;
}

// ---- templates --------------------------------------------------------------------------

Result<Schedule> PulseLibrary::buildFromTemplate(const Defcal& d, const ParamMap& params) const {
    if (d.ref == "cr_echo") return buildCrEcho(d, false);
    if (d.ref == "cr_echo_bare") return buildCrEcho(d, true);
    if (d.ref == "cz_adiabatic" || d.ref == "siswap_resonant") return buildFluxGate(d, d.ref);
    if (d.ref == "ms_bichromatic") return buildMs(d, params);
    if (d.ref == "measure_dispersive") return buildMeasure(d);
    if (d.ref == "reset_active") return buildResetActive(d);
    return fail(kErrLibrary, std::format("defcal {} references unknown template '{}'",
                                         d.key.toString(), d.ref));
}

// `measure_dispersive` always ships its own instruction list (spec 10 §6.7); the template
// only carries the defaults, so the explicit list is authoritative when present.
Result<Schedule> PulseLibrary::buildMeasure(const Defcal& d) const {
    if (d.instructions.is_array() && !d.instructions.empty()) return buildFromInstructions(d, {});
    return fail(kErrLibrary, std::format("defcal {}: measure_dispersive needs instructions",
                                         d.key.toString()));
}

// spec 10 §6.8 — measure, wait out the feed-forward latency, then a conditional π pulse.
// The schedule model has no branch: the corrective `x` is emitted unconditionally and the
// backend applies it only on outcome 1 (spec 08 §6).
Result<Schedule> PulseLibrary::buildResetActive(const Defcal& d) const {
    if (d.key.qubits.size() != 1)
        return fail(kErrLibrary, std::format("defcal {}: reset takes one qubit", d.key.toString()));
    const std::uint32_t q = d.key.qubits[0];
    QXL_TRY_ASSIGN(Schedule s, scheduleFor("measure", std::span<const std::uint32_t>(&q, 1)));

    double feedforwardNs = 300.0; // spec 10 §10 default
    if (templates_.contains("reset_active"))
        feedforwardNs = templates_["reset_active"].value("feedforward_ns", feedforwardNs);
    QXL_TRY_ASSIGN(const Waveform xwf, singleQubitWaveform("x", q));

    const ChannelId drive = isIonDevice() ? ChannelId::raman(q) : ChannelId::drive(q);
    const Picoseconds ready = s.duration() + quantise(feedforwardNs * 1e-9, false);
    s.insert(Delay{drive, Picoseconds{0}, ready - s.channelEnd(drive)}, s.channelEnd(drive));
    s.insert(Play{drive, xwf, Picoseconds{0}}, ready);
    return s;
}

// The envelope of a calibrated single-qubit gate, used by the area theorem (spec 10 §7) and
// as the echo pulse of the cross-resonance sequence (§6.3).
Result<Waveform> PulseLibrary::singleQubitWaveform(std::string_view gate, std::uint32_t q) const {
    const Defcal* d = find(gate, std::span<const std::uint32_t>(&q, 1));
    if (!d || !d->instructions.is_array())
        return fail(kErrNoDefcal, std::format("no '{}' defcal on qubit {}", gate, q));
    const ParamMap params{{"theta", kPi / 2.0}};
    const BuildCtx ctx{params, resolver(), dt_, granularity_, minPulseSamples_};
    for (auto const& item : d->instructions) {
        if (!item.is_object() || !item.contains("play")) continue;
        QXL_TRY_ASSIGN(auto p, playFromJson(item["play"], ctx));
        return p.wf;
    }
    return fail(kErrNoDefcal, std::format("defcal '{}' on qubit {} plays no pulse", gate, q));
}

} // namespace qlab::pulse
