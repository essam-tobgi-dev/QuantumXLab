#pragma once
// Spec 08 §1 — operator-sum representation of a channel, T04 (1.2). Every Kraus set built through
// `make`/`fromPauliWeights` satisfies Σ K†K = I within num::tol::kTraceTol.
#include "Noise/Types.hpp"
#include "Numerics/Matrix.hpp"
#include <span>
#include <string>
#include <vector>

namespace qlab::noise {

struct Kraus {
    std::vector<num::Matrix> ops;     // K_k, each levels^arity square; targets[0] least significant
    std::uint32_t arity = 1;          // m targets
    std::uint32_t levels = 2;         // d per site
    bool isPauli = false;             // every K_k ∝ one Pauli string (Stabilizer + fast SV paths)
    std::vector<double> pauliWeights; // if isPauli: probabilities, same order as ops
    std::vector<std::uint32_t>
        pauliIndices; // if isPauli: Pauli-string index of ops[k] (see PauliTwirl)

    std::size_t dim() const;
    // A single unitary Kraus operator is a coherent error (spec 08 §1).
    bool isUnitary() const;
    // Exactly the identity channel (one operator ∝ I): callers may skip it.
    bool isIdentity() const;
    // Re-checks dimensions and Σ K†K = I; used by loaders and tests.
    Status validate(double tolAbs = num::tol::kTraceTol) const;

    // Validating constructor. Detects the Pauli structure for qubit channels of arity ≤ 3.
    static Result<Kraus> make(std::vector<num::Matrix> ops, std::uint32_t arity,
                              std::uint32_t levels = 2);
    // Pauli channel Σ_P w_P PρP from 4^arity weights (Σ w = 1). Zero-weight terms are dropped.
    static Result<Kraus> fromPauliWeights(std::uint32_t arity, std::span<const double> weights);
    // Single unitary (coherent error); rejects non-unitary input.
    static Result<Kraus> fromUnitary(num::Matrix u, std::uint32_t arity, std::uint32_t levels = 2);
};

// Pauli-string helpers. Index convention as PauliTwirl: base-4 digit k acts on targets[k].
num::Matrix pauliMatrix(std::uint32_t code); // 0..3 → I, X, Y, Z
num::Matrix pauliStringMatrix(std::uint32_t index,
                              std::uint32_t arity); // ⊗ with targets[0] least significant
std::string pauliStringLabel(std::uint32_t index,
                             std::uint32_t arity); // MSB-first, like qsim::PauliString
Result<std::uint32_t>
pauliStringIndex(std::string_view targetOrderLetters); // "ZX": Z on targets[0], X on targets[1]

// second ∘ first on the same targets: {B_j A_k}, zero products dropped (T04 §1.4).
Result<Kraus> compose(const Kraus& first, const Kraus& second);
// low ⊗ high: `low` acts on targets[0..low.arity), `high` on the following targets.
Result<Kraus> tensor(const Kraus& low, const Kraus& high);
// Qubit channel on d-level sites: K_0 acts as identity on the levels ≥ 2, the other operators as
// zero, so the set stays trace preserving (the backend's own padding would not be).
Result<Kraus> expandLevels(const Kraus& k, std::uint32_t levels);

// Pauli twirl p_P = Σ_k |Tr(P K_k)|²/d² — the diagonal of the χ matrix (T04 §5.3). Qubit channels
// only.
Result<PauliTwirl> pauliTwirl(const Kraus& k);
// Entanglement fidelity to a target unitary, F_e = Σ_k |Tr(U†K_k)|²/d² (T10 (1.2)); identity if
// empty.
double processFidelity(const Kraus& k, num::ConstMatrixView target = {});
// Average gate fidelity (d F_e + 1)/(d + 1) (T10 (1.3)).
double averageGateFidelity(const Kraus& k, num::ConstMatrixView target = {});
// Depolarizing-equivalent strength p = d/(d−1) · (1 − F_avg) of a channel (T10 (1.5), spec 08
// §4.1).
double depolarizingEquivalent(const Kraus& k);
// Σ K ρ K† on the channel's own space (inspector and tests; backends use their own kernels).
Result<num::Matrix> applyToDensity(const Kraus& k, const num::Matrix& rho);

} // namespace qlab::noise
