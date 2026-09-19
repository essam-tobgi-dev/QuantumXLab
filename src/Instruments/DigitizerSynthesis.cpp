// Spec 12 §5 — shot synthesis, demodulation, calibration run and discriminator training.
#include "Instruments/DigitizerInternal.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::instr {
namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;
}

std::vector<double> Digitizer::Synth::meanOf(std::span<const Complex> field) const {
    std::vector<double> m(field.size());
    for (std::size_t k = 0; k < field.size(); ++k)
        m[k] = (voltsPerRootPhoton * field[k] *
                std::polar(1.0, kTwoPi * ifHz * static_cast<double>(k) / sampleRateHz))
                   .real();
    return m;
}

std::vector<double> Digitizer::Synth::record(std::span<const Complex> field,
                                             core::Random& rng) const {
    std::vector<double> v = meanOf(field);
    const double half = static_cast<double>(1u << (bits - 1));
    for (double& x : v) {
        x += rng.normal(0.0, noiseRmsV);
        x = std::clamp(std::round(x / lsbV), -half, half - 1.0) * lsbV;
    }
    return v;
}

Complex Digitizer::Synth::demodulate(std::span<const double> rec) const {
    Complex acc{};
    for (std::size_t k = 0; k < rec.size() && k < demod.size(); ++k)
        acc += demod[k] * rec[k];
    return acc;
}

Result<Digitizer::Synth> Digitizer::prepare(std::uint32_t k, const SettingValues& v) const {
    Synth s;
    QXL_TRY_ASSIGN(s.params, readoutParams(k));
    s.states = static_cast<int>(v.integer("states"));
    s.sampleRateHz = v.real("sample_rate");
    s.ifHz = v.real("demod_if");
    s.bits = static_cast<int>(v.integer("resolution_bits"));
    s.rangeV = v.real("input_range_v");
    s.lsbV = 2.0 * s.rangeV / static_cast<double>(1u << s.bits);
    s.averages = static_cast<std::size_t>(v.integer("averages"));
    const auto n = static_cast<std::size_t>(std::llround(s.params.windowS * s.sampleRateHz));
    if (n < 8)
        return fail(err::BadInput, std::format("{}: acquisition window of {} samples is too short",
                                               id().toString(), n));
    if (n > static_cast<std::size_t>(v.integer("memory_samples")))
        return fail(
            err::MemoryOverflow,
            std::format("acquisition window of {} samples exceeds the {} samples of record memory",
                        n, v.integer("memory_samples")));
    s.response = cavityResponse(s.params, s.sampleRateHz, s.states);
    const std::string kind = v.text("integration");
    if (kind == "boxcar")
        s.weights = boxcarWeights(n);
    else if (kind == "matched")
        s.weights = matchedWeights(s.response);
    else {
        std::lock_guard lk(demodMu_);
        const auto& custom = demod_[k].customWeights;
        if (custom.empty())
            return fail(
                err::BadInput,
                std::format("{}: integration = custom but no weights are loaded", id().toString()));
        s.weights.resize(n);
        for (std::size_t j = 0; j < n; ++j)
            s.weights[j] = custom[std::min(custom.size() - 1, j * custom.size() / n)];
    }
    s.demod.resize(n);
    double energy = 0.0;
    for (std::size_t j = 0; j < n; ++j) {
        s.demod[j] = 2.0 / static_cast<double>(n) * s.weights[j] *
                     std::polar(1.0, -kTwoPi * s.ifHz * static_cast<double>(j) / s.sampleRateHz);
        energy += std::norm(s.demod[j]);
    }
    s.voltsPerRootPhoton = voltsPerRootPhoton(s.params);
    s.noiseRmsV = adcNoiseRms(s.params, s.sampleRateHz);
    for (int st = 0; st < s.states; ++st) {
        s.meanRecord.push_back(s.meanOf(s.response.alpha[static_cast<std::size_t>(st)]));
        s.meanIq.push_back(s.demodulate(s.meanRecord.back()));
    }
    // Complex variance of the point is Σ|c_k|² σ²; half of it per quadrature. Quantisation adds
    // Δ²/12.
    s.sigmaIq = std::sqrt((s.noiseRmsV * s.noiseRmsV + s.lsbV * s.lsbV / 12.0) * energy / 2.0);
    s.snrExpected = weightedSnr(s.params, s.response, s.weights);
    s.snrTheory = theorySnr(s.params);
    return s;
}

