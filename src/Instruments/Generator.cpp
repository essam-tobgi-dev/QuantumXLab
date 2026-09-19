#include "Instruments/Generator.hpp"
#include <cmath>
#include <numbers>

namespace qlab::instr {
namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kInternalTimebaseError = 5e-8; // free-running reference (Model)
} // namespace

double PhaseNoiseMask::dbcPerHz(double offsetHz) const {
    const double l10k = std::pow(10.0, at10kHzDbc / 10.0);
    const double floor = std::pow(10.0, floorDbc / 10.0);
    const double f = std::max(std::abs(offsetHz), 1e-3);
    return 10.0 * std::log10(l10k * (1e4 / f) * (1e4 / f) + floor);
}

std::vector<double> PhaseNoiseMask::synthesize(std::size_t n, double sampleRateHz,
                                               core::Random& rng) const {
    std::vector<double> phi(n, 0.0);
    if (n == 0 || !(sampleRateHz > 0.0))
        return phi;
    const double l10k = std::pow(10.0, at10kHzDbc / 10.0);
    const double floor = std::pow(10.0, floorDbc / 10.0);
    // T07 §7. One-sided S_φ(f) = 2 L(f). A Wiener phase with step variance σ_w² at rate fs has
    // S_φ = 2 σ_w² fs / (2π f)², so σ_w² = L_10k (2π·10⁴)² / fs; white phase noise of density
    // 2 L_floor over the Nyquist band fs/2 has variance L_floor·fs.
    const double sigmaWalk = std::sqrt(l10k * (2.0 * kPi * 1e4) * (2.0 * kPi * 1e4) / sampleRateHz);
    const double sigmaWhite = std::sqrt(floor * sampleRateHz);
    double walk = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        walk += rng.normal(0.0, sigmaWalk);
        phi[k] = walk + rng.normal(0.0, sigmaWhite);
    }
    return phi;
}

SettingSchema Generator::makeSchema() {
    SettingSchema s;
    s.id = "instr/sg_mw.schema.json";
    s.instrument = "sg_mw";
    s.settings = {
        SettingSpec::real("frequency", "Hz", 10e6, 20e9, 1.0, 5.0e9,
                          "Carrier frequency (10 MHz – 20 GHz, 1 Hz steps)"),
        SettingSpec::real("power", "dBm", -20.0, 20.0, 0.01, 10.0, "Output power into 50 Ω"),
        SettingSpec::boolean("rf_on", false, "RF output enabled"),
        // Spec 12 §2 gives −110…−140 dBc/Hz, the descriptor's spec sheet −120…−100: the union is
        // accepted.
        SettingSpec::real("phase_noise_dbc", "dBc/Hz", -140.0, -100.0, 0.1, -125.0,
                          "SSB phase noise at 10 kHz offset"),
        SettingSpec::real("phase_noise_floor_dbc", "dBc/Hz", -175.0, -130.0, 0.1, -150.0,
                          "Far-from-carrier phase-noise floor"),
        SettingSpec::choice("ref", {"internal", "ext_10MHz"}, "ext_10MHz", "Frequency reference"),
        SettingSpec::boolean(
            "apply_phase_noise", false,
            "Apply the phase noise to the Lindblad backend as a frame-phase random walk (Model)"),
        refreshRateSetting(),
    };
    return s;
}

Generator::Generator(std::uint32_t index) : InstrumentBase({"sg_mw", index}, makeSchema()) {
    setChannels({
        {{}, "f", "s", "Hz", FidelityClass::Model, false, false, "Carrier frequency"},
        {{}, "P_dBm", "s", "dBm", FidelityClass::Model, false, false, "Output power"},
        {{}, "on", "s", "", FidelityClass::Model, false, false, "RF output state (0/1)"},
        {{},
         "phase_noise",
         "Hz",
         "dBc/Hz",
         FidelityClass::Model,
         false,
         false,
         "SSB phase-noise mask L(f) versus offset"},
    });
}

PhaseNoiseMask Generator::mask() const {
    const SettingValues v = snapshot();
    return {v.real("phase_noise_dbc"), v.real("phase_noise_floor_dbc")};
}

double Generator::carrierHz() const {
    const SettingValues v = snapshot();
    const double f = v.real("frequency");
    return v.text("ref") == "internal" ? f * (1.0 + kInternalTimebaseError) : f;
}

bool Generator::outputOn() const {
    return snapshot().flag("rf_on");
}
double Generator::powerDbm() const {
    return snapshot().real("power");
}

Result<Signal> Generator::signal(std::string_view, const SignalRequest& request) const {
    const SettingValues v = snapshot();
    Signal s;
    s.node = id().toString() + ".rf";
    const double f = v.real("frequency");
    s.referenceHz = v.text("ref") == "internal" ? f * (1.0 + kInternalTimebaseError) : f;
    s.sampleRateHz = request.sampleRateHz > 0.0 ? request.sampleRateHz : 1e9;
    s.t0S = request.t0S;
    const std::size_t n = Signal::requestedSamples(request, s.sampleRateHz, 4096);
    const double amplitude = std::sqrt(2.0 * wattsFromDbm(v.real("power")) * kZ0); // T07 (3.1)
    s.fullScaleV = amplitude;
    s.noisePsdWPerHz = 1.380649e-23 * 290.0;
    s.cls = FidelityClass::Model;
    s.samples.assign(n, Complex{});
    if (state() == State::Off || !v.flag("rf_on")) {
        s.note = "RF output off";
        return s;
    }
    core::Random rng(request.noiseSeed ^ 0x6A09E667F3BCC908ull);
    const PhaseNoiseMask m{v.real("phase_noise_dbc"), v.real("phase_noise_floor_dbc")};
    const std::vector<double> phi = m.synthesize(n, s.sampleRateHz, rng);
    for (std::size_t k = 0; k < n; ++k)
        s.samples[k] = std::polar(amplitude, phi[k]);
    return s;
}

std::optional<double> Generator::query(std::string_view path) const {
    if (path == "f")
        return carrierHz();
    if (path == "P_dBm")
        return powerDbm();
    if (path == "on")
        return outputOn() && state() != State::Off ? 1.0 : 0.0;
    return InstrumentBase::query(path);
}

Result<Trace> Generator::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    const SettingValues& v = ctx.settings;
    if (channel.name == "f")
        return scalarTrace(channel, ctx, carrierHz());
    if (channel.name == "P_dBm")
        return scalarTrace(channel, ctx, v.real("power"));
    if (channel.name == "on")
        return scalarTrace(channel, ctx, v.flag("rf_on") ? 1.0 : 0.0);
    // phase_noise: the mask on a logarithmic offset axis, 10 Hz … 100 MHz, 10 points per decade.
    Trace t = makeTrace(channel, ctx);
    const PhaseNoiseMask m{v.real("phase_noise_dbc"), v.real("phase_noise_floor_dbc")};
    for (int k = 0; k <= 70; ++k) {
        const double f = 10.0 * std::pow(10.0, k / 10.0);
        t.x.push_back(f);
        t.y.push_back(m.dbcPerHz(f));
    }
    t.markers.push_back({1e4, m.dbcPerHz(1e4), "L(10 kHz)", m.dbcPerHz(1e4), 0.0, "dBc/Hz"});
    return t;
}

} // namespace qlab::instr
