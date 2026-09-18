#pragma once
// Spec 12 §5, T05 §6.3, T07 §6 — the dispersive readout signal the digitizer sees.
//
// Cavity. Driven at the bare resonator frequency, the field for qubit state s obeys
//   dα_s/dt = −(κ/2 + iδ_s) α_s + ε(t),   δ_s = (2s − 1) χ,
// whose step response is T07 §6's α_s^ss (1 − e^{−κt/2} e^{∓iχt}) with the pointer states of
// T05 (6.5), α_{0,1} = ε/(κ/2 ∓ iχ). |2⟩ is placed at δ_2 = 3χ (linear-in-level, Model).
//
// Units. The resonator is a notch on the feedline and radiates κ/2 toward the amplifier, so the
// power leaving for the output line is ħω_r (κ/2)|α|² and the envelope at the ADC is
//   Ṽ_s(t) = √(Z0 G ħω_r κ) α_s(t)  volts (peak),   v(t) = Re[Ṽ_s e^{i2π f_IF t}] + n(t),
// with n(t) white of one-sided density k_B T_sys G Z0 (T_sys referred to the chip, vacuum included).
// Boxcar integration of the steady state over T then separates the two clouds by
//   SNR = |μ_1 − μ_0|/σ = |α_1 − α_0| √(2ηκT),   η = T_q/T_sys, T_q = ħω/2k_B   (T05 (6.7), T07 (8.2)),
// which is the oracle of spec 25 §3.7.
//
// Calibration anchor (spec 12 §15). The calibrated assignment matrix belongs to the reference
// chain `readout_out_std` without a preamp. Its smaller error is the Gaussian overlap of that
// chain, which fixes the photon number of the calibrated tone; the rest of each error is a
// state transition during the window: T1 decay (up to its physical probability) and
// measurement-induced flips (Model). A quieter chain (TWPA) then shrinks only the overlap term.
#include "Instruments/Inputs.hpp"
#include <span>
#include <string>
#include <vector>

namespace qlab::instr {

using num::Complex;

struct ReadoutParams {
    std::uint32_t qubit = 0;
    double resonatorHz = 7.0e9;      // bare f_r
    double kappaRadS = 1.38e7;       // κ
    double chiRadS = -6.97e6;        // χ, signed (T05 (6.3))
    double photons = 5.0;            // steady-state n̄ at relative tone amplitude 1
    double efficiency = 0.5;         // η of the chain in use
    double tSysK = 0.34;             // T_sys of the chain in use, referred to the chip
    double gainDb = 66.0;            // chip → ADC
    double t1S = 0.0;                // 0: no decay modelled
    double toneS = 700e-9;           // readout tone length
    double windowS = 700e-9;         // acquisition length T
    double acquireDelayS = 100e-9;   // acquisition start after the tone start (spec 10 §6.7)
    double toneSigmaS = 20e-9, toneRiseS = 40e-9; // gaussian_square edges of the tone
    double toneAmplitude = 1.0;      // played amplitude / calibrated amplitude (n̄ ∝ amplitude²)
    // State transitions during the window (Model): instantaneous flips and T1 decay of |1⟩.
    double flip01 = 0.0, flip10 = 0.0, decay10 = 0.0;
    std::string lineId;              // output line the chain was evaluated on
};

// Expected cavity field on the acquisition grid t_k = acquireDelay + k/f_s, in √photons.
struct CavityResponse {
    double sampleRateHz = 1e9;
    std::vector<std::vector<Complex>> alpha; // [state][k]
    std::size_t samples() const { return alpha.empty() ? 0 : alpha[0].size(); }
};

Complex steadyStateField(const ReadoutParams& p, int state); // T05 (6.5), at toneAmplitude
CavityResponse cavityResponse(const ReadoutParams& p, double sampleRateHz, int states = 2);
// Field of a shot prepared in |1⟩ that relaxes to |0⟩ at `decayS` after the acquisition start.
std::vector<Complex> decayedField(const ReadoutParams& p, double sampleRateHz, double decayS);

// T07 (6.2) matched filter w ∝ conj(α_1 − α_0) scaled to max |w| = 1, or boxcar w = 1.
std::vector<Complex> matchedWeights(const CavityResponse& r);
std::vector<Complex> boxcarWeights(std::size_t n);

// T05 (6.7): |α_1 − α_0| √(2ηκT) with the steady-state pointer states — the theory line.
double theorySnr(const ReadoutParams& p);
// Separation over σ of the integrated points for these weights and this window, ring-up included:
// |Σ w Δα| √(2ηκ / (f_s Σ|w|²)). Equals theorySnr for boxcar weights on the steady state.
double weightedSnr(const ReadoutParams& p, const CavityResponse& r, std::span<const Complex> weights, int s0 = 0, int s1 = 1);
// Ṽ per √photon at the ADC, √(Z0 G ħω_r κ), and the rms noise per ADC sample, √(k_B T_sys G Z0 f_s/2).
double voltsPerRootPhoton(const ReadoutParams& p);
double adcNoiseRms(const ReadoutParams& p, double sampleRateHz);
// Overlap error of two equal Gaussians at separation SNR: ½ erfc(SNR / 2√2)   (T07 (6.3)).
double overlapError(double snr);
double snrForOverlapError(double error);

struct ReadoutDefaults {
    double acquireDelayS = 100e-9;
    double windowS = 0.0;        // 0: the calibrated readout duration
    double toneAmplitude = 1.0;
};
// Readout of `qubit` from the device calibration, the wiring's output chain at the current stage
// temperatures, and the calibration anchor above. Errors: NotBound (no calibration), BadInput
// (qubit without readout parameters).
Result<ReadoutParams> readoutFromEnvironment(const Environment& env, std::uint32_t qubit, const ReadoutDefaults& d = {});

} // namespace qlab::instr
