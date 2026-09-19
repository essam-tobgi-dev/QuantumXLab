#include "Instruments/Awg.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::instr {
namespace {
constexpr double kPi = std::numbers::pi;

// The DAC path of one channel: IF modulation, §13 predistortion, N-bit quantisation.
struct DacModel {
    double vfs = 0.5;
    int bits = 14;
    double ifHz = 0.0;
    IqCorrection c;

    Complex ideal(Complex s, double tS) const {
        const Complex z = vfs * std::conj(s) * std::polar(1.0, 2.0 * kPi * ifHz * tS);
        const double i = z.real(), q = z.imag();
        return {i - c.gainRatio * q * std::sin(c.phaseSkewRad) + c.offsetI,
                c.gainRatio * q * std::cos(c.phaseSkewRad) + c.offsetQ};
    }
    Complex quantise(Complex z, std::int32_t* codeI = nullptr,
                     std::int32_t* codeQ = nullptr) const {
        const std::int32_t ci = quantiseCode(z.real(), vfs, bits),
                           cq = quantiseCode(z.imag(), vfs, bits);
        if (codeI)
            *codeI = ci;
        if (codeQ)
            *codeQ = cq;
        const double lsb = lsbVolts(vfs, bits);
        return {ci * lsb, cq * lsb};
    }
};

DacModel dacOf(const SettingValues& v, std::uint32_t port) {
    DacModel d;
    d.vfs = v.real("full_scale_v");
    d.bits = static_cast<int>(v.integer("resolution_bits"));
    d.ifHz = v.real("if_frequency");
    const std::string p = std::format("ch[{}].", port);
    d.c.offsetI = v.real(p + "offset_i");
    d.c.offsetQ = v.real(p + "offset_q");
    d.c.gainRatio = v.real(p + "gain_ratio");
    d.c.phaseSkewRad = v.real(p + "phase_skew") * kPi / 180.0;
    return d;
}
} // namespace

std::int32_t quantiseCode(double volts, double fullScaleV, int bits) {
    const double lsb = lsbVolts(fullScaleV, bits);
    const double half = static_cast<double>(1u << (bits - 1));
    return static_cast<std::int32_t>(std::clamp(std::round(volts / lsb), -half, half - 1.0));
}

std::optional<std::uint32_t> parsePortIndex(std::string_view text) {
    const auto open = text.find("ch[");
    if (open != 0)
        return std::nullopt;
    const auto close = text.find(']');
    if (close == std::string_view::npos)
        return std::nullopt;
    std::uint32_t k = 0;
    auto [p, ec] = std::from_chars(text.data() + 3, text.data() + close, k);
    if (ec != std::errc{} || p != text.data() + close)
        return std::nullopt;
    return k;
}

SettingSchema Awg::makeSchema() {
    SettingSchema s;
    s.id = "instr/awg.schema.json";
    s.instrument = "awg";
    s.settings = {
        SettingSpec::discrete("sample_rate", "Hz", {1e9, 2e9, 4.5e9}, 4.5e9,
                              "DAC sample rate (4.5 GS/s is the device dt of 222 ps)"),
        SettingSpec::integer("resolution_bits", "bit", 14, 16, 14,
                             "DAC resolution; Δ = 2 V_fs / 2^N"),
        SettingSpec::integer("channels", "", kChannels, kChannels, kChannels,
                             "Output channels per unit")
            .constant(),
        SettingSpec::real("full_scale_v", "V", 0.25, 1.0, 0.001, 0.5,
                          "Peak output voltage at envelope amplitude 1"),
        SettingSpec::integer("memory_samples", "", 1024, std::int64_t{1} << 29,
                             std::int64_t{1} << 21, "Waveform memory per channel"),
        SettingSpec::integer("granularity_samples", "", 16, 16, 16, "Record length granularity")
            .constant(),
        SettingSpec::choice("trigger", {"internal", "ext"}, "ext",
                            "Trigger source (ext: from the controller)"),
        SettingSpec::real("if_frequency", "Hz", -500e6, 500e6, 1.0, 100e6,
                          "Intermediate frequency of the digital up-conversion"),
        SettingSpec::boolean(
            "apply_quantization", false,
            "Apply the DAC quantisation to the drive in the Lindblad backend (Model)"),
    };
    for (std::uint32_t k = 0; k < kChannels; ++k) {
        const std::string p = std::format("ch[{}].", k);
        s.settings.push_back(SettingSpec::real(p + "offset_i", "V", -0.1, 0.1, 0.0, 0.0,
                                               "I DC offset (mixer calibration, §13)"));
        s.settings.push_back(SettingSpec::real(p + "offset_q", "V", -0.1, 0.1, 0.0, 0.0,
                                               "Q DC offset (mixer calibration, §13)"));
        s.settings.push_back(SettingSpec::real(p + "gain_ratio", "", 0.5, 2.0, 0.0, 1.0,
                                               "Q/I amplitude ratio (mixer calibration, §13)"));
        s.settings.push_back(SettingSpec::real(p + "phase_skew", "deg", -20.0, 20.0, 0.0, 0.0,
                                               "I/Q phase skew (mixer calibration, §13)"));
    }
    s.settings.push_back(refreshRateSetting());
    return s;
}

