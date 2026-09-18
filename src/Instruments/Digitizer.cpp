#include "Instruments/DigitizerInternal.hpp"
#include "Instruments/Awg.hpp" // parsePortIndex
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::instr {

SettingSchema Digitizer::makeSchema() {
    SettingSchema s;
    s.id = "instr/digitizer.schema.json";
    s.instrument = "digitizer";
    s.settings = {
        SettingSpec::discrete("sample_rate", "Hz", {0.5e9, 1e9, 2e9}, 1e9, "ADC sample rate"),
        SettingSpec::integer("resolution_bits", "bit", 12, 14, 12, "ADC resolution"),
        SettingSpec::integer("channels", "", 2, 2, 2, "ADC inputs (I, Q after down-conversion) per readout line").constant(),
        SettingSpec::real("demod_if", "Hz", 10e6, 500e6, 1.0, 100e6, "Intermediate frequency of the readout tone"),
        SettingSpec::choice("integration", {"boxcar", "matched", "custom"}, "matched", "Integration weights (T07 (6.2))"),
        SettingSpec::real("window_ns", "ns", 0.0, 1e6, 1.0, 0.0, "Acquisition length; 0 = the schedule's acquire / the calibrated readout duration"),
        SettingSpec::integer("averages", "", 1, 1000000, 1, "Shots averaged into one IQ point"),
        SettingSpec::integer("states", "", 2, 3, 2, "States discriminated (3 adds |2> for leakage measurement)"),
        SettingSpec::real("input_range_v", "V", 0.05, 2.0, 0.001, 0.5, "ADC input range (± volts)"),
        SettingSpec::integer("memory_samples", "", 1024, std::int64_t{1} << 24, std::int64_t{1} << 20, "Record memory per input"),
        SettingSpec::real("acquire_delay_ns", "ns", 0.0, 1000.0, 1.0, 100.0, "Acquisition start after the tone start (spec 10 §6.7)"),
        SettingSpec::real("reference_amplitude", "", 0.001, 1.0, 0.0, 0.1, "Calibrated measure-tone amplitude A_ro (pulses.json)"),
        SettingSpec::choice("source", {"run", "prepare_0", "prepare_1", "alternate"}, "run", "What a live acquisition measures"),
        SettingSpec::integer("shots_per_acquire", "", 1, 100000, 1, "Shots taken per acquisition when not following a run"),
        SettingSpec::integer("cloud_points", "", 100, 200000, 2000, "IQ points kept per demodulator"),
        SettingSpec::integer("histogram_channel", "", 0, kDemodChannels - 1, 0, "Demodulator shown by `histogram`"),
        SettingSpec::integer("histogram_bins", "", 8, 512, 64, "Histogram bins"),
        SettingSpec::integer("scope_channel", "", 0, kDemodChannels - 1, 0, "Demodulator shown by `trace`, `demod` and `weights`"),
        SettingSpec::integer("scope_state", "", 0, 2, 0, "Qubit state of the displayed record when no run provides one"),
        SettingSpec::choice("trigger", {"run", "free_run"}, "run", "Acquisition trigger"),
        refreshRateSetting(),
    };
    return s;
}

Digitizer::Digitizer(std::uint32_t index) : InstrumentBase({"digitizer", index}, makeSchema()) {
    std::vector<ChannelDesc> ch;
    for (std::uint32_t k = 0; k < kDemodChannels; ++k)
        ch.push_back({{}, std::format("ch[{}].iq", k), "V", "V", FidelityClass::Statistical, false, false,
                      "IQ cloud: x = I, y = Q; aux columns `prepared`, `assigned`"});
    ch.push_back({{}, "trigger", "s", "", FidelityClass::Model, false, false, "1 when armed on the run trigger"});
    ch.push_back({{}, "histogram", "", "", FidelityClass::Statistical, false, false, "Counts along the discriminating axis (units of σ)"});
    ch.push_back({{}, "trace", "s", "V", FidelityClass::Model, false, false, "One quantised ADC record"});
    ch.push_back({{}, "demod", "s", "V", FidelityClass::Model, true, false, "Demodulated voltage before integration"});
    ch.push_back({{}, "weights", "s", "", FidelityClass::Model, true, false, "Integration weights w(t)"});
    setChannels(std::move(ch));
}

std::string Digitizer::conflict(const SettingValues& v) const {
    const double samples = v.real("window_ns") * 1e-9 * v.real("sample_rate");
    if (samples > v.real("memory_samples")) // spec 12 §1: window longer than memory
        return std::format("acquisition window of {:.0f} samples exceeds the {} samples of record memory", samples, v.integer("memory_samples"));
    return {};
}

std::uint32_t Digitizer::qubitOfChannel(std::uint32_t k) const {
    const auto& bound = bindings().channels;
    return k < bound.size() ? bound[k].a : k;
}

void Digitizer::setReadoutOverride(std::uint32_t k, std::optional<ReadoutParams> params) {
    if (k >= kDemodChannels) return;
    std::lock_guard lk(demodMu_);
    demod_[k].override = std::move(params);
}

void Digitizer::setCustomWeights(std::uint32_t k, std::vector<Complex> weights) {
    if (k >= kDemodChannels) return;
    std::lock_guard lk(demodMu_);
    demod_[k].customWeights = std::move(weights);
}

Result<ReadoutParams> Digitizer::readoutParams(std::uint32_t k) const {
    if (k >= kDemodChannels) return fail(err::UnknownChannel, std::format("{}: no demodulator ch[{}]", id().toString(), k));
    const SettingValues v = snapshot();
    std::optional<ReadoutParams> fixed;
    {
        std::lock_guard lk(demodMu_);
        fixed = demod_[k].override;
    }
    ReadoutParams p;
    if (fixed) p = *fixed;
    else {
        auto env = environment();
        if (!env) return fail(err::NotBound, id().toString() + ": no environment is bound");
        const std::uint32_t q = qubitOfChannel(k);
        ReadoutDefaults d;
        d.acquireDelayS = v.real("acquire_delay_ns") * 1e-9;
        QXL_TRY_ASSIGN(p, readoutFromEnvironment(*env, q, d));
        if (auto run = runView(); run && run->schedule) { // the tone and window actually scheduled (spec 10 §6.7)
            const pulse::Play* tone = nullptr;
            const pulse::Acquire* window = nullptr;
            for (auto const& instr : run->schedule->instructions()) {
                if (auto* play = std::get_if<pulse::Play>(&instr); play && play->ch == pulse::ChannelId::measure(q)) tone = play;
                if (auto* acq = std::get_if<pulse::Acquire>(&instr); acq && acq->ch == pulse::ChannelId::acquire(q)) window = acq;
            }
            if (tone) {
                p.toneS = tone->wf.duration;
                p.toneAmplitude = std::abs(tone->wf.amplitude) / v.real("reference_amplitude");
                if (tone->wf.kind == pulse::WaveformKind::GaussianSquare) { p.toneSigmaS = tone->wf.sigma; p.toneRiseS = tone->wf.rise; }
            }
            if (window) p.windowS = pulse::secondsOf(window->length);
            if (tone && window) p.acquireDelayS = std::max(0.0, pulse::secondsOf(window->t0 - tone->t0));
        }
    }
    if (v.real("window_ns") > 0.0) p.windowS = v.real("window_ns") * 1e-9;
    return p;
}

std::optional<Discriminator> Digitizer::discriminator(std::uint32_t k) const {
    std::lock_guard lk(demodMu_);
    return k < kDemodChannels ? demod_[k].discriminator : std::nullopt;
}
std::optional<TrainingReport> Digitizer::lastTraining(std::uint32_t k) const {
    std::lock_guard lk(demodMu_);
    return k < kDemodChannels ? demod_[k].training : std::nullopt;
}
int Digitizer::classify(std::uint32_t k, double i, double q) const {
    std::lock_guard lk(demodMu_);
    return k < kDemodChannels && demod_[k].discriminator ? demod_[k].discriminator->classify(i, q) : -1;
}
std::vector<IqPoint> Digitizer::cloud(std::uint32_t k) const {
    std::lock_guard lk(demodMu_);
    if (k >= kDemodChannels) return {};
    return {demod_[k].cloud.begin(), demod_[k].cloud.end()};
}
void Digitizer::clearCloud(std::uint32_t k) {
    std::lock_guard lk(demodMu_);
    if (k < kDemodChannels) demod_[k].cloud.clear();
}

Result<Trace> Digitizer::cloudTrace(const ChannelDesc& channel, AcquireContext& ctx, std::uint32_t k) {
    const SettingValues& v = ctx.settings;
    const std::string source = v.text("source");
    std::vector<std::uint8_t> states;
    if (source == "run") { // one point per new shot of the live run (spec 12 §5)
        const std::uint32_t q = qubitOfChannel(k);
        if (ctx.run && q < ctx.run->measuredBits.size() && ctx.run->measuredBits[q] >= 0) {
            std::lock_guard lk(demodMu_);
            if (demod_[k].lastShot != ctx.run->shot) {
                demod_[k].lastShot = ctx.run->shot;
                states.push_back(static_cast<std::uint8_t>(ctx.run->measuredBits[q]));
            }
        }
    } else {
        const auto shots = static_cast<std::size_t>(v.integer("shots_per_acquire"));
        std::lock_guard lk(demodMu_);
        for (std::size_t j = 0; j < shots; ++j)
            states.push_back(source == "prepare_1" ? 1 : source == "prepare_0" ? 0 : static_cast<std::uint8_t>(demod_[k].alternate++ % 2));
    }
    if (!states.empty()) {
        auto shots = measure(k, states, ctx.rng);
        if (!shots) {
            if (shots.error().code == err::MemoryOverflow) return raiseFault(err::MemoryOverflow, shots.error().message);
            return std::unexpected(shots.error());
        }
    }
    Trace t = makeTrace(channel, ctx);
    auto& prepared = t.aux["prepared"];
    auto& assigned = t.aux["assigned"];
    for (auto const& pt : cloud(k)) {
        t.x.push_back(pt.i);
        t.y.push_back(pt.q);
        prepared.push_back(pt.prepared);
        assigned.push_back(pt.assigned);
    }
    if (auto s = prepare(k, v)) { // model lines (class Model) next to the statistical cloud
        t.sigma = Uncertainty{std::vector<double>(t.x.size(), s->sigmaIq / std::sqrt(static_cast<double>(s->averages))), {}};
        t.markers.push_back({0.0, 0.0, "snr_theory", s->snrTheory, 0.0, ""});
        t.markers.push_back({0.0, 0.0, "snr_expected", s->snrExpected, 0.0, ""});
        for (std::size_t st = 0; st < s->meanIq.size(); ++st)
            t.markers.push_back({s->meanIq[st].real(), s->meanIq[st].imag(), std::format("model_mu{}", st), static_cast<double>(st), 0.0, "V"});
    }
    if (auto rep = lastTraining(k)) {
        const Discriminator& d = rep->discriminator;
        for (std::size_t st = 0; st < d.means.size(); ++st)
            t.markers.push_back({d.means[st][0], d.means[st][1], std::format("mu{}", st), static_cast<double>(st), 0.0, "V"});
        t.markers.push_back({0.0, 0.0, "snr", rep->snrMeasured, 0.0, ""});
        t.markers.push_back({0.0, 0.0, "fidelity", rep->assignment.fidelity(), 0.5 * std::hypot(rep->assignment.sigma[0][1], rep->assignment.sigma[1][0]), ""});
    }
    return t;
}

Result<Trace> Digitizer::histogramTrace(const ChannelDesc& channel, AcquireContext& ctx) {
    const auto k = static_cast<std::uint32_t>(ctx.settings.integer("histogram_channel"));
    Discriminator axis;
    if (auto d = discriminator(k)) axis = *d;
    else { // untrained: the model's own axis
        QXL_TRY_ASSIGN(Synth s, prepare(k, ctx.settings));
        axis.means = {{s.meanIq[0].real(), s.meanIq[0].imag()}, {s.meanIq[1].real(), s.meanIq[1].imag()}};
        axis.sxx = axis.syy = s.sigmaIq * s.sigmaIq / static_cast<double>(s.averages);
        axis.sxy = 0.0;
    }
    const double separation = axis.project(axis.means[1][0], axis.means[1][1]) * 2.0;
    const double reach = separation / 2.0 + 4.0;
    const auto bins = static_cast<std::size_t>(ctx.settings.integer("histogram_bins"));
    Trace t = makeTrace(channel, ctx);
    t.x.resize(bins);
    t.y.assign(bins, 0.0);
    auto& p0 = t.aux["prepared_0"];
    auto& p1 = t.aux["prepared_1"];
    p0.assign(bins, 0.0);
    p1.assign(bins, 0.0);
    const double width = 2.0 * reach / static_cast<double>(bins);
    for (std::size_t b = 0; b < bins; ++b) t.x[b] = -reach + (static_cast<double>(b) + 0.5) * width;
    for (auto const& pt : cloud(k)) {
        const double u = axis.project(pt.i, pt.q);
        const auto b = static_cast<std::int64_t>(std::floor((u + reach) / width));
        if (b < 0 || b >= static_cast<std::int64_t>(bins)) continue;
        t.y[static_cast<std::size_t>(b)] += 1.0;
        if (pt.prepared == 0) p0[static_cast<std::size_t>(b)] += 1.0;
        if (pt.prepared == 1) p1[static_cast<std::size_t>(b)] += 1.0;
    }
    t.sigma = Uncertainty{{}, {}};
    for (double c : t.y) t.sigma->y.push_back(std::sqrt(std::max(c, 1.0))); // Poisson
    t.markers.push_back({0.0, 0.0, "threshold", 0.0, 0.0, ""});
    t.markers.push_back({separation / 2.0, 0.0, "separation", separation, 0.0, ""});
    return t;
}

Result<Trace> Digitizer::recordTrace(const ChannelDesc& channel, AcquireContext& ctx) {
    SignalRequest request;
    request.noiseSeed = ctx.rng.next();
    if (channel.name == "weights") {
        QXL_TRY_ASSIGN(Synth s, prepare(static_cast<std::uint32_t>(ctx.settings.integer("scope_channel")), ctx.settings));
        Trace t = makeTrace(channel, ctx);
        for (std::size_t j = 0; j < s.weights.size(); ++j) {
            t.x.push_back(static_cast<double>(j) / s.sampleRateHz);
            t.y.push_back(s.weights[j].real());
            t.y_im.push_back(s.weights[j].imag());
        }
        return t;
    }
    QXL_TRY_ASSIGN(Signal sig, signal(channel.name == "trace" ? "in" : "demod", request));
    Trace t = makeTrace(channel, ctx);
    for (std::size_t j = 0; j < sig.samples.size(); ++j) {
        t.x.push_back(sig.t0S + static_cast<double>(j) / sig.sampleRateHz);
        t.y.push_back(sig.samples[j].real());
        if (channel.complexValued) t.y_im.push_back(sig.samples[j].imag());
    }
    return t;
}

Result<Signal> Digitizer::signal(std::string_view port, const SignalRequest& request) const {
    const SettingValues v = snapshot();
    const auto k = static_cast<std::uint32_t>(v.integer("scope_channel"));
    QXL_TRY_ASSIGN(Synth s, prepare(k, v));
    int st = static_cast<int>(std::min<std::int64_t>(v.integer("scope_state"), s.states - 1));
    if (auto run = runView(); run && qubitOfChannel(k) < run->measuredBits.size() && run->measuredBits[qubitOfChannel(k)] >= 0)
        st = std::min<int>(run->measuredBits[qubitOfChannel(k)], s.states - 1);
    core::Random rng(request.noiseSeed ^ 0xBB67AE8584CAA73Bull);
    const std::vector<double> rec = s.record(s.response.alpha[static_cast<std::size_t>(st)], rng);
    Signal out;
    out.node = id().toString() + "." + std::string(port);
    out.sampleRateHz = s.sampleRateHz;
    out.t0S = s.params.acquireDelayS;
    out.fullScaleV = s.rangeV;
    out.noisePsdWPerHz = s.noiseRmsV * s.noiseRmsV / kZ0 / (s.sampleRateHz / 2.0);
    out.cls = FidelityClass::Model;
    out.samples.resize(rec.size());
    if (port == "in") {
        for (std::size_t j = 0; j < rec.size(); ++j) out.samples[j] = rec[j];
        return out;
    }
    // demod: 2 v e^{−i2π f_IF t} averaged over one IF period (removes the 2 f_IF component)
    const auto period = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(s.sampleRateHz / s.ifHz)));
    std::vector<Complex> mixed(rec.size());
    for (std::size_t j = 0; j < rec.size(); ++j)
        mixed[j] = 2.0 * rec[j] * std::polar(1.0, -2.0 * std::numbers::pi * s.ifHz * static_cast<double>(j) / s.sampleRateHz);
    Complex acc{};
    for (std::size_t j = 0; j < rec.size(); ++j) {
        acc += mixed[j];
        if (j >= period) acc -= mixed[j - period];
        out.samples[j] = acc / static_cast<double>(std::min(j + 1, period));
    }
    return out;
}

