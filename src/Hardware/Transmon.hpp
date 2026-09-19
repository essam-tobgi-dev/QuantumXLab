#pragma once
// Spec 09 §5.1–5.3 — transmon physics builders (T05 §3–§6).
#include "Core/Error.hpp"
#include "Numerics/Numerics.hpp"
#include "Units/Units.hpp"

namespace qlab::hw::transmon {

// Charge-basis Cooper-pair-box spectrum: H = 4E_C(n − n_g)² − E_J cos φ, truncated to |n| ≤ N.
struct Spectrum {
    std::vector<double> energiesJ;  // lowest d eigenvalues (J), ascending
    num::Matrix vectors;            // columns = eigenvectors in the charge basis (2N+1 × d)
    units::Frequency f01;           // (E1 − E0)/h
    units::Frequency anharmonicity; // (E2 − 2E1 + E0)/h
    double nMatrixElement01 = 0.0;  // |⟨0|n̂|1⟩|
    double nMatrixElement12 = 0.0;  // |⟨1|n̂|2⟩|
};
// Energies given as E/h (Frequency). Returns d lowest levels. N = charge cutoff (spec: 30).
Result<Spectrum> chargeBasisSpectrum(units::Frequency EJ, units::Frequency EC, double ng = 0.0,
                                     int d = 3, int N = 30);
// Charge-dispersion of the 0-1 transition: max over n_g of E01 minus min (spec 09 §5.1).
Result<units::Frequency> chargeDispersion01(units::Frequency EJ, units::Frequency EC, int N = 30);
// Asymptotic transmon formulas (T05 (3.4)).
units::Frequency approxF01(units::Frequency EJ, units::Frequency EC);
units::Frequency approxAlpha(units::Frequency EC);
// Inverse problem: (f01, α) → (EJ, EC) by Newton iteration on the exact spectrum, 1e-10 relative.
struct EjEc {
    units::Frequency EJ, EC;
    int iterations = 0;
};
Result<EjEc> fromTargets(units::Frequency f01, units::Frequency alpha, int N = 30);
// SQUID flux tunability (T05 (4.1)); phi in units of Φ0.
units::Frequency josephsonEnergy(units::Frequency EJsum, double phiOverPhi0,
                                 double asymmetry = 0.0);
// Duffing/Kerr operators truncated to d levels: a, a†, n̂ = a†a, and H/ħ = ω a†a + (α/2) a†a†aa
// (rad/s units).
struct KerrOps {
    num::Matrix a, adag, n, H;
};
KerrOps kerrOperators(units::Frequency f01, units::Frequency alpha, int d);
// Static ZZ (T05 (5.3)): ζ/2π for coupling g (ordinary freq), detuning Δ12 = f1 − f2,
// anharmonicities α1, α2.
units::Frequency staticZZ(units::Frequency g, units::Frequency f1, units::Frequency f2,
                          units::Frequency alpha1, units::Frequency alpha2);
// Dispersive shift χ/2π (T05 (6.3)) with Δ = f_q − f_r.
units::Frequency dispersiveShift(units::Frequency g, units::Frequency fq, units::Frequency fr,
                                 units::Frequency alpha);
double criticalPhotonNumber(units::Frequency g, units::Frequency fq, units::Frequency fr);
// Purcell rate γ_P = κ g²/Δ² (ordinary-frequency inputs; returns 1/s).
double purcellRate(units::Frequency kappa, units::Frequency g, units::Frequency fq,
                   units::Frequency fr);
// Tunable coupler effective coupling (T05 (5.2)) and its off point.
units::Frequency effectiveCoupling(units::Frequency g12, units::Frequency g1c, units::Frequency g2c,
                                   units::Frequency f1, units::Frequency f2, units::Frequency fc);
// Solves g_eff(fc) = 0 for fc above both qubits by bisection in [max(f1,f2)+50 MHz, 12 GHz].
Result<units::Frequency> couplerOffPoint(units::Frequency g12, units::Frequency g1c,
                                         units::Frequency g2c, units::Frequency f1,
                                         units::Frequency f2);
// Charge-coupling g from capacitances (T05 (5.1)).
units::Frequency couplingFromCapacitance(units::Capacitance Cg, units::Capacitance C1,
                                         units::Capacitance C2, units::Frequency f1,
                                         units::Frequency f2);

} // namespace qlab::hw::transmon
