#pragma once
// Spec 21 §2.5, §3.7 — shot statistics of the probability histogram: Wilson score intervals at
// 68.3 % (z = 1), Hellinger and total-variation distance to the exact Born distribution, sorting,
// marginals, the cumulative curve and the "other" bin. Pure functions over data::Histogram.
#include "Core/Error.hpp"
#include "Data/Fidelity.hpp"
#include "Data/Histogram.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::viz::math {

// Wilson score interval of k successes in n shots (spec 21 §2.5; data::wilson). z = 1 is the 1σ
// equivalent; the interval is [0, 1] when n = 0.
struct WilsonBar {
    double pHat = 0.0, lo = 0.0, hi = 1.0;
    bool contains(double p) const { return p >= lo && p <= hi; }
};
WilsonBar wilsonBar(std::uint64_t k, std::uint64_t n, double z = 1.0);

// H(p̂, p) = sqrt(1 − Σ_i sqrt(p̂_i p_i)) and TVD = ½ Σ_i |p̂_i − p_i| over aligned distributions.
double hellingerDistance(std::span<const double> pHat, std::span<const double> p);
double totalVariationDistance(std::span<const double> pHat, std::span<const double> p);
// Standard error of a Pauli expectation estimated from N shots (spec 21 §3.11): √((1 − ⟨P⟩²)/N).
double pauliStandardError(double expectation, std::uint64_t shots);

enum class HistogramOrder : std::uint8_t { ByLabel, ByCount };

struct HistogramOptions {
    HistogramOrder order = HistogramOrder::ByLabel;
    std::size_t maxOutcomes = 4096; // spec 21 §3.7: top-k by count, the rest aggregated as "other"
    std::vector<std::size_t>
        marginalBits; // non-empty: marginal over these bit indices (bit 0 = rightmost)
    double z = 1.0;
};

struct HistogramBar {
    std::string label;       // bitstring, bit 0 rightmost; "other" for the aggregate
    std::uint64_t index = 0; // little-endian integer of the label (0 for "other")
    std::uint64_t count = 0;
    WilsonBar estimate;
    std::optional<double> ideal; // exact Born probability when the run had one
    double cumulative = 0.0;     // Σ p̂ up to and including this bar, in display order
    bool other = false;
};

struct HistogramModel {
    std::vector<HistogramBar> bars;
    std::size_t nbits = 0;
    std::uint64_t shots = 0;
    std::size_t distinct = 0; // distinct outcomes before the "other" aggregation
    bool hasIdeal = false;
    std::optional<double> hellinger, totalVariation; // printed with the number of shots
    data::FidelityClass cls = data::FidelityClass::Statistical;
};

// `ideal` is the exact distribution over all 2^nbits outcomes (little-endian index), or empty; when
// empty, `counts.theory` (aligned with `counts.all()`) is used if present. With `marginalBits` both
// the counts and the ideal distribution are marginalised first. A zero-probability ideal outcome
// that was never observed is not listed; an ideal outcome with p > 0 is, with zero counts, so the
// overlay never silently loses probability mass.
Result<HistogramModel> buildHistogram(const data::Histogram& counts,
                                      std::span<const double> ideal = {},
                                      const HistogramOptions& options = {});

} // namespace qlab::viz::math
