#include "Instruments/Oscilloscope.hpp"
#include "Data/Fidelity.hpp"
#include "Instruments/Awg.hpp" // parsePortIndex
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::instr {
namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}
double normalCdf(double x) {
    return 0.5 * std::erfc(-x / std::numbers::sqrt2);
}
} // namespace

SettingSchema Oscilloscope::makeSchema() {
    SettingSchema s;
    s.id = "instr/oscilloscope.schema.json";
    s.instrument = "oscilloscope";
    s.settings = {
        SettingSpec::real("timebase", "s", 1e-10, 1e-3, 0.0, 10e-9,
                          "Horizontal scale per division (10 divisions)"),
        SettingSpec::real("sample_rate", "Hz", 1e9, 20e9, 0.0, 5e9,
                          "Sample rate (reduced when the record exceeds the memory depth)"),
        SettingSpec::real("bandwidth", "Hz", 1e8, 4e9, 0.0, 1e9,
                          "Analog bandwidth (−3 dB, Gaussian response)"),
        SettingSpec::integer("resolution_bits", "bit", 8, 8, 8, "Vertical resolution").constant(),
        SettingSpec::integer("channels", "", kChannels, kChannels, kChannels, "Input channels")
            .constant(),
        SettingSpec::choice("trigger", {"free_run", "run", "ch1", "ch2", "ch3", "ch4"}, "run",
                            "Trigger source: the run trigger or an edge on a channel"),
        SettingSpec::real("trigger_level", "V", -5.0, 5.0, 0.0, 0.05, "Edge trigger level"),
        SettingSpec::choice("trigger_slope", {"rising", "falling"}, "rising", "Edge trigger slope"),
        SettingSpec::real("delay", "s", -1e-3, 1e-3, 0.0, 0.0,
                          "Record start relative to the trigger"),
    };
    for (std::uint32_t k = 0; k < kChannels; ++k) {
        const std::string p = std::format("ch[{}].", k);
        s.settings.push_back(SettingSpec::text(p + "source", k == 0 ? "awg[0].ch[0]" : "",
                                               "Routing node shown on this channel (empty: off)"));
        s.settings.push_back(SettingSpec::choice(p + "component", {"I", "Q", "magnitude"},
                                                 k % 2 == 0 ? "I" : "Q",
                                                 "Part of the complex signal"));
        s.settings.push_back(SettingSpec::choice(
            p + "view", {"envelope", "output"}, "envelope",
            "Schedule envelope (Exact) or the signal through the front end (Model)"));
        s.settings.push_back(SettingSpec::real(p + "volts_per_div", "V", 1e-3, 5.0, 0.0, 0.125,
                                               "Vertical scale per division (8 divisions)"));
        s.settings.push_back(
            SettingSpec::real(p + "offset", "V", -5.0, 5.0, 0.0, 0.0, "Vertical offset"));
    }
    s.settings.push_back(refreshRateSetting());
    return s;
}

Oscilloscope::Oscilloscope(std::uint32_t index)
    : InstrumentBase({"oscilloscope", index}, makeSchema()) {
    std::vector<ChannelDesc> ch;
    for (std::uint32_t k = 0; k < kChannels; ++k)
        ch.push_back(
            {{},
             std::format("ch[{}]", k),
             "s",
             "V",
             FidelityClass::Exact,
             false,
             false,
             "Time-domain trace (Exact for the envelope view, Model through the front end)"});
    setChannels(std::move(ch));
}

bool Oscilloscope::triggerSourceSet(const SettingValues& v) const {
    return v.text("trigger") != "free_run";
}

double Oscilloscope::componentOf(const Complex& z, const std::string& component) {
    return component == "Q" ? z.imag() : component == "magnitude" ? std::abs(z) : z.real();
}

Result<Oscilloscope::Capture> Oscilloscope::capture(std::uint32_t k, const SettingValues& v,
                                                    std::uint64_t seed) const {
    const SignalGraph* graph = routing();
    if (!graph)
        return fail(err::NotBound, id().toString() + ": no routing matrix is bound");
    const std::string p = std::format("ch[{}].", k);
    const std::string node = v.text(p + "source");
    if (node.empty())
        return fail(err::NoSignal,
                    std::format("{}: channel {} has no source", id().toString(), k + 1));
    Capture c;
    c.component = v.text(p + "component");
    c.envelope = v.text(p + "view") == "envelope";
    SignalRequest request;
    request.envelopeView = c.envelope;
    request.noiseSeed = seed;
    QXL_TRY_ASSIGN(c.signal, graph->signalAt(node, request));
    if (c.signal.samples.empty() || !(c.signal.sampleRateHz > 0.0))
        return fail(err::NoSignal,
                    std::format("{}: node '{}' delivered no samples", id().toString(), node));
    return c;
}

Result<std::pair<double, bool>> Oscilloscope::triggerTime(const SettingValues& v,
                                                          std::uint64_t seed) const {
    const std::string source = v.text("trigger");
    if (source == "run" || source == "free_run")
        return std::pair{0.0, true}; // the run trigger starts the schedule
    const auto k = static_cast<std::uint32_t>(source.back() - '1');
    QXL_TRY_ASSIGN(Capture c, capture(k, v, seed));
    const double level = v.real("trigger_level");
    const bool rising = v.text("trigger_slope") == "rising";
    const double dt = 1.0 / c.signal.sampleRateHz;
    for (std::size_t i = 1; i < c.signal.samples.size(); ++i) {
        const double a = componentOf(c.signal.samples[i - 1], c.component),
                     b = componentOf(c.signal.samples[i], c.component);
        if (rising ? (a < level && b >= level) : (a > level && b <= level))
            return std::pair{c.signal.t0S + static_cast<double>(i) * dt, true};
    }
    return std::pair{0.0, false}; // auto mode: no edge, the record starts with the schedule
}

