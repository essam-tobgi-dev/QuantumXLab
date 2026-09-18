#pragma once
// Spec 09 §5.4 — linear ion chain: equilibrium positions, normal modes, Lamb–Dicke parameters (T06 §1, §3).
#include "Core/Error.hpp"
#include "Numerics/Numerics.hpp"
#include "Units/Units.hpp"
#include <vector>

namespace qlab::hw::ion {

struct ChainParams {
    int count = 2;
    units::Mass mass{2.840e-25};          // 171Yb+ (kg)
    units::Frequency omegaZ{0.3e6};       // axial COM (ordinary frequency)
    units::Frequency omegaR{3.0e6};       // radial
    units::Length ramanWavelength{355e-9};
    double beamAngleCos = 1.0;            // cos θ of Δk with the mode axis (counter-propagating Raman: |Δk| = 2k)
};

struct Modes {
    std::vector<double> u;                          // equilibrium positions in units of ℓ
    units::Length lengthScale;                      // ℓ
    std::vector<units::Frequency> axial;            // ascending; [0] = COM
    num::RealMatrix axialVectors;                   // columns = participation b^(k)
    std::vector<units::Frequency> radial;           // descending; [0] = COM (highest)
    num::RealMatrix radialVectors;
    std::vector<std::vector<double>> etaAxial;      // [ion][mode]
    std::vector<std::vector<double>> etaRadial;
};

// Solves the dimensionless equilibrium (T06 (1.6)) by Newton iteration; returns sorted positions.
Result<std::vector<double>> equilibriumPositions(int n);
// Axial Hessian A (T06 (1.7)) and its modes.
num::RealMatrix axialHessian(const std::vector<double>& u);
Result<Modes> normalModes(const ChainParams& p);
// Length scale ℓ = (e² / 4πε0 m ω_z²)^{1/3} with ω_z angular.
units::Length lengthScale(units::Mass m, units::Frequency fz);
// Lamb–Dicke η = Δk cosθ b sqrt(ħ / 2 m ω) with ω angular.
double lambDicke(double deltaK, double participation, units::Mass m, units::Frequency fMode, double cosTheta = 1.0);
// Zigzag stability check (T06 §1.2 fit): ω_r/ω_z > 0.73 N^0.86 required for a linear chain.
bool linearChainStable(int n, units::Frequency fz, units::Frequency fr);

// Spin–motion operators for N qubits ⊗ one motional mode with Fock cutoff (little-endian: qubits low, mode high).
struct SpinMotionOps {
    std::size_t nQubits = 0; int fock = 8;
    num::SparseMatrix H0;                     // ħ ω_m a†a (+ 0 for qubits in their rotating frame), rad/s
    num::SparseMatrix a, adag;                // mode operators embedded
    std::vector<num::SparseMatrix> sigmaX, sigmaY, sigmaZ; // per qubit, embedded
};
SpinMotionOps spinMotionOperators(std::size_t nQubits, units::Frequency fMode, int fock);

} // namespace qlab::hw::ion
