#pragma once
// Spec 10 §7 — amplitude ↔ Rabi rate, the area theorem, DRAG, cross-resonance and
// Mølmer–Sørensen relations. Angular quantities are rad/s; times are seconds.
// Theory: T07 §2 (rotating frame, Rabi), T05 §7–§9 (DRAG, CR, flux CZ), T06 §6 (MS).
#include "Pulse/Waveform.hpp"
#include <span>

namespace qlab::pulse::physics {

// Ω(t) = κ_d · A · e(t): the calibrated line-to-qubit coupling κ_d is stored per qubit as
// `drive_rad_per_s_per_V` (spec 10 §7); `amplitude` is the dimensionless envelope scale.
inline double rabiRate(double kappaD, double amplitude) { return kappaD * amplitude; }

// Area theorem θ = ∫Ω dt = κ_d · A · area(e)  (exact for the two-level RWA model).
double rotationAngle(double kappaD, double amplitude, double envelopeAreaSeconds);
double rotationAngle(double kappaD, const Waveform& wf);
// Inverse: the amplitude that realises `theta` with the given envelope.
double amplitudeForAngle(double theta, double kappaD, double envelopeAreaSeconds);
double amplitudeForAngle(double theta, double kappaD, const Waveform& wf);

// First-order DRAG coefficient β = −1/α with α the *angular* anharmonicity (T05 (7.3)).
// For a transmon α < 0, so β > 0.
inline double dragBeta(double alphaAngular) { return alphaAngular == 0.0 ? 0.0 : -1.0 / alphaAngular; }

// Cross-resonance ZX rate (T05 (8.2)):  ν_ZX = −(J Ω_c / Δ)·(α_c / (Δ + α_c)),
// with J the exchange coupling, Ω_c the control Rabi rate, Δ = ω_c − ω_t, α_c the control
// anharmonicity — all angular (rad/s). The returned rate is angular too.
double crZxRate(double jAngular, double omegaControl, double detuning, double alphaControl);
// The IX term that the cancellation tone removes: ν_IX = −J Ω_c /(Δ + α_c).
double crIxRate(double jAngular, double omegaControl, double detuning, double alphaControl);
// Control amplitude A (with κ_d the control's coupling) giving ∫ν_ZX dt = θ over a pulse of
// envelope area `envelopeAreaSeconds`.
double crAmplitudeForAngle(double theta, double jAngular, double detuning, double alphaControl,
                           double kappaD, double envelopeAreaSeconds);

// ---- Mølmer–Sørensen (T06 §6) -----------------------------------------------------------
// Conventions of T06 (6.1)–(6.5): H = g(t) S_φ (a e^{−iδt} + a† e^{iδt}), g = ηΩ/2 with Ω the
// carrier Rabi rate of each tone, XX(θ) ≡ exp(−iθ/2 XX), θ = −4Φ(τ). A positive θ needs δ < 0.

// Loop closure of a square envelope: δ·τ = 2πK (T06 (6.3)).
double msLoopDuration(double deltaAngular, int K);        // τ = 2πK/|δ|
double msDetuningForDuration(double tau, int K);          // |δ| = 2πK/τ
// Square envelope, closed loop: θ = −η_i η_j Ω² τ / δ  (T06 (6.5) with η² → η_i η_j).
double msAngle(double etaI, double etaJ, double omega, double tau, double deltaAngular);
// Inverse of msAngle in magnitude: Ω = √(|θ δ| / (η_i η_j τ))  (spec 10 §6.6).
double msAmplitudeForAngle(double theta, double etaI, double etaJ, double tau, double deltaAngular);
// Maximally entangling square pulse |θ| = π/2: |δ| = 2 η Ω √K (T06 (6.6)).
double msMaxEntanglingDetuning(double eta, double omega, int K);

// Exact propagator data for a piecewise-constant coupling g_k [rad/s] held on
// [t0 + k·dt, t0 + (k+1)·dt): the spin-dependent displacement per unit eigenvalue of S_φ,
// α = −Σ g_k (e^{iδt_{k+1}} − e^{iδt_k})/δ  (T06 (11.1)), and the geometric phase
// Φ = ∫₀^τ dt ∫₀^t dt' g(t) g(t') sin δ(t − t')  (T06 (6.2)), so U = D(S α) e^{iΦ S²}.
struct MsIntegrals {
    Complex alpha;
    double phi = 0.0;
    double theta() const { return -4.0 * phi; } // XX angle, T06 (6.5)
};
MsIntegrals msIntegrals(std::span<const double> coupling, double dt, double deltaAngular, double t0 = 0.0);

} // namespace qlab::pulse::physics
