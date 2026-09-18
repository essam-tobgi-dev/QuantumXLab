#pragma once
// Spec 11 / T07 §9 / T08 — small physics helpers private to the cryo module (SI doubles).
// Units module is not a dependency of cryo; suffixes name the unit (T_K, P_W, f_Hz).
#include <cmath>
#include <algorithm>

namespace qlab::cryo::phys {

inline constexpr double kPlanck_Js = 6.62607015e-34;
inline constexpr double kBoltzmann_JK = 1.380649e-23;
inline constexpr double kStefanBoltzmann = 5.670374419e-8; // W m^-2 K^-4
inline constexpr double kHe3LatentHeat_Jmol = 25.0;        // near 0.8 K (spec 11 §1)
inline constexpr double kZ0_ohm = 50.0;

// Bose–Einstein occupation of a mode at f, T (T07 (9.1)).
inline double thermalPhotons(double f_Hz, double T_K) {
    if (T_K <= 0.0) return 0.0;
    double x = kPlanck_Js * f_Hz / (kBoltzmann_JK * T_K);
    if (x > 700.0) return 0.0;
    return 1.0 / std::expm1(x);
}

// Inverse of thermalPhotons: radiative temperature giving occupation n at f.
inline double effectiveTemperature(double f_Hz, double n) {
    if (n <= 0.0) return 0.0;
    return kPlanck_Js * f_Hz / (kBoltzmann_JK * std::log1p(1.0 / n));
}

// Qubit excited-state population implied by a photon bath of occupation n (T08 (10.1) form).
inline double populationFromPhotons(double n) { return n / (1.0 + 2.0 * n); }

// Effective qubit temperature from a measured excited population P1 at f.
inline double temperatureFromPopulation(double f_Hz, double P1) {
    P1 = std::clamp(P1, 1e-300, 0.5 - 1e-12);
    return kPlanck_Js * f_Hz / (kBoltzmann_JK * std::log((1.0 - P1) / P1));
}

inline double dbToLinear(double dB) { return std::pow(10.0, dB / 10.0); }
inline double linearToDb(double x) { return 10.0 * std::log10(x); }
inline double dbmToWatts(double dBm) { return 1e-3 * std::pow(10.0, dBm / 10.0); }
inline double wattsToDbm(double W) { return 10.0 * std::log10(W / 1e-3); }

// Power dissipated in an attenuator of A dB with input power P_in (spec 11 §2.3).
inline double attenuatorDissipation(double P_in_W, double A_dB) {
    return P_in_W * (1.0 - std::pow(10.0, -A_dB / 10.0));
}

// Radiative exchange between two surfaces with N floating shields (spec 11 §2.2).
inline double radiationLoad(double area_m2, double eps_hot, double eps_cold, int shields,
                            double T_hot_K, double T_cold_K) {
    double epsEff = 1.0 / (1.0 / eps_hot + 1.0 / eps_cold - 1.0) / (shields + 1.0);
    double th4 = T_hot_K * T_hot_K * T_hot_K * T_hot_K;
    double tc4 = T_cold_K * T_cold_K * T_cold_K * T_cold_K;
    return kStefanBoltzmann * epsEff * area_m2 * (th4 - tc4);
}

} // namespace qlab::cryo::phys
