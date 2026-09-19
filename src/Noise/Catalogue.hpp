#pragma once
// Spec 08 §2 — the channel catalogue as closed-form Kraus sets (derivations in T04 §3–§5, §9).
// Every function validates its parameters and returns a CPTP-checked Kraus; times are in seconds,
// frequencies in hertz, probabilities dimensionless.
#include "Noise/Kraus.hpp"
#include <string_view>

namespace qlab::noise::channels {

// ---- §2.1 Pauli channels
Result<Kraus> bitFlip(double p);      // K0 = √(1−p) I, K1 = √p X
Result<Kraus> phaseFlip(double p);    // K0 = √(1−p) I, K1 = √p Z
Result<Kraus> bitPhaseFlip(double p); // K0 = √(1−p) I, K1 = √p Y
Result<Kraus> pauli(double px, double py, double pz);
// ρ → (1−p)ρ + p I/d with d = 2^n (T04 (5.2)): weight 1 − p(d²−1)/d² on I, p/d² on every other
// string.
Result<Kraus> depolarizing1q(double p);
Result<Kraus> depolarizing2q(double p);
Result<Kraus> depolarizingNq(std::uint32_t nQubits, double p);
// Gate error r = 1 − F_avg → depolarizing probability p = d r/(d−1) (T10 (1.5)); clamps to [0, 1].
double depolarizingFromGateError(double r, std::uint32_t nQubits);
double gateErrorFromDepolarizing(double p, std::uint32_t nQubits);

// ---- §2.2 amplitude damping
Result<Kraus> amplitudeDamping(double gamma);                             // (2.1)
Result<Kraus> generalizedAmplitudeDamping(double gamma, double pThermal); // (2.2)
// Duration form: γ = 1 − e^{−t/T1}; pThermal = excited-state equilibrium population.
Result<Kraus> amplitudeDampingOver(double t1S, double tS, double pThermal = 0.0);
// p_th = n/(1 + 2n) and its inverse (T04 (3.3)).
double thermalPopulationFromPhotons(double nThermal);
double photonsFromThermalPopulation(double pThermal);

// ---- §2.3 phase damping. Coherences are multiplied by √(1−λ) (T04 (4.2)).
Result<Kraus> phaseDamping(double lambda);
// Duration form: λ = 1 − e^{−2t/Tφ}, so that ρ01 decays as e^{−t/Tφ}.
Result<Kraus> phaseDampingOver(double tPhiS, double tS);
// 1/Tφ = 1/T2 − 1/(2T1) (2.4); +∞ when T2 = 2T1. Rejects T2 > 2T1 (err::Unphysical).
Result<double> pureDephasingTime(double t1S, double t2S);
// (2.2) followed by (2.3): populations relax with T1 towards pThermal, coherences as e^{−t/T2}
// exactly.
Result<Kraus> thermalRelaxation(double t1S, double t2S, double tS, double pThermal = 0.0);
// 1 − F_avg of thermalRelaxation in closed form, T04 (4.6); independent of pThermal.
double thermalRelaxationInfidelity(double t1S, double t2S, double tS);

// ---- §2.4 coherent errors (single unitary Kraus operator)
// R_n(ε) = exp(−i ε P/2) about the Pauli axis given in gate-argument order ("X"; "ZX" = Z⊗X on
// targets[0], targets[1]).
Result<Kraus> overRotation(std::string_view axis, double epsilonRad);
// R_Z(2π δf t) with R_Z(θ) = diag(e^{−iθ/2}, e^{+iθ/2}).
Result<Kraus> detuningPhase(double detuningHz, double tS);
// exp(−i 2π ζ t Z⊗Z/4), ζ in Hz (T04 (9.1)).
Result<Kraus> zzCrosstalk(double zetaHz, double tS);
// Over-rotation angle whose average infidelity equals rCoherent (spec 08 §4.1):
// 1 − F_avg = d/(d+1) · sin²(ε/2) for a Pauli axis on d = 2^n dimensions.
double overRotationAngleForInfidelity(double rCoherent, std::uint32_t nQubits);

// ---- §2.5 leakage on a d = 3 site: |1⟩→|2⟩ with pLeak, |2⟩→|1⟩ with pSeep.
Result<Kraus> leakage(double pLeak, double pSeep);

// ---- §3, §5.5 SPAM-related
Result<Kraus> resetError(double p); // bit flip with the residual excited population after reset
// Phase damping with λ = scale · (1 − e^{−2 t_ro/T2}) on an unmeasured feedline neighbour (§3):
// at scale 1 its coherence is multiplied by e^{−t_ro/T2}.
Result<Kraus> measurementDephasing(double readoutDurationS, double t2S, double scale);

// ---- §2.6 quasi-static detuning, shot-averaged over δf ~ N(0, σ_f) for one uninterrupted window:
// phase damping whose coherence factor is e^{−2π²σ_f²t²} = e^{−(t/T2*)²}.
Result<Kraus> gaussianDephasing(double sigmaHz, double tS);

} // namespace qlab::noise::channels
