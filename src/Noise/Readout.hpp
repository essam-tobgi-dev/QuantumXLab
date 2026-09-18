#pragma once
// Spec 08 §3 — classical assignment errors at readout, and their mitigation (T10 §7).
// Convention (spec 09 §3): M is row-stochastic, M_ij = P(read j | prepared i), so a true
// distribution p is observed as q_j = Σ_i p_i M_ij, i.e. q = Mᵀp.
#include "Core/Random.hpp"
#include "Noise/Types.hpp"
#include "QSim/Types.hpp"
#include <array>
#include <span>
#include <vector>

namespace qlab::noise {

using Assignment2 = std::array<std::array<double, 2>, 2>; // same type as hw::Matrix2d

// One independent block of the assignment map: a single qubit (k = 1) or a correlated readout
// group of k ≤ 4 qubits sharing a feedline, which replaces the tensor product of its members.
struct ReadoutFactor {
    std::vector<std::size_t> positions; // indices into ReadoutModel::qubits(); local index little-endian
    num::RealMatrix matrix;             // 2^k × 2^k, matrix(i, j) = P(read j | prepared i)
};

struct Mitigation {
    std::vector<double> probabilities; // argmin ‖Mᵀp − q‖₂ subject to p ≥ 0, Σp = 1   (T10 (7.2))
    std::vector<double> unconstrained; // M^{-T} q: unbiased but possibly negative; empty if M is singular
    double residual = 0.0;             // ‖Mᵀp − q‖₂ at `probabilities`
    std::size_t iterations = 0;        // projected-gradient iterations; 0 when the inverse was feasible
    bool converged = true;
};

class ReadoutModel {
public:
    ReadoutModel() = default;
    // General form. Every measured qubit must be covered by exactly one factor.
    static Result<ReadoutModel> make(std::vector<QubitIndex> qubits, std::vector<ReadoutFactor> factors);
    // Independent errors: assignment[k] belongs to qubits[k].
    static Result<ReadoutModel> independent(std::vector<QubitIndex> qubits, std::span<const Assignment2> assignment);
    // Error-free readout of `qubits`.
    static ReadoutModel ideal(std::vector<QubitIndex> qubits);

    std::span<const QubitIndex> qubits() const { return qubits_; }
    std::size_t size() const { return qubits_.size(); } // measured bits n
    std::span<const ReadoutFactor> factors() const { return factors_; }
    bool isIdeal() const;

    // A prepared outcome i is reported as j with probability M_ij. bits[k] belongs to qubits()[k].
    Status applyToBits(std::span<std::uint8_t> bits, core::Random& rng) const;
    Result<std::size_t> applyToIndex(std::size_t prepared, core::Random& rng) const; // little-endian index
    // Noisy counts, shot by shot; keys are MSB-first bitstrings over qubits() like qsim::Counts.
    Result<qsim::Counts> applyToCounts(const qsim::Counts& ideal, core::Random& rng) const;
    // q = Mᵀp over all 2^n outcomes (DensityMatrix: applied to the diagonal before sampling, §7.1).
    Result<std::vector<double>> applyToProbabilities(std::span<const double> ideal) const;

    // Constrained least-squares inversion of a measured distribution (T10 (7.2)).
    Result<Mitigation> mitigate(std::span<const double> measured) const;
    Result<Mitigation> mitigateCounts(const qsim::Counts& measured) const;

    // Dense 2^n × 2^n row-stochastic matrix for display and tests (n ≤ 10).
    Result<num::RealMatrix> fullMatrix() const;

private:
    // out_local = B · in_local along one factor, for every setting of the other bits.
    void applyAlong(std::vector<double>& v, const ReadoutFactor& f, const num::RealMatrix& b) const;
    Status checkLength(std::size_t n) const;
    std::vector<QubitIndex> qubits_;
    std::vector<ReadoutFactor> factors_;
};

// Row-stochastic check used by the model loaders: entries in [0, 1], rows summing to 1 within 1e-6.
Status validateAssignment(const num::RealMatrix& m, std::string_view what);
num::RealMatrix toRealMatrix(const Assignment2& a);
// Readout fidelity F_ro = 1 − (M01 + M10)/2 (spec 08 §3).
double readoutFidelity(const Assignment2& a);
// Probability vector from counts (keys MSB-first over nBits), normalised by the shot total.
Result<std::vector<double>> probabilitiesFromCounts(const qsim::Counts& counts, std::size_t nBits);

} // namespace qlab::noise
