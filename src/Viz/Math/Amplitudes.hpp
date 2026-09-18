#pragma once
// Spec 21 §2.3, §3.2–3.3 — selection of the basis states a view shows: threshold |a_i|² > ε,
// top-k by probability (partial sort, once per snapshot), ordering by index or magnitude, and
// marginal distributions over a qubit subset. Pure functions; indices are little-endian.
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include "Numerics/Types.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace qlab::viz::math {

using num::Complex;

inline constexpr double kDefaultAmplitudeThreshold = 1e-4; // spec 21 §3.2: |a_i|² > ε
inline constexpr std::size_t kDefaultTopK = 256;           // spec 21 §3.2
inline constexpr std::uint32_t kFullAmplitudeQubits = 20;  // above this the run supplies the top-k

struct BasisEntry {
    std::uint64_t index = 0;     // little-endian basis index
    Complex amplitude{};
    double probability = 0.0;    // |a|²
    double phase = 0.0;          // arg a ∈ (−π, π]
};

enum class AmplitudeOrder : std::uint8_t { ByIndex, ByMagnitude };

struct AmplitudeFilter {
    double threshold = kDefaultAmplitudeThreshold; // keep |a_i|² > threshold; ≤ 0 keeps everything
    std::size_t maxEntries = kDefaultTopK;         // top-k by |a_i|² after the threshold; 0 = no limit
    AmplitudeOrder order = AmplitudeOrder::ByIndex;
};

struct AmplitudeSelection {
    std::uint32_t nQubits = 0;
    std::vector<BasisEntry> entries;
    std::size_t totalStates = 0;        // 2^n
    std::size_t aboveThreshold = 0;     // how many passed the threshold before the top-k cut
    double shownProbability = 0.0;      // Σ |a_i|² of `entries` (the "probability mass shown")
    double totalProbability = 0.0;      // Σ |a_i|² of the whole state (1 up to round-off)
    bool truncated = false;             // the top-k cut dropped states above the threshold
};

// O(2^n) scan plus a partial sort of the survivors; spec 21 §2.3 allows it on the UI thread for up
// to 2^20 amplitudes.
AmplitudeSelection selectAmplitudes(std::span<const Complex> psi, const AmplitudeFilter& filter = {});

// |a_i|² for every basis state.
std::vector<double> bornProbabilities(std::span<const Complex> psi);
// Diagonal of a density matrix restricted to the computational subspace of `levels`-level sites.
Result<std::vector<double>> bornProbabilities(const num::Matrix& rho, std::uint32_t nSites, std::uint32_t levels);

// Marginal distribution over `subset` (subset[0] is the least significant bit of the result index).
// Probabilities are Physical-class data: a marginal has no phase (spec 21 §3.2).
Result<std::vector<double>> marginalProbabilities(std::span<const double> probabilities, std::uint32_t nQubits,
                                                  std::span<const QubitIndex> subset);

} // namespace qlab::viz::math
