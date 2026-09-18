#include "Instruments/SpectrumAnalyzer.hpp"
#include "Numerics/Fft.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::instr {
namespace {
constexpr double kPi = std::numbers::pi;

struct HannWindow {
    num::RealVector w;
    double coherentGain = 0.5; // Σw / N
    double enbwBins = 1.5;     // N Σw² / (Σw)²
    explicit HannWindow(std::size_t n) : w(num::window(num::Window::Hann, n)) {
        double s1 = 0.0, s2 = 0.0;
        for (double x : w) { s1 += x; s2 += x * x; }
        if (n > 0 && s1 > 0.0) {
            coherentGain = s1 / static_cast<double>(n);
            enbwBins = static_cast<double>(n) * s2 / (s1 * s1);
        }
    }
};
} // namespace

SettingSchema SpectrumAnalyzer::makeSchema() {
    SettingSchema s;
    s.id = "instr/spectrum_analyzer.schema.json";
    s.instrument = "spectrum_analyzer";
    s.settings = {
        SettingSpec::real("center", "Hz", 1e6, 26e9, 1.0, 5.1e9, "Centre frequency (1 MHz – 26 GHz)"),
        SettingSpec::real("span", "Hz", 1e3, 5e9, 1.0, 500e6, "Frequency span"),
        SettingSpec::real("rbw", "Hz", 1.0, 3e6, 0.0, 1e6, "Resolution bandwidth (noise bandwidth of the Hann window)"),
        SettingSpec::real("ref_level", "dBm", -100.0, 30.0, 0.1, 0.0, "Reference level (top of the screen)"),
        SettingSpec::choice("detector", {"peak", "sample", "rms"}, "peak", "Detector applied to each display bucket"),
        SettingSpec::integer("points", "", 101, 10001, 1001, "Display points"),
        SettingSpec::integer("averages", "", 1, 64, 1, "Power averages per trace"),
        SettingSpec::real("danl_dbm_hz", "dBm/Hz", -175.0, -120.0, 0.1, -150.0, "Displayed average noise level of the analyzer"),
        SettingSpec::text("input", "iq_mixer[0].rf", "Routing node the input is attached to"),
        SettingSpec::choice("trigger", {"free_run", "run"}, "free_run", "Sweep trigger"),
        refreshRateSetting(),
    };
    return s;
}

SpectrumAnalyzer::SpectrumAnalyzer(std::uint32_t index) : InstrumentBase({"spectrum_analyzer", index}, makeSchema()) {
    setChannels({
        {{}, "trace", "Hz", "dBm", FidelityClass::Model, false, false, "Power per resolution bandwidth"},
        {{}, "markers", "Hz", "dBm", FidelityClass::Model, false, false, "Peak table, strongest first"},
    });
}

Result<SpectrumAnalyzer::Record> SpectrumAnalyzer::record(const SettingValues& v, std::uint64_t noiseSeed) const {
    const SignalGraph* graph = routing();
    if (!graph) return fail(err::NotBound, id().toString() + ": no routing matrix is bound");
    const std::string node = v.text("input");
    QXL_TRY_ASSIGN(double native, graph->sampleRateAt(node));
    const double fs = native > 0.0 ? native : std::max(2.5 * v.real("span"), 1e6); // CW source: we choose
    const double enbw = HannWindow(4096).enbwBins;
    const double wanted = std::ceil(enbw * fs / v.real("rbw"));
    const auto n = static_cast<std::size_t>(std::clamp(wanted, 64.0, static_cast<double>(kMaxRecord)));
    SignalRequest request;
    request.samples = n;
    request.sampleRateHz = fs;
    request.centered = true;
    request.noiseSeed = noiseSeed;
    Record r;
    QXL_TRY_ASSIGN(r.signal, graph->signalAt(node, request));
    if (r.signal.samples.empty() || !(r.signal.sampleRateHz > 0.0))
        return fail(err::NoSignal, std::format("{}: node '{}' delivered no samples", id().toString(), node));
    r.rbwHz = HannWindow(r.signal.samples.size()).enbwBins * r.signal.sampleRateHz / static_cast<double>(r.signal.samples.size());
    r.floorWatts = (r.signal.noisePsdWPerHz + wattsFromDbm(v.real("danl_dbm_hz"))) * r.rbwHz;
    return r;
}