Awg::Awg(std::uint32_t index) : InstrumentBase({"awg", index}, makeSchema()) {
    std::vector<ChannelDesc> ch;
    for (std::uint32_t k = 0; k < kChannels; ++k)
        ch.push_back({{},
                      std::format("ch[{}].waveform", k),
                      "s",
                      "V",
                      FidelityClass::Exact,
                      true,
                      false,
                      "Quantised DAC streams: y = I, y_im = Q"});
    ch.push_back(
        {{}, "running", "s", "", FidelityClass::Model, false, false, "1 while a run is playing"});
    ch.push_back({{},
                  "seq.shot",
                  "s",
                  "",
                  FidelityClass::Exact,
                  false,
                  false,
                  "Current shot of the sequencer"});
    setChannels(std::move(ch));
}

void Awg::settingChanged(std::string_view key) {
    if (key != "sample_rate")
        return;
    std::lock_guard lk(cacheMu_);
    cache_ = {};
}

std::optional<pulse::ChannelId> Awg::channelOfPort(std::uint32_t port,
                                                   const pulse::Schedule& schedule) const {
    const auto& bound = bindings().channels;
    if (!bound.empty())
        return port < bound.size() ? std::optional(bound[port]) : std::nullopt;
    std::uint32_t k = 0;
    for (auto const& ch : schedule.channels())
        if (ch.isDrivelike() && k++ == port)
            return ch;
    return std::nullopt;
}

Result<std::shared_ptr<const pulse::SampledSchedule>>
Awg::sampled(const std::shared_ptr<const pulse::Schedule>& s, double rateHz) const {
    std::lock_guard lk(cacheMu_);
    if (cache_.schedule == s && cache_.rateHz == rateHz && cache_.record)
        return cache_.record;
    QXL_TRY_ASSIGN(pulse::SampledSchedule rec, pulse::sampleSchedule(*s, rateHz, 16));
    cache_ = {s, rateHz, std::make_shared<const pulse::SampledSchedule>(std::move(rec))};
    return cache_.record;
}

