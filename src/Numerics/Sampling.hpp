#pragma once
// Spec 06 §9 — sampling from probability vectors (T11 §3.3).
#include "Core/Random.hpp"
#include "Numerics/Types.hpp"
#include <span>
#include <vector>
namespace qlab::num {

// Vose alias method: O(n) build, O(1) sample. Probabilities are normalised on build.
class AliasTable {
  public:
    AliasTable() = default;
    explicit AliasTable(std::span<const double> probs);
    std::size_t sample(core::Random& rng) const;
    std::size_t size() const { return prob_.size(); }

  private:
    std::vector<double> prob_;
    std::vector<std::size_t> alias_;
};

// Inverse-CDF sampling via binary search over the cumulative sum (O(log n) per sample).
class CumulativeSampler {
  public:
    CumulativeSampler() = default;
    explicit CumulativeSampler(std::span<const double> probs);
    std::size_t sample(core::Random& rng) const;
    std::size_t size() const { return cdf_.size(); }

  private:
    std::vector<double> cdf_;
};

// |ψ_i|² over an amplitude vector.
RealVector probabilities(std::span<const Complex> psi);
// Draw `shots` indices from a probability vector; uses alias for shots > 64, cumulative otherwise.
std::vector<std::size_t> sampleCounts(std::span<const double> probs, std::size_t shots,
                                      core::Random& rng);
// Chi-square statistic of observed counts vs expected probabilities (for tests).
double chiSquare(std::span<const std::size_t> counts, std::span<const double> probs);

} // namespace qlab::num