Result<double> SpectrumAnalyzer::markerPowerDbm(double frequencyHz) const {
    if (state() == State::Off) return fail(err::PoweredOff, id().toString() + " is switched off");
    QXL_TRY_ASSIGN(Record r, record(snapshot(), 0));
    const auto& z = r.signal.samples;
    const HannWindow win(z.size());
    const double step = -2.0 * kPi * (frequencyHz - r.signal.referenceHz) / r.signal.sampleRateHz;
    Complex acc{};
    for (std::size_t k = 0; k < z.size(); ++k) acc += win.w[k] * z[k] * std::polar(1.0, step * static_cast<double>(k));
    const double amplitude = std::abs(acc) / (static_cast<double>(z.size()) * win.coherentGain);
    return dbmFromWatts(tonePowerWatts(amplitude) + r.floorWatts);
}

Result<Trace> SpectrumAnalyzer::sweep(const ChannelDesc& channel, AcquireContext& ctx) const {
    const SettingValues& v = ctx.settings;
    QXL_TRY_ASSIGN(Record r, record(v, ctx.rng.next()));
    const auto& z = r.signal.samples;
    const std::size_t n = z.size();
    const double fs = r.signal.sampleRateHz;
    const HannWindow win(n);
    const std::size_t pad = n <= (std::size_t{1} << 16) ? 8 : 4; // peak-detector scalloping < 0.03 dB / 0.09 dB
    const std::size_t m = std::bit_ceil(n * pad);
    const double norm = 1.0 / (static_cast<double>(n) * win.coherentGain);
    // Complex-envelope noise of one-sided density N0: E|n|² = 2 Z0 N0 fs, so a bin reads N0·RBW.
    const double n0 = r.floorWatts / r.rbwHz;
    const double sigma = std::sqrt(2.0 * kZ0 * n0 * fs / 2.0); // per quadrature
    const auto averages = static_cast<std::size_t>(v.integer("averages"));
    std::vector<double> power(m, 0.0);
    num::Vector buffer(m);
    for (std::size_t a = 0; a < averages; ++a) {
        std::fill(buffer.begin(), buffer.end(), Complex{});
        for (std::size_t k = 0; k < n; ++k)
            buffer[k] = win.w[k] * (z[k] + Complex{ctx.rng.normal(0.0, sigma), ctx.rng.normal(0.0, sigma)});
        num::fftInPlace(buffer);
        for (std::size_t k = 0; k < m; ++k) power[k] += tonePowerWatts(std::abs(buffer[k]) * norm);
    }
    for (double& p : power) p /= static_cast<double>(averages);

    Trace t = makeTrace(channel, ctx);
    const auto points = static_cast<std::size_t>(v.integer("points"));
    const double start = v.real("center") - v.real("span") / 2.0;
    const double bucket = v.real("span") / static_cast<double>(points - 1);
    const double binHz = fs / static_cast<double>(m);
    const std::string detector = v.text("detector");
    t.x.resize(points);
    t.y.resize(points);
    for (std::size_t j = 0; j < points; ++j) {
        const double f = start + static_cast<double>(j) * bucket;
        t.x[j] = f;
        const double lo = f - bucket / 2.0 - r.signal.referenceHz, hi = f + bucket / 2.0 - r.signal.referenceHz;
        if (hi <= -fs / 2.0 || lo >= fs / 2.0) { // outside the band the node's record covers: floor only
            t.y[j] = dbmFromWatts(r.floorWatts);
            continue;
        }
        auto binAt = [&](std::int64_t k) { return power[static_cast<std::size_t>(((k % static_cast<std::int64_t>(m)) + static_cast<std::int64_t>(m)) % static_cast<std::int64_t>(m))]; };
        // Signed bin indices run from −m/2 to m/2 − 1. A display bucket that straddles the band edge
        // is clipped to that range: without it binAt's modulo wrap would fold bins in from the
        // opposite edge of the record's band and show them as power at this frequency.
        const auto half = static_cast<std::int64_t>(m / 2);
        const auto centre = std::clamp(static_cast<std::int64_t>(std::llround((f - r.signal.referenceHz) / binHz)), -half, half - 1);
        std::int64_t first = std::max(-half, static_cast<std::int64_t>(std::ceil(lo / binHz)));
        std::int64_t last = std::min(half - 1, static_cast<std::int64_t>(std::ceil(hi / binHz)) - 1);
        if (last < first || detector == "sample") first = last = centre; // bucket narrower than a bin
        double value = 0.0;
        if (detector == "rms") {
            for (std::int64_t k = first; k <= last; ++k) value += binAt(k);
            value /= static_cast<double>(last - first + 1);
        } else {
            for (std::int64_t k = first; k <= last; ++k) value = std::max(value, binAt(k));
        }
        t.y[j] = dbmFromWatts(value);
    }

    // Markers: effective RBW, the strongest peak, and the landmarks the source names (§4 spurs).
    t.markers.push_back({t.x.front(), 0.0, "rbw_effective", r.rbwHz, 0.0, "Hz"});
    t.markers.push_back({t.x.front(), dbmFromWatts(r.floorWatts), "noise_floor", dbmFromWatts(r.floorWatts), 0.0, "dBm"});
    const auto peakIt = std::max_element(t.y.begin(), t.y.end());
    const auto peakIdx = static_cast<std::size_t>(peakIt - t.y.begin());
    t.markers.push_back({t.x[peakIdx], *peakIt, "peak", *peakIt, 0.0, "dBm"});
    lastPeakDbm_.store(*peakIt);
    lastPeakHz_.store(t.x[peakIdx]);
    if (*peakIt > v.real("ref_level")) t.markers.push_back({t.x[peakIdx], *peakIt, "overload", *peakIt - v.real("ref_level"), 0.0, "dB"});
    const double reach = std::max(r.rbwHz, bucket);
    std::optional<double> carrierDbm;
    for (auto const& [name, f] : r.signal.landmarks) {
        double best = -400.0, at = f;
        for (std::size_t j = 0; j < points; ++j)
            if (std::abs(t.x[j] - f) <= reach && t.y[j] > best) { best = t.y[j]; at = t.x[j]; }
        if (best <= -400.0) continue; // off screen
        t.markers.push_back({at, best, name, best, 0.0, "dBm"});
        if (name == "carrier") carrierDbm = best;
    }
    if (carrierDbm)
        for (const char* name : {"lo", "image"})
            if (const Marker* mk = t.marker(name)) {
                const Marker rel{mk->x, mk->y, std::string(name) + "_dbc", mk->value - *carrierDbm, 0.0, "dBc"};
                t.markers.push_back(rel);
            }
    if (!r.signal.connected) t.markers.push_back({t.x.front(), 0.0, "no_signal", 0.0, 0.0, ""});
    return t;
}