double Digitizer::sampleRateHz(std::string_view) const { return snapshot().real("sample_rate"); }

std::optional<double> Digitizer::query(std::string_view path) const {
    if (path == "trigger") return state() == State::Armed || (runView() && runView()->running) ? 1.0 : 0.0;
    if (const auto k = parsePortIndex(path); k && *k < kDemodChannels) {
        std::lock_guard lk(demodMu_);
        const auto& d = demod_[*k];
        if (path.ends_with(".snr")) return d.training ? std::optional(d.training->snrMeasured) : std::nullopt;
        if (path.ends_with(".fidelity")) return d.training ? std::optional(d.training->assignment.fidelity()) : std::nullopt;
        if (d.cloud.empty()) return std::nullopt;
        if (path.ends_with(".iq.i")) return d.cloud.back().i;
        if (path.ends_with(".iq.q")) return d.cloud.back().q;
        if (path.ends_with(".iq")) return std::hypot(d.cloud.back().i, d.cloud.back().q);
    }
    return InstrumentBase::query(path);
}

Result<Trace> Digitizer::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    if (channel.name == "trigger") return scalarTrace(channel, ctx, ctx.run && ctx.run->running ? 1.0 : 0.0);
    if (channel.name == "histogram") return histogramTrace(channel, ctx);
    if (const auto k = parsePortIndex(channel.name)) return cloudTrace(channel, ctx, *k);
    return recordTrace(channel, ctx);
}

} // namespace qlab::instr
