#include "Instruments/ReadoutChain.hpp"
#include "Instruments/Types.hpp"
#include "Units/Units.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::instr {
namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;

double detuning(const ReadoutParams& p, int state) {
    return (2.0 * state - 1.0) * p.chiRadS;
}

double driveAmplitude(const ReadoutParams& p) { // ε with |α_ss|² = photons at toneAmplitude 1
    return p.toneAmplitude *
           std::sqrt(p.photons * (p.kappaRadS * p.kappaRadS / 4.0 + p.chiRadS * p.chiRadS));
}

// Integrates the cavity from before the tone to the end of the window; `stateAt(t)` gives the qubit
// state as a function of time since the acquisition start. Exact per step for a held drive.
template <class StateAt>
std::vector<Complex> integrate(const ReadoutParams& p, double fs, StateAt stateAt) {
    const double h = 1.0 / fs;
    const auto n = static_cast<std::size_t>(std::llround(p.windowS * fs));
    const auto lead = static_cast<std::size_t>(std::ceil(std::max(p.acquireDelayS, 0.0) * fs));
    const double tStart = p.acquireDelayS - static_cast<double>(lead) * h; // ≤ 0: before the tone
    const pulse::Waveform tone =
        pulse::Waveform::gaussianSquare(p.toneS, p.toneSigmaS, p.toneRiseS);
    const double eps = driveAmplitude(p);
    std::vector<Complex> out(n);
    Complex alpha{};
    for (std::size_t j = 0; j < lead + n; ++j) {
        const double t = tStart + static_cast<double>(j) * h;
        if (j >= lead)
            out[j - lead] = alpha;
        const Complex lambda{p.kappaRadS / 2.0, detuning(p, stateAt(t - p.acquireDelayS))};
        const Complex decay = std::exp(-lambda * h);
        const double drive = eps * tone.sample(t + h / 2.0).real(); // envelope held over the step
        alpha = alpha * decay + drive / lambda * (1.0 - decay);
    }
    return out;
}

// P(assigned 0 | prepared 1, decayed during the window), averaged over the decay time.
double decayFlipProbability(const ReadoutParams& p, const CavityResponse& r,
                            std::span<const Complex> w, double snr) {
    Complex s0{}, s1{};
    for (std::size_t k = 0; k < w.size(); ++k) {
        s0 += w[k] * r.alpha[0][k];
        s1 += w[k] * r.alpha[1][k];
    }
    const Complex axis = s1 - s0;
    if (std::norm(axis) <= 0.0)
        return 0.5;
    constexpr int kSteps = 32;
    double acc = 0.0;
    for (int j = 0; j < kSteps; ++j) {
        const std::vector<Complex> field =
            decayedField(p, r.sampleRateHz, (j + 0.5) * p.windowS / kSteps);
        Complex sd{};
        for (std::size_t k = 0; k < w.size(); ++k)
            sd += w[k] * field[k];
        const double u =
            ((sd - 0.5 * (s0 + s1)) * std::conj(axis)).real() / std::norm(axis); // ±½ at the blobs
        acc += 0.5 * std::erfc(u * snr / std::numbers::sqrt2);
    }
    return acc / kSteps;
}
} // namespace

Complex steadyStateField(const ReadoutParams& p, int state) {
    return driveAmplitude(p) / Complex{p.kappaRadS / 2.0, detuning(p, state)};
}

CavityResponse cavityResponse(const ReadoutParams& p, double sampleRateHz, int states) {
    CavityResponse r;
    r.sampleRateHz = sampleRateHz;
    for (int s = 0; s < states; ++s)
        r.alpha.push_back(integrate(p, sampleRateHz, [s](double) { return s; }));
    return r;
}

std::vector<Complex> decayedField(const ReadoutParams& p, double sampleRateHz, double decayS) {
    return integrate(p, sampleRateHz, [decayS](double t) { return t < decayS ? 1 : 0; });
}

std::vector<Complex> matchedWeights(const CavityResponse& r) {
    std::vector<Complex> w(r.samples());
    double peak = 0.0;
    for (std::size_t k = 0; k < w.size(); ++k) {
        w[k] = std::conj(r.alpha[1][k] - r.alpha[0][k]);
        peak = std::max(peak, std::abs(w[k]));
    }
    if (peak > 0.0)
        for (auto& x : w)
            x /= peak;
    else
        std::fill(w.begin(), w.end(), Complex{1.0, 0.0});
    return w;
}

std::vector<Complex> boxcarWeights(std::size_t n) {
    return std::vector<Complex>(n, Complex{1.0, 0.0});
}

double theorySnr(const ReadoutParams& p) {
    const double separation = std::abs(steadyStateField(p, 1) - steadyStateField(p, 0));
    return separation * std::sqrt(2.0 * p.efficiency * p.kappaRadS * p.windowS);
}