Result<AwgStreams> Awg::renderWith(std::uint32_t port, const SettingValues& v,
                                   const RunView& run) const {
    if (!run.schedule)
        return fail(err::NotBound, id().toString() + ": the run has no pulse schedule");
    if (port >= kChannels)
        return fail(err::UnknownChannel,
                    std::format("{}: no output ch[{}]", id().toString(), port));
    QXL_TRY_ASSIGN(auto rec, sampled(run.schedule, v.real("sample_rate")));
    const auto memory = static_cast<std::size_t>(v.integer("memory_samples"));
    if (rec->length > memory) // spec 12 §3: a Fault with the sample count
        return fail(
            err::MemoryOverflow,
            std::format(
                "record of {} samples exceeds the {} samples of waveform memory per channel",
                rec->length, memory));
    AwgStreams out;
    out.dtS = pulse::secondsOf(rec->dt);
    out.sampleRateHz =
        1.0 / out.dtS; // the rate actually played: dt is quantised to whole picoseconds
    const DacModel dac = dacOf(v, port);
    out.fullScaleV = dac.vfs;
    out.bits = dac.bits;
    out.memorySamples = rec->length;
    const auto ch = channelOfPort(port, *run.schedule);
    const pulse::ChannelSamples* cs = ch ? rec->find(*ch) : nullptr;
    const std::size_t n = rec->length;
    out.envelope.resize(n);
    out.ideal.resize(n);
    out.output.resize(n);
    out.codeI.resize(n);
    out.codeQ.resize(n);
    for (std::size_t k = 0; k < n; ++k) {
        const Complex s = cs ? cs->samples[k] : Complex{};
        out.envelope[k] = dac.vfs * s;
        out.ideal[k] = dac.ideal(s, static_cast<double>(k) * out.dtS);
        out.output[k] = dac.quantise(out.ideal[k], &out.codeI[k], &out.codeQ[k]);
    }
    return out;
}

Result<AwgStreams> Awg::render(std::uint32_t port) const {
    auto run = runView();
    if (!run)
        return fail(err::NotBound, id().toString() + ": no run is bound");
    auto st = renderWith(port, snapshot(), *run);
    // Spec 12 §3: an over-long record is a Fault with the sample count, whoever discovers it — the
    // AWG's own panel, or a scope/analyzer reading its output node through the routing matrix.
    if (!st && st.error().code == err::MemoryOverflow)
        return raiseFault(err::MemoryOverflow, st.error().message);
    return st;
}

Result<std::vector<Complex>> Awg::quantisedEnvelope(std::uint32_t port) const {
    QXL_TRY_ASSIGN(AwgStreams st, render(port));
    DacModel dac;
    dac.vfs = st.fullScaleV;
    dac.bits = st.bits;
    std::vector<Complex> out(st.envelope.size());
    for (std::size_t k = 0; k < out.size(); ++k)
        out[k] = dac.quantise(st.envelope[k]) / st.fullScaleV;
    return out;
}

IqCorrection Awg::correction(std::uint32_t port) const {
    return dacOf(snapshot(), port).c;
}

Result<void> Awg::setCorrection(std::uint32_t port, const IqCorrection& c) {
    if (port >= kChannels)
        return fail(err::UnknownChannel,
                    std::format("{}: no output ch[{}]", id().toString(), port));
    const std::string p = std::format("ch[{}].", port);
    QXL_TRY(set(p + "offset_i", c.offsetI));
    QXL_TRY(set(p + "offset_q", c.offsetQ));
    QXL_TRY(set(p + "gain_ratio", c.gainRatio));
    return set(p + "phase_skew", c.phaseSkewRad * 180.0 / kPi);
}

double Awg::intermediateFrequencyHz() const {
    return snapshot().real("if_frequency");
}
double Awg::fullScaleVolts() const {
    return snapshot().real("full_scale_v");
}
Complex Awg::idleOutput(std::uint32_t port) const {
    const DacModel dac = dacOf(snapshot(), port);
    return dac.quantise(dac.ideal(Complex{}, 0.0));
}

