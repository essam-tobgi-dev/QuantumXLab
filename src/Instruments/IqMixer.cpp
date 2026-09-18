#include "Instruments/IqMixer.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace qlab::instr {
namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kNominalLoDbm = 7.0; // below this LO drive the conversion loss grows dB for dB (Model)

Complex coefficient(double dbc, double angleDeg) { return std::polar(std::pow(10.0, dbc / 20.0), angleDeg * kPi / 180.0); }
} // namespace

SettingSchema IqMixer::makeSchema() {
    SettingSchema s;
    s.id = "instr/iq_mixer.schema.json";
    s.instrument = "iq_mixer";
    s.settings = {
        SettingSpec::real("lo_leakage_dbc", "dBc", -80.0, -20.0, 0.1, -40.0, "Uncalibrated LO leakage relative to a full-scale carrier"),
        SettingSpec::real("image_rejection_dbc", "dBc", -80.0, -20.0, 0.1, -35.0, "Uncalibrated image sideband relative to the wanted sideband"),
        SettingSpec::real("leakage_phase", "deg", -180.0, 180.0, 0.0, 30.0, "Phase of the leakage vector (how it splits between the I and Q offsets)"),
        SettingSpec::real("imbalance_angle", "deg", -180.0, 180.0, 0.0, 45.0, "0°: pure amplitude imbalance, 90°: pure phase skew"),
        SettingSpec::real("conversion_loss_db", "dB", 0.0, 15.0, 0.1, 6.0, "IF → RF conversion loss"),
        SettingSpec::real("rf_min", "Hz", 2e9, 2e9, 0.0, 2e9, "Lower edge of the RF band").constant(),
        SettingSpec::real("rf_max", "Hz", 12e9, 12e9, 0.0, 12e9, "Upper edge of the RF band").constant(),
        refreshRateSetting(),
    };
    return s;
}

IqMixer::IqMixer(std::uint32_t index) : InstrumentBase({"iq_mixer", index}, makeSchema()) {
    setChannels({
        {{}, "lo_leak", "s", "dBc", FidelityClass::Model, false, false, "Residual LO leakage with the present AWG offsets"},
        {{}, "image_rej", "s", "dBc", FidelityClass::Model, false, false, "Residual image sideband with the present AWG skew"},
        {{}, "rf", "s", "V", FidelityClass::Model, true, false, "Complex envelope of the RF output around f_LO"},
    });
}

void IqMixer::attach(const Awg* awg, std::uint32_t awgPort, const Generator* lo) {
    awg_ = awg;
    awgPort_ = awgPort;
    lo_ = lo;
}

Complex IqMixer::leakageCoefficient() const {
    const SettingValues v = snapshot();
    return coefficient(v.real("lo_leakage_dbc"), v.real("leakage_phase"));
}
Complex IqMixer::imageCoefficient() const {
    const SettingValues v = snapshot();
    return coefficient(v.real("image_rejection_dbc"), v.real("imbalance_angle"));
}

IqImbalance IqMixer::imbalance() const {
    const Complex e = imageCoefficient();
    const Complex gamma = (1.0 - e) / (1.0 + e);
    return {20.0 * std::log10(std::abs(gamma)), std::arg(gamma) * 180.0 / kPi};
}

double IqMixer::loLeakageDbc() const {
    Complex leak = leakageCoefficient();
    if (awg_) { // DC offsets as the DAC actually holds them (quantised), relative to full scale
        const Complex d = awg_->idleOutput(awgPort_) / awg_->fullScaleVolts();
        leak += d + imageCoefficient() * std::conj(d);
    }
    return 20.0 * std::log10(std::max(std::abs(leak), 1e-15));
}

double IqMixer::imageDbc() const {
    const Complex e = imageCoefficient();
    Complex alpha{1.0, 0.0}, beta{};
    if (awg_) { // s' = α u + β conj(u) with γ_c = a e^{iφ} (spec 12 §13 predistortion)
        const IqCorrection c = awg_->correction(awgPort_);
        const Complex gc = std::polar(c.gainRatio, c.phaseSkewRad);
        alpha = (1.0 + gc) / 2.0;
        beta = (1.0 - gc) / 2.0;
    }
    const double image = std::abs(beta + e * std::conj(alpha));
    const double wanted = std::abs(alpha + e * std::conj(beta));
    return 20.0 * std::log10(std::max(image, 1e-15) / std::max(wanted, 1e-15));
}

