#pragma once
// Spec 05 §5 — energy–frequency conventions and thermal-photon helpers (T07 §9, T08 §7).
#include "Units/Constants.hpp"
#include "Units/Dimension.hpp"
#include <cmath>

namespace qlab::units {

// E = h f. Interfaces carry Frequency = E/h; Hamiltonian builders store Energy.
constexpr Energy energyFromFrequency(Frequency f) { return Energy(consts::h.v * f.v); }
constexpr Energy energyFromGHz(Frequency f) { return energyFromFrequency(f); }
constexpr Frequency frequencyFromEnergy(Energy E) { return Frequency(E.v / consts::h.v); }
constexpr AngularFrequency angularFromEnergy(Energy E) { return AngularFrequency(E.v / consts::hbar.v); }
constexpr Energy energyFromAngular(AngularFrequency w) { return Energy(consts::hbar.v * w.v); }
// k_B T / h : 20 mK ↔ 0.4167 GHz
constexpr Frequency thermalFrequency(Temperature T) { return Frequency(consts::k_B.v * T.v / consts::h.v); }
constexpr Temperature temperatureFromFrequency(Frequency f) { return Temperature(consts::h.v * f.v / consts::k_B.v); }
// Flux in units of Phi0.
constexpr double fluxQuanta(MagneticFlux phi) { return phi.v / consts::Phi0.v; }
constexpr MagneticFlux fromFluxQuanta(double n) { return MagneticFlux(n * consts::Phi0.v); }

// Bose–Einstein occupation n(f,T) = 1 / (exp(hf/k_B T) − 1). T ≤ 0 → 0 photons.
inline double thermalPhotons(Frequency f, Temperature T) {
    if (T.v <= 0.0) return 0.0;
    double x = consts::h.v * f.v / (consts::k_B.v * T.v);
    if (x > 700.0) return 0.0;
    return 1.0 / std::expm1(x);
}
inline double thermalPhotonNumber(Frequency f, Temperature T) { return thermalPhotons(f, T); }
// Inverse: T_eff = hf / (k_B ln(1 + 1/n)). n ≤ 0 → 0 K.
inline Temperature effectiveTemperature(Frequency f, double n) {
    if (n <= 0.0) return Temperature(0.0);
    return Temperature(consts::h.v * f.v / (consts::k_B.v * std::log1p(1.0 / n)));
}
// Two-level thermal population P1 = 1 / (1 + exp(hf/k_B T)) (T07 §9).
inline double thermalPopulation(Frequency f, Temperature T) {
    if (T.v <= 0.0) return 0.0;
    double x = consts::h.v * f.v / (consts::k_B.v * T.v);
    return 1.0 / (1.0 + std::exp(x));
}
inline Temperature temperatureFromPopulation(Frequency f, double p1) {
    if (p1 <= 0.0) return Temperature(0.0);
    if (p1 >= 0.5) return Temperature(INFINITY);
    return Temperature(consts::h.v * f.v / (consts::k_B.v * std::log(1.0 / p1 - 1.0)));
}
// Voltage on a matched line from power: V = sqrt(2 Z0 P) (peak), Vrms = sqrt(Z0 P).
inline Voltage peakVoltage(Power p, Resistance z0) { return Voltage(std::sqrt(2.0 * z0.v * p.v)); }
inline Voltage rmsVoltage(Power p, Resistance z0) { return Voltage(std::sqrt(z0.v * p.v)); }

} // namespace qlab::units
