#include "Instruments/Controller.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace qlab::instr {
namespace {
// Fractional frequency stability of the sequencer's own oscillator when it is not locked to the
// 10 MHz reference (`ref_10mhz` spec sheet: a locked reference is 1e-11 … 1e-9).
constexpr double kFreeRunStability = 1e-8;
} // namespace

SettingSchema Controller::makeSchema() {
    SettingSchema s;
    s.id = "instr/controller.schema.json";
    s.instrument = "controller";
    s.settings = {
        SettingSpec::choice("clock_ref", {"internal", "ext_10MHz"}, "ext_10MHz",
                            "Clock reference (10 MHz)"),
        SettingSpec::real("feedforward_latency_ns", "ns", 50.0, 2000.0, 1.0, 200.0,
                          "Measurement-to-pulse feed-forward latency (spec 10 §10)"),
        SettingSpec::real("shot_loop", "s", 1e-6, 1.0, 0.0, 250e-6,
                          "Repetition delay between shots"),
        SettingSpec::integer("sequencer_memory", "", 64, std::int64_t{1} << 24, 65536,
                             "Instructions the sequencer holds"),
        // Spec sheets of the rack's clock units (spec 12 §14: every row is a term the model
        // enforces).
        SettingSpec::integer("outputs", "", 1, 64, 16,
                             "Clock and trigger outputs, one per sequenced channel"),
        SettingSpec::real("skew_ps", "ps", 0.0, 100.0, 0.0, 20.0,
                          "Channel-to-channel skew the distribution leaves (the compiler's "
                          "per-channel delay table calibrates it out)"),
        SettingSpec::real("jitter_ps", "ps", 0.0, 50.0, 0.0, 10.0, "Shot-trigger jitter, 1σ"),
        SettingSpec::real("stability", "", 1e-12, 1e-8, 0.0, 1e-11,
                          "Fractional frequency stability of the locked 10 MHz reference"),
        refreshRateSetting(),
    };
    return s;
}

Controller::Controller(std::uint32_t index) : InstrumentBase({"controller", index}, makeSchema()) {
    setChannels({
        {{},
         "timeline",
         "s",
         "s",
         FidelityClass::Exact,
         false,
         false,
         "Run timeline: x = instruction start, y = duration; aux `line` (channel row) and `kind`"},
        {{},
         "locked",
         "s",
         "",
         FidelityClass::Model,
         false,
         false,
         "1 when locked to the 10 MHz reference"},
        {{}, "rate", "s", "Hz", FidelityClass::Model, false, false, "Shot repetition rate"},
    });
}

std::vector<TimelineEntry> Controller::timeline(const pulse::Schedule& schedule) {
    std::vector<TimelineEntry> out;
    for (auto const& instr : schedule.instructions()) {
        TimelineEntry e;
        e.channel = pulse::instructionChannel(instr).toString();
        e.t0S = pulse::secondsOf(pulse::instructionStart(instr));
        e.durationS = pulse::secondsOf(pulse::instructionDuration(instr));
        if (auto* p = std::get_if<pulse::Play>(&instr)) {
            e.kind = 0;
            e.label = std::format("play {} {}", e.channel, pulse::waveformKindName(p->wf.kind));
        } else if (auto* f = std::get_if<pulse::FrameOp>(&instr)) {
            e.kind = 1;
            const char* names[] = {"set_frequency", "shift_frequency", "set_phase", "shift_phase"};
            e.label = std::format("{} {}", names[static_cast<int>(f->op)], e.channel);
        } else if (std::holds_alternative<pulse::Acquire>(instr)) {
            e.kind = 2;
            e.label = "acquire " + e.channel;
        } else if (std::holds_alternative<pulse::Delay>(instr)) {
            e.kind = 3;
            e.label = "delay " + e.channel;
        } else {
            e.kind = 4;
            e.label = "barrier";
        }
        out.push_back(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const TimelineEntry& a, const TimelineEntry& b) { return a.t0S < b.t0S; });
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i].index = i;
    return out;
}

std::optional<std::size_t> Controller::executingAt(const std::vector<TimelineEntry>& entries,
                                                   double timeS) {
    std::optional<std::size_t> best;
    for (auto const& e : entries) {
        const bool inside =
            e.durationS > 0.0 ? (timeS >= e.t0S && timeS < e.t0S + e.durationS) : timeS == e.t0S;
        if (inside && e.kind != 3 && e.kind != 4 && (!best || e.t0S >= entries[*best].t0S))
            best = e.index;
    }
    return best;
}

