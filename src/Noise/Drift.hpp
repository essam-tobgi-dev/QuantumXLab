#pragma once
// Spec 08 §2.6 — slow (1/f-like) frequency noise as a shot-constant detuning δf ~ N(0, σ_f).
// Because δf is constant within a shot it is correlated across every idle window of that shot:
// an echo refocuses it (T2_echo = T2) while Ramsey sees the Gaussian envelope e^{−(t/T2*)²}
// (T04 §8.2–8.3). Backends therefore average over *whole shots*: per-shot samples (StateVector,
// Trajectories) or the Gauss–Hermite nodes below (DensityMatrix, Lindblad; spec 08 §7.4).
#include "Core/Random.hpp"
#include "Noise/Types.hpp"
#include <vector>

namespace qlab::noise {

// (2.5): T2* = √2/(2π σ_f)  ⇔  σ_f = √2/(2π T2*). A non-positive or infinite T2* means no drift.
double driftSigmaFromT2Star(double t2StarS);
double t2StarFromDriftSigma(double sigmaHz);
// Loader rule of spec 08 §4.1: σ_f from T2* when T2* < T2, otherwise 0.
double driftSigmaFromCalibration(double t2StarS, double t2S);
// One per-shot detuning sample, in Hz.
double sampleDetuning(double sigmaHz, core::Random& rng);

struct DetuningNode {
    double detuningHz = 0.0;
    double weight = 0.0; // Σ weight = 1
};
// Gauss–Hermite rule for E[f(δf)], δf ~ N(0, σ): exact for polynomials of degree ≤ 2n − 1. Spec 08
// §7.4 uses 7 nodes; tests that resolve e^{−(t/T2*)²} out to t ≈ 2 T2* use more. 1 ≤ nodes ≤ 64.
Result<std::vector<DetuningNode>> gaussHermiteDetunings(double sigmaHz, std::size_t nodes = 7);

} // namespace qlab::noise