std::optional<double> Oscilloscope::query(std::string_view path) const {
    const auto k = parsePortIndex(path);
    if (!k || *k >= kChannels)
        return InstrumentBase::query(path);
    auto run = runView();
    auto c = capture(*k, snapshot(), 0);
    if (!c || !run)
        return std::nullopt;
    const auto i = static_cast<std::int64_t>(
        std::floor((run->playheadS - c->signal.t0S) * c->signal.sampleRateHz));
    // Outside the captured record there is no reading — not a reading of 0 V.
    if (i < 0 || i >= static_cast<std::int64_t>(c->signal.samples.size()))
        return std::nullopt;
    return componentOf(c->signal.samples[static_cast<std::size_t>(i)], c->component);
}

Result<Trace> Oscilloscope::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    const SettingValues& v = ctx.settings;
    const std::uint32_t k = parsePortIndex(channel.name).value_or(0);
    const std::uint64_t seed = ctx.rng.next();
    QXL_TRY_ASSIGN(Capture c, capture(k, v, seed));
    QXL_TRY_ASSIGN(auto trigger, triggerTime(v, seed));
    const double record = 10.0 * v.real("timebase");
    const double fs = std::min(v.real("sample_rate"), static_cast<double>(kMemoryDepth) / record);
    const std::int64_t periodPs = std::max<std::int64_t>(1, std::llround(1e12 / fs));
    const auto n =
        static_cast<std::size_t>(std::max<std::int64_t>(2, std::llround(record * 1e12) / periodPs));
    const bool edge = v.text("trigger").starts_with("ch");
    const double start =
        trigger.first + v.real("delay") - (edge && trigger.second ? v.real("timebase") : 0.0);
    const std::int64_t startPs = std::llround(start * 1e12);
    const std::int64_t dtPs = std::max<std::int64_t>(1, std::llround(1e12 / c.signal.sampleRateHz));
    const std::int64_t t0Ps = std::llround(c.signal.t0S * 1e12);
    const auto length = static_cast<std::int64_t>(c.signal.samples.size());
    auto held = [&](std::int64_t i) {
        return i >= 0 && i < length ? c.signal.samples[static_cast<std::size_t>(i)] : Complex{};
    };

    Trace t = makeTrace(channel, ctx);
    t.cls = c.envelope ? data::weakest(FidelityClass::Exact, c.signal.cls) : FidelityClass::Model;
    t.x.resize(n);
    t.y.resize(n);
    const std::string p = std::format("ch[{}].", k);
    const double lsb = 8.0 * v.real(p + "volts_per_div") / 256.0, offset = v.real(p + "offset");
    const double bw = v.real("bandwidth");
    const double sigmaT =
        std::sqrt(std::numbers::ln2) / (kTwoPi * bw); // Gaussian response, −3 dB at `bandwidth`
    const double dt = static_cast<double>(dtPs) * 1e-12;
    for (std::size_t j = 0; j < n; ++j) {
        const std::int64_t tPs = startPs + static_cast<std::int64_t>(j) * periodPs;
        const double time = static_cast<double>(tPs) * 1e-12;
        t.x[j] = time;
        if (c.envelope) { // the schedule itself: zero-order hold, no front end
            t.y[j] = componentOf(held(floorDiv(tPs - t0Ps, dtPs)), c.component);
            continue;
        }
        double y = 0.0;
        if (c.signal.referenceHz > 0.0) { // RF node: carrier response × the real waveform
            const Complex z = held(floorDiv(tPs - t0Ps, dtPs));
            const double h =
                std::exp(-0.5 * std::numbers::ln2 * std::pow(c.signal.referenceHz / bw, 2));
            y = c.component == "magnitude"
                    ? h * std::abs(z)
                    : h * (z * std::polar(1.0, kTwoPi * c.signal.referenceHz * time)).real();
        } else { // baseband: Gaussian-filtered zero-order hold, exact per held sample
            const std::int64_t centre = floorDiv(tPs - t0Ps, dtPs);
            const auto reach = static_cast<std::int64_t>(std::ceil(6.0 * sigmaT / dt)) + 1;
            for (std::int64_t i = centre - reach; i <= centre + reach; ++i) {
                const double tk = static_cast<double>(t0Ps + i * dtPs) * 1e-12;
                y += componentOf(held(i), c.component) *
                     (normalCdf((time - tk) / sigmaT) - normalCdf((time - tk - dt) / sigmaT));
            }
        }
        t.y[j] = std::clamp(std::round((y - offset) / lsb), -128.0, 127.0) * lsb +
                 offset; // 8-bit vertical
    }
    t.markers.push_back(
        {start, 0.0, "sample_rate_effective", 1e12 / static_cast<double>(periodPs), 0.0, "Hz"});
    t.markers.push_back(
        {trigger.first, 0.0, trigger.second ? "trigger" : "no_trigger", trigger.first, 0.0, "s"});
    if (!c.envelope)
        t.markers.push_back({start, 0.0, "lsb", lsb, 0.0, "V"});
    if (!c.signal.connected)
        t.markers.push_back({start, 0.0, "no_signal", 0.0, 0.0, ""});
    return t;
}

} // namespace qlab::instr