double weightedSnr(const ReadoutParams& p, const CavityResponse& r,
                   std::span<const Complex> weights, int s0, int s1) {
    Complex signal{};
    double energy = 0.0;
    for (std::size_t k = 0; k < weights.size() && k < r.samples(); ++k) {
        signal += weights[k] * (r.alpha[static_cast<std::size_t>(s1)][k] -
                                r.alpha[static_cast<std::size_t>(s0)][k]);
        energy += std::norm(weights[k]);
    }
    return energy > 0.0 ? std::abs(signal) * std::sqrt(2.0 * p.efficiency * p.kappaRadS /
                                                       (r.sampleRateHz * energy))
                        : 0.0;
}

double voltsPerRootPhoton(const ReadoutParams& p) {
    const double gain = std::pow(10.0, p.gainDb / 10.0);
    return std::sqrt(kZ0 * gain * units::consts::hbar.v * kTwoPi * p.resonatorHz * p.kappaRadS);
}

double adcNoiseRms(const ReadoutParams& p, double sampleRateHz) {
    const double gain = std::pow(10.0, p.gainDb / 10.0);
    return std::sqrt(units::consts::k_B.v * p.tSysK * gain * kZ0 * sampleRateHz / 2.0);
}

double overlapError(double snr) {
    return 0.5 * std::erfc(snr / (2.0 * std::numbers::sqrt2));
}

double snrForOverlapError(double error) {
    double lo = 0.0, hi = 40.0;
    for (int it = 0; it < 200; ++it) {
        const double mid = 0.5 * (lo + hi);
        (overlapError(mid) > error ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

Result<ReadoutParams> readoutFromEnvironment(const Environment& env, std::uint32_t qubit,
                                             const ReadoutDefaults& d) {
    if (!env.calibration)
        return fail(err::NotBound, "readout: no calibration is bound");
    const hw::QubitCal* qc = env.calibration->qubit(qubit);
    if (!qc)
        return fail(err::BadInput, std::format("readout: calibration has no qubit {}", qubit));
    if (!qc->readoutChi || !qc->readoutKappa)
        return fail(
            err::BadInput,
            std::format("readout: qubit {} has no dispersive readout parameters (χ, κ)", qubit));
    ReadoutParams p;
    p.qubit = qubit;
    if (qc->readoutFrequency)
        p.resonatorHz = qc->readoutFrequency->value.v;
    else if (env.device && qubit < env.device->readout.resonatorFrequencies.size())
        p.resonatorHz = env.device->readout.resonatorFrequencies[qubit].v;
    else
        return fail(err::BadInput,
                    std::format("readout: qubit {} has no resonator frequency", qubit));
    p.kappaRadS = kTwoPi * qc->readoutKappa->value.v;
    p.chiRadS = kTwoPi * qc->readoutChi->value.v;
    const double calibratedWindow =
        qc->readoutDuration.value.v > 0.0 ? qc->readoutDuration.value.v : 700e-9;
    p.toneS = calibratedWindow;
    p.windowS = calibratedWindow;
    p.acquireDelayS = d.acquireDelayS;
    p.t1S = qc->t1.value.v;

    // Calibration anchor: the reference chain reproduces the calibrated matrix (spec 12 §15).
    const OutputChain reference = referenceOutputChain(p.resonatorHz);
    const double e01 = qc->readoutAssignment[0][1], e10 = qc->readoutAssignment[1][0];
    const double overlap = std::clamp(std::min(e01, e10), 1e-12, 0.4);
    ReadoutParams ref = p;
    ref.photons = 1.0;
    ref.efficiency = reference.efficiency;
    const CavityResponse unit = cavityResponse(ref, 1e9);
    const std::vector<Complex> w = matchedWeights(unit);
    const double snrPerRootPhoton = weightedSnr(ref, unit, w);
    const double snrReference = snrForOverlapError(overlap);
    p.photons = snrPerRootPhoton > 0.0
                    ? (snrReference / snrPerRootPhoton) * (snrReference / snrPerRootPhoton)
                    : 1.0;
    const double q01 = std::max(0.0, (e01 - overlap) / (1.0 - 2.0 * overlap));
    const double q10 = std::max(0.0, (e10 - overlap) / (1.0 - 2.0 * overlap));
    ref.photons = p.photons;
    const CavityResponse full = cavityResponse(ref, 1e9);
    const double flipGivenDecay = std::max(decayFlipProbability(ref, full, w, snrReference), 1e-6);
    const double pT1 = p.t1S > 0.0 ? -std::expm1(-calibratedWindow / p.t1S) : 0.0;
    p.decay10 = std::min(pT1, q10 / flipGivenDecay); // T1 decay up to its physical probability …
    p.flip10 = std::max(0.0, q10 - p.decay10 *
                                       flipGivenDecay); // … the rest is measurement-induced (Model)
    p.flip01 = q01;

    // The chain in use.
    const OutputChain chain = env.outputChain(qubit, p.resonatorHz);
    p.efficiency = chain.efficiency;
    p.tSysK = chain.tSysK;
    p.gainDb = chain.gainDb;
    p.lineId = chain.lineId;
    if (d.windowS > 0.0)
        p.windowS = d.windowS;
    p.toneAmplitude = d.toneAmplitude;
    return p;
}

} // namespace qlab::instr