std::optional<double> SpectrumAnalyzer::query(std::string_view path) const {
    if (path == "trace" || path == "peak") { const double p = lastPeakDbm_.load(); return std::isfinite(p) ? std::optional(p) : std::nullopt; }
    if (path == "peak_frequency") { const double f = lastPeakHz_.load(); return std::isfinite(f) ? std::optional(f) : std::nullopt; }
    return InstrumentBase::query(path);
}

std::vector<SpectrumPeak> SpectrumAnalyzer::findPeaks(const Trace& trace, std::size_t maxPeaks, double prominenceDb) {
    std::vector<SpectrumPeak> peaks;
    const auto& y = trace.y;
    for (std::size_t i = 1; i + 1 < y.size(); ++i) {
        if (!(y[i] > y[i - 1] && y[i] >= y[i + 1])) continue;
        double left = y[i], right = y[i]; // lowest valley on each side before a higher sample
        for (std::size_t k = i; k-- > 0 && y[k] <= y[i];) left = std::min(left, y[k]);
        for (std::size_t k = i + 1; k < y.size() && y[k] <= y[i]; ++k) right = std::min(right, y[k]);
        if (y[i] - std::max(left, right) >= prominenceDb) peaks.push_back({trace.x[i], y[i]});
    }
    std::sort(peaks.begin(), peaks.end(), [](const SpectrumPeak& a, const SpectrumPeak& b) { return a.powerDbm > b.powerDbm; });
    if (peaks.size() > maxPeaks) peaks.resize(maxPeaks);
    return peaks;
}

Result<Trace> SpectrumAnalyzer::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    if (channel.name == "trace") return sweep(channel, ctx);
    QXL_TRY_ASSIGN(Trace full, sweep(channels()[0], ctx));
    Trace t = makeTrace(channel, ctx);
    for (auto const& p : findPeaks(full)) {
        t.x.push_back(p.frequencyHz);
        t.y.push_back(p.powerDbm);
    }
    t.markers = full.markers;
    return t;
}

} // namespace qlab::instr