Result<std::vector<IqPoint>>
Digitizer::measure(std::uint32_t k, std::span<const std::uint8_t> states, core::Random& rng) {
    if (k >= kDemodChannels)
        return fail(err::UnknownChannel,
                    std::format("{}: no demodulator ch[{}]", id().toString(), k));
    const SettingValues v = snapshot();
    QXL_TRY_ASSIGN(Synth s, prepare(k, v));
    const ReadoutParams& p = s.params;
    std::optional<Discriminator> disc;
    {
        std::lock_guard lk(demodMu_);
        disc = demod_[k].discriminator;
    }
    std::vector<IqPoint> out;
    out.reserve(states.size());
    for (std::uint8_t prepared : states) {
        if (prepared >= s.states)
            return fail(err::BadInput,
                        std::format("{}: state |{}> needs `states` = {}", id().toString(),
                                    static_cast<int>(prepared), prepared + 1));
        Complex iq;
        if (s.averages == 1) { // one shot in the time domain
            int effective = prepared;
            std::optional<double> decayAt;
            const double u = rng.uniform();
            if (prepared == 0 && u < p.flip01)
                effective = 1;
            else if (prepared == 1 && u < p.flip10)
                effective = 0;
            else if (prepared == 1 && u < p.flip10 + p.decay10 &&
                     p.t1S > 0.0) // T1 decay inside the window
                decayAt = -p.t1S * std::log1p(-rng.uniform() * -std::expm1(-p.windowS / p.t1S));
            if (decayAt)
                iq = s.demodulate(s.record(decayedField(p, s.sampleRateHz, *decayAt), rng));
            else
                iq = s.demodulate(
                    s.record(s.response.alpha[static_cast<std::size_t>(effective)], rng));
        } else { // mean of `averages` shots: exact Gaussian of the integrated noise around the
                 // mixed mean
            const double a = static_cast<double>(s.averages);
            Complex mean = s.meanIq[prepared];
            if (prepared <= 1) {
                const Complex other = s.meanIq[1 - prepared];
                const double flips = std::min(
                    a, static_cast<double>(rng.poisson(a * (prepared == 0 ? p.flip01 : p.flip10))));
                const double decays =
                    prepared == 1
                        ? std::min(a - flips, static_cast<double>(rng.poisson(a * p.decay10)))
                        : 0.0;
                mean =
                    ((a - flips - decays) * mean + flips * other + decays * 0.5 * (mean + other)) /
                    a;
            }
            const double sigma = s.sigmaIq / std::sqrt(a);
            iq = mean + Complex{rng.normal(0.0, sigma), rng.normal(0.0, sigma)};
        }
        IqPoint pt{iq.real(), iq.imag(), static_cast<std::int8_t>(prepared), -1};
        if (disc)
            pt.assigned = static_cast<std::int8_t>(disc->classify(pt.i, pt.q));
        out.push_back(pt);
    }
    const auto keep = static_cast<std::size_t>(v.integer("cloud_points"));
    std::lock_guard lk(demodMu_);
    auto& cloud = demod_[k].cloud;
    cloud.insert(cloud.end(), out.begin(), out.end());
    while (cloud.size() > keep)
        cloud.pop_front();
    return out;
}

Result<TrainingReport> Digitizer::train(std::uint32_t k, std::span<const IqPoint> labelled) {
    if (k >= kDemodChannels)
        return fail(err::UnknownChannel,
                    std::format("{}: no demodulator ch[{}]", id().toString(), k));
    const SettingValues v = snapshot();
    QXL_TRY_ASSIGN(Synth s, prepare(k, v));
    TrainingReport rep;
    QXL_TRY_ASSIGN(rep.discriminator, trainDiscriminator(labelled, s.states));
    rep.assignment = estimateAssignment(labelled, rep.discriminator);
    rep.snrMeasured = rep.discriminator.snr;
    rep.snrExpected = s.snrExpected * std::sqrt(static_cast<double>(s.averages)); // σ ∝ 1/√averages
    rep.snrTheory = s.snrTheory * std::sqrt(static_cast<double>(s.averages));
    rep.params = s.params;
    std::lock_guard lk(demodMu_);
    demod_[k].discriminator = rep.discriminator;
    demod_[k].training = rep;
    for (auto& pt : demod_[k].cloud)
        pt.assigned = static_cast<std::int8_t>(rep.discriminator.classify(pt.i, pt.q));
    return rep;
}

Result<TrainingReport> Digitizer::calibrate(std::uint32_t k, std::size_t shotsPerState,
                                            std::uint64_t seed) {
    if (state() == State::Off)
        return fail(err::PoweredOff, id().toString() + " is switched off");
    const int states = static_cast<int>(snapshot().integer("states"));
    std::vector<std::uint8_t> prepared;
    prepared.reserve(shotsPerState * static_cast<std::size_t>(states));
    for (std::size_t shot = 0; shot < shotsPerState; ++shot)
        for (int st = 0; st < states; ++st)
            prepared.push_back(static_cast<std::uint8_t>(st));
    core::Random rng(seed);
    QXL_TRY_ASSIGN(std::vector<IqPoint> shots, measure(k, prepared, rng));
    return train(k, shots);
}

} // namespace qlab::instr
