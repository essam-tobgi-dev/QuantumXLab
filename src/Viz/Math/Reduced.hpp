#pragma once
// Spec 21 §2.1–2.2 — reduced states and the scalar measures the views print. Pure functions: no
// GL, no ImGui. Definitions are those of qsim::measures / num (T01 §5, §8); this layer adds the
// O(2^n) kernels the run job uses for one- and two-qubit reductions and the little-endian
// bookkeeping. Qubit 0 is the least significant bit of a basis index (README).
#include "Core/Error.hpp"
#include "Numerics/Types.hpp"
#include "QSim/Types.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace qlab::viz::math {

using num::Complex;
using num::Matrix;

// ρ_k of qubit k from a state vector, spec 21 §2.1:
//   (ρ_k)_ab = Σ_{j: j_k = 0} ψ_{j + a·2^k} ψ*_{j + b·2^k}.  Cost O(2^n).
Result<Matrix> reducedSingle(std::span<const Complex> psi, std::uint32_t nQubits, std::uint32_t k);

// ρ_ij (4×4) from a state vector, in the basis |q_j q_i⟩: index = b_i + 2·b_j, i.e. the first
// qubit named is the least significant, as for `num::reducedState(psi, n, {i, j})`. Cost O(2^n).
Result<Matrix> reducedPair(std::span<const Complex> psi, std::uint32_t nQubits, std::uint32_t i,
                           std::uint32_t j);

// General subset from a state vector (keep[0] least significant). Cost O(2^n · 2^|keep|).
Result<Matrix> reducedSubset(std::span<const Complex> psi, std::uint32_t nQubits,
                             std::span<const QubitIndex> keep);

// Partial trace of a density matrix over sites of `levels` levels each (2 for qubits, 3 for the
// transmon sites of the Lindblad backend); keep[0] least significant in the result.
Result<Matrix> reducedFromDensity(const Matrix& rho, std::uint32_t nSites, std::uint32_t levels,
                                  std::span<const QubitIndex> keep);

// The computational (two-level) block of a k-site reduced state with `levels` levels per site,
// NOT renormalised: its trace is 1 − P_leak, so leakage shortens the Bloch vector instead of
// being hidden (spec 21 §3.15 shows P_2 separately).
Result<Matrix> computationalBlock(const Matrix& rho, std::uint32_t nSites, std::uint32_t levels);

// Bloch vector r = (Tr ρX, Tr ρY, Tr ρZ) (spec 21 §2.2). H = −½ħωZ, so |0⟩ is +z (DEVELOPMENT.md).
struct BlochVector {
    double x = 0.0, y = 0.0, z = 1.0;
    double norm() const;
    double theta() const; // polar angle from +z, [0, π]; 0 for the null vector
    double phi() const;   // azimuth atan2(y, x), (−π, π]; 0 when x = y = 0
    double p1() const { return 0.5 * (1.0 - z); } // P(|1⟩) = (1 − r_z)/2
    std::array<double, 3> array() const { return {x, y, z}; }
};
Result<BlochVector> blochVector(const Matrix& rho1q);

double purity(const Matrix& rho);      // Tr ρ²
double entropyBits(const Matrix& rho); // S(ρ) = −Tr ρ log₂ ρ

// Spec 21 §2.2 / §3.8: entropies of two qubits and of the pair, I(i:j) = S_i + S_j − S_ij ∈ [0, 2]
// and the Wootters concurrence of ρ_ij.
struct PairMeasures {
    double entropyI = 0.0, entropyJ = 0.0, entropyIJ = 0.0;
    double mutualInformation = 0.0;
    double concurrence = 0.0;
};
// ρ_i = Tr_j ρ_ij and ρ_j = Tr_i ρ_ij are taken from the pair state itself (basis |q_j q_i⟩).
Result<PairMeasures> pairMeasures(const Matrix& rhoIJ);

// Spec 21 §3.9: Schmidt coefficients λ_k (descending, Σλ² = 1) of a pure state across A | Ā,
// S_A = −Σ λ² log₂ λ² and the Schmidt rank (number of λ_k above `rankTol`). |A| ≤ 14.
struct SchmidtSpectrum {
    std::vector<QubitIndex> partition;
    std::vector<double> coefficients;
    double entropyBits = 0.0;
    std::size_t rank = 0;
};
Result<SchmidtSpectrum> schmidtSpectrum(std::span<const Complex> psi, std::uint32_t nQubits,
                                        std::span<const QubitIndex> partition,
                                        double rankTol = 1e-10);

// Number of qubits of a 2^n state vector, or an error when the length is not a power of two.
Result<std::uint32_t> qubitCountOf(std::size_t amplitudes);

} // namespace qlab::viz::math