double Controller::triggerRateHz() const {
    auto run = runView();
    const double schedule =
        run && run->schedule ? pulse::secondsOf(run->schedule->duration()) : 0.0;
    return 1.0 / (schedule + snapshot().real("shot_loop"));
}

double Controller::periodSigmaS() const {
    const SettingValues v = snapshot();
    const double stability =
        v.text("clock_ref") == "ext_10MHz" ? v.real("stability") : kFreeRunStability;
    const double jitter = v.real("jitter_ps") * 1e-12, period = 1.0 / triggerRateHz();
    return std::hypot(jitter, stability * period);
}

std::optional<double> Controller::query(std::string_view path) const {
    auto run = runView();
    if (path == "locked")
        return snapshot().text("clock_ref") == "ext_10MHz" && state() != State::Off ? 1.0 : 0.0;
    if (path == "rate")
        return triggerRateHz();
    if (path == "rate_sigma")
        return triggerRateHz() * triggerRateHz() * periodSigmaS(); // σ_f = f² σ_T
    if (path == "skew")
        return snapshot().real("skew_ps") * 1e-12;
    if (path == "shot")
        return run ? static_cast<double>(run->shot) : 0.0;
    if (path == "playhead")
        return run ? run->playheadS : 0.0;
    if (path == "instruction") {
        if (!run || !run->schedule)
            return std::nullopt;
        const auto at = executingAt(timeline(*run->schedule), run->playheadS);
        return at ? std::optional(static_cast<double>(*at)) : std::nullopt;
    }
    return InstrumentBase::query(path);
}

Result<Trace> Controller::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    if (channel.name == "locked")
        return scalarTrace(channel, ctx, ctx.settings.text("clock_ref") == "ext_10MHz" ? 1.0 : 0.0);
    if (channel.name == "rate") { // σ_f = f² σ_T from trigger jitter and the reference's stability
        const double rate = triggerRateHz();
        Trace r = scalarTrace(channel, ctx, rate);
        r.sigma = Uncertainty{{rate * rate * periodSigmaS()}, {}};
        r.markers.push_back({0.0, 0.0, "period_sigma", periodSigmaS(), 0.0, "s"});
        return r;
    }
    if (!ctx.run || !ctx.run->schedule)
        return fail(err::NotBound, id().toString() + ": the run has no pulse schedule");
    const std::vector<TimelineEntry> entries = timeline(*ctx.run->schedule);
    if (entries.size() > static_cast<std::size_t>(ctx.settings.integer("sequencer_memory")))
        return raiseFault(
            err::MemoryOverflow,
            std::format("schedule of {} instructions exceeds the {} of sequencer memory",
                        entries.size(), ctx.settings.integer("sequencer_memory")));
    Trace t = makeTrace(channel, ctx);
    std::map<std::string, double> rows; // one row per channel, in order of first use
    auto& line = t.aux["line"];
    auto& kind = t.aux["kind"];
    for (auto const& e : entries) {
        t.x.push_back(e.t0S);
        t.y.push_back(e.durationS);
        line.push_back(rows.try_emplace(e.channel, static_cast<double>(rows.size())).first->second);
        kind.push_back(e.kind);
    }
    // One clock/trigger output drives each sequenced channel: more rows than outputs cannot be
    // played.
    if (rows.size() > static_cast<std::size_t>(ctx.settings.integer("outputs")))
        return raiseFault(
            err::MemoryOverflow,
            std::format("schedule sequences {} channels, more than the {} clock outputs",
                        rows.size(), ctx.settings.integer("outputs")));
    const double playhead = ctx.run->playheadS;
    t.markers.push_back({playhead, 0.0, "playhead", playhead, 0.0, "s"});
    if (const auto at = executingAt(entries, playhead)) {
        const TimelineEntry& e = entries[*at];
        t.markers.push_back(
            {e.t0S, e.durationS, "current: " + e.label, static_cast<double>(*at), 0.0, ""});
    }
    t.markers.push_back({0.0, 0.0, "feedforward_latency",
                         ctx.settings.real("feedforward_latency_ns") * 1e-9, 0.0, "s"});
    t.markers.push_back({0.0, 0.0, "skew", ctx.settings.real("skew_ps") * 1e-12, 0.0, "s"});
    t.markers.push_back(
        {0.0, 0.0, "outputs", static_cast<double>(ctx.settings.integer("outputs")), 0.0, ""});
    return t;
}

} // namespace qlab::instr