Result<Signal> Awg::signal(std::string_view port, const SignalRequest& request) const {
    const auto index = parsePortIndex(port);
    if (!index)
        return fail(err::UnknownChannel, std::format("{}: no port '{}'", id().toString(), port));
    QXL_TRY_ASSIGN(AwgStreams st, render(*index));
    Signal s;
    s.node = std::format("{}.ch[{}]", id().toString(), *index);
    s.sampleRateHz = st.sampleRateHz;
    s.fullScaleV = st.fullScaleV;
    s.noisePsdWPerHz = 1.380649e-23 * 290.0;
    s.cls = FidelityClass::Exact;
    const auto length = static_cast<std::int64_t>(st.output.size());
    const auto n = static_cast<std::int64_t>(
        Signal::requestedSamples(request, st.sampleRateHz, st.output.size()));
    const std::int64_t first =
        request.centered ? length / 2 - n / 2 : std::llround(request.t0S / st.dtS);
    s.t0S = static_cast<double>(first) * st.dtS;
    s.samples.assign(static_cast<std::size_t>(n), Complex{});
    if (state() == State::Off) {
        s.note = "AWG switched off";
        return s;
    }
    const DacModel dac = dacOf(snapshot(), *index);
    for (std::int64_t j = 0; j < n; ++j) {
        const std::int64_t i = first + j;
        const bool inside = i >= 0 && i < length;
        if (request.envelopeView)
            s.samples[static_cast<std::size_t>(j)] =
                inside ? st.envelope[static_cast<std::size_t>(i)] : Complex{};
        else // outside the record the DAC idles at its offset code
            s.samples[static_cast<std::size_t>(j)] =
                inside ? st.output[static_cast<std::size_t>(i)]
                       : dac.quantise(dac.ideal(Complex{}, static_cast<double>(i) * st.dtS));
    }
    return s;
}

double Awg::sampleRateHz(std::string_view) const {
    // The rate actually played: the setting quantised to whole picoseconds (pulse::sampleSchedule).
    return 1e12 / static_cast<double>(std::llround(1e12 / snapshot().real("sample_rate")));
}

std::optional<double> Awg::query(std::string_view path) const {
    auto run = runView();
    if (path == "running")
        return run && run->running && state() != State::Off ? 1.0 : 0.0;
    if (path == "seq.shot" || path == "shot")
        return run ? static_cast<double>(run->shot) : 0.0;
    if (const auto port = parsePortIndex(path); port && path.ends_with(".waveform")) {
        if (!run || !run->schedule || *port >= kChannels)
            return std::nullopt;
        const SettingValues v = snapshot();
        auto rec = sampled(run->schedule, v.real("sample_rate"));
        if (!rec)
            return std::nullopt;
        const auto ch = channelOfPort(*port, *run->schedule);
        const pulse::ChannelSamples* cs = ch ? (*rec)->find(*ch) : nullptr;
        const double dt = pulse::secondsOf((*rec)->dt);
        const auto k = static_cast<std::int64_t>(std::floor(run->playheadS / dt));
        const Complex s = cs && k >= 0 && k < static_cast<std::int64_t>(cs->samples.size())
                              ? cs->samples[static_cast<std::size_t>(k)]
                              : Complex{};
        const DacModel dac = dacOf(v, *port);
        return dac.quantise(dac.ideal(s, static_cast<double>(k) * dt))
            .real(); // I output at the playhead
    }
    return InstrumentBase::query(path);
}

Result<Trace> Awg::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    if (channel.name == "running")
        return scalarTrace(channel, ctx, ctx.run && ctx.run->running ? 1.0 : 0.0);
    if (channel.name == "seq.shot")
        return scalarTrace(channel, ctx, ctx.run ? static_cast<double>(ctx.run->shot) : 0.0);
    const std::uint32_t port = parsePortIndex(channel.name).value_or(0);
    if (!ctx.run)
        return fail(err::NotBound, id().toString() + ": no run is bound");
    auto st = renderWith(port, ctx.settings, *ctx.run);
    if (!st) {
        if (st.error().code == err::MemoryOverflow)
            return raiseFault(err::MemoryOverflow, st.error().message);
        return std::unexpected(st.error());
    }
    Trace t = makeTrace(channel, ctx);
    const std::size_t n = st->output.size();
    t.x.resize(n);
    t.y.resize(n);
    t.y_im.resize(n);
    for (std::size_t k = 0; k < n; ++k) {
        t.x[k] = static_cast<double>(k) * st->dtS;
        t.y[k] = st->output[k].real();
        t.y_im[k] = st->output[k].imag();
    }
    const double lsb = lsbVolts(st->fullScaleV, st->bits);
    t.markers.push_back({0.0, 0.0, "LSB", lsb, 0.0, "V"});
    t.markers.push_back({0.0, 0.0, "memory_used", static_cast<double>(st->memorySamples), 0.0, ""});
    return t;
}

} // namespace qlab::instr
