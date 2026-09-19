#pragma once
// Spec 05 §7 — CODATA 2018 physical constants as SI-coherent constexpr quantities.
#include "Units/Dimension.hpp"
#include <numbers>

namespace qlab::units::consts {

// Exact (2019 SI redefinition)
inline constexpr Velocity c{299792458.0};                                // m/s
inline constexpr Action h{6.62607015e-34};                               // J s
inline constexpr Action hbar{6.62607015e-34 / (2.0 * std::numbers::pi)}; // 1.054571817e-34 J s
inline constexpr Charge e{1.602176634e-19};                              // C
inline constexpr HeatCapacity k_B{1.380649e-23};                         // J/K
inline constexpr Q<PerMolDim> N_A{6.02214076e23};                        // 1/mol
// Derived exact
inline constexpr MagneticFlux Phi0{6.62607015e-34 /
                                   (2.0 * 1.602176634e-19)}; // h/2e = 2.067833848e-15 Wb
inline constexpr Resistance R_K{6.62607015e-34 /
                                (1.602176634e-19 * 1.602176634e-19)}; // h/e^2 = 25812.80745 Ω
inline constexpr Energy eV{1.602176634e-19};                          // J
// Measured (CODATA 2018), uncertainty in the trailing digits noted
inline constexpr Q<PermeabilityDim> mu0{1.25663706212e-6};            // N/A^2 ± 1.9e-16
inline constexpr Q<PermittivityDim> eps0{8.8541878128e-12};           // F/m   ± 1.3e-21
inline constexpr Mass m_e{9.1093837015e-31};                          // kg    ± 2.8e-40
inline constexpr Mass u{1.66053906660e-27};                           // kg    ± 5.0e-37
inline constexpr Q<Dim<-2, 2, 0, 1, 0, 0, 0>> mu_B{9.2740100783e-24}; // J/T ± 2.8e-33
// Derived helpers
inline constexpr Q<Dim<-1, 0, 0, 0, -1, 0, 0>> kB_over_h{1.380649e-23 /
                                                         6.62607015e-34}; // Hz/K = 20.8366 GHz/K
inline constexpr Q<Dim<1, 0, 0, 0, 1, 0, 0>> h_over_kB{6.62607015e-34 / 1.380649e-23}; // K s
inline constexpr MagneticFlux hOver2e = Phi0;

} // namespace qlab::units::consts