Result<Signal> IqMixer::signal(std::string_view, const SignalRequest& request) const {
    const SignalGraph* graph = routing();
    if (!graph) return fail(err::NotBound, id().toString() + ": no routing matrix is bound");
    SignalRequest ifRequest = request;
    ifRequest.envelopeView = false;
    QXL_TRY_ASSIGN(Signal s, graph->signalAt(ifNode(), ifRequest));
    SignalRequest loRequest;
    loRequest.samples = s.samples.size();
    loRequest.sampleRateHz = s.sampleRateHz;
    loRequest.t0S = s.t0S;
    loRequest.noiseSeed = request.noiseSeed;
    QXL_TRY_ASSIGN(Signal lo, graph->signalAt(loNode(), loRequest));

    const SettingValues v = snapshot();
    Signal out;
    out.node = rfNode();
    out.referenceHz = lo.referenceHz;
    out.sampleRateHz = s.sampleRateHz;
    out.t0S = s.t0S;
    out.cls = FidelityClass::Model;
    out.noisePsdWPerHz = 1.380649e-23 * 290.0;
    out.samples.assign(s.samples.size(), Complex{});
    double loAmplitude = 0.0;
    for (auto const& z : lo.samples) loAmplitude = std::max(loAmplitude, std::abs(z));
    const double ifHz = awg_ ? awg_->intermediateFrequencyHz() : 0.0;
    out.landmarks = {{"carrier", lo.referenceHz + ifHz}, {"lo", lo.referenceHz}, {"image", lo.referenceHz - ifHz}};
    if (state() == State::Off || loAmplitude <= 0.0) { // no LO drive: nothing is converted
        out.connected = lo.connected;
        out.note = state() == State::Off ? "mixer switched off" : (lo.connected ? "LO off" : lo.note);
        return out;
    }
    double lossDb = v.real("conversion_loss_db");
    const double loDbm = dbmFromWatts(tonePowerWatts(loAmplitude));
    if (loDbm < kNominalLoDbm) lossDb += kNominalLoDbm - loDbm; // starved LO (Model)
    // The band limit is on the RF port, so it is the wanted sideband f_LO + f_IF that has to be in
    // band — not the LO itself (an LO just inside the edge can still put the carrier outside it).
    const double carrierHz = lo.referenceHz + ifHz;
    if (carrierHz < v.real("rf_min") || carrierHz > v.real("rf_max")) {
        lossDb += 40.0;
        out.note = "carrier outside the RF band of the mixer";
    }
    const double c = std::pow(10.0, -lossDb / 20.0);
    const double vfs = s.fullScaleV > 0.0 ? s.fullScaleV : 0.5;
    const Complex leak = coefficient(v.real("lo_leakage_dbc"), v.real("leakage_phase")) * vfs;
    const Complex image = coefficient(v.real("image_rejection_dbc"), v.real("imbalance_angle"));
    out.fullScaleV = c * vfs;
    // A source is free to return fewer samples than asked for; past the end of the LO record there
    // is no carrier to convert onto, so those samples stay zero.
    const std::size_t n = std::min(s.samples.size(), lo.samples.size());
    for (std::size_t k = 0; k < n; ++k) {
        const Complex u = s.samples[k];
        out.samples[k] = c * (u + leak + image * std::conj(u)) * (lo.samples[k] / loAmplitude);
    }
    if (!s.connected) out.note = s.note; // the leakage term survives a missing IF cable
    return out;
}

double IqMixer::sampleRateHz(std::string_view) const {
    const SignalGraph* graph = routing();
    return graph ? graph->sampleRateAt(ifNode()).value_or(0.0) : 0.0;
}

std::optional<double> IqMixer::query(std::string_view path) const {
    if (path == "lo_leak") return loLeakageDbc();
    if (path == "image_rej") return imageDbc();
    return InstrumentBase::query(path);
}

Result<Trace> IqMixer::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    if (channel.name == "lo_leak") return scalarTrace(channel, ctx, loLeakageDbc());
    if (channel.name == "image_rej") return scalarTrace(channel, ctx, imageDbc());
    SignalRequest request;
    request.noiseSeed = ctx.rng.next();
    QXL_TRY_ASSIGN(Signal s, signal("rf", request));
    Trace t = makeTrace(channel, ctx);
    const double dt = s.sampleRateHz > 0.0 ? 1.0 / s.sampleRateHz : 0.0;
    for (std::size_t k = 0; k < s.samples.size(); ++k) {
        t.x.push_back(s.t0S + static_cast<double>(k) * dt);
        t.y.push_back(s.samples[k].real());
        t.y_im.push_back(s.samples[k].imag());
    }
    for (auto const& [name, f] : s.landmarks) t.markers.push_back({0.0, 0.0, name, f, 0.0, "Hz"});
    return t;
}

} // namespace qlab::instr
