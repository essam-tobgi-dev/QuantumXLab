#pragma once
// Spec 22 §2 — bitstring histogram (little-endian rendered), Histogram1D, Histogram2D, Wilson.
#include "Data/Fidelity.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace qlab::data {

// Wilson score interval for a binomial proportion with k successes in n trials.
// z = 1 gives the 68.3 % interval used for Statistical error bars (spec 21 §2.5).
struct Interval {
    double lo, hi, center;
};
Interval wilson(std::uint64_t k, std::uint64_t n, double z = 1.0);
// Binomial σ with p clipped to [0.5/N, 1 - 0.5/N] (spec 22 §5.1).
double binomialSigma(std::uint64_t k, std::uint64_t n);

// Bitstring labels are rendered most-significant qubit first: label[0] is q_{n-1}, label[n-1]
// is q_0 (README conventions). `bit k` below always means qubit k, i.e. label[n-1-k].
class Histogram {
  public:
    Histogram() = default;
    explicit Histogram(std::size_t nbits) : nbits_(nbits) {}
    void add(const std::string& label, std::uint64_t count = 1);
    void add(std::uint64_t index, std::uint64_t count = 1); // little-endian integer index
    std::uint64_t count(const std::string& label) const;
    std::uint64_t total() const { return total_; }
    std::size_t nbits() const { return nbits_; }
    std::size_t distinct() const { return counts_.size(); }
    double probability(const std::string& label) const;
    // Sorted descending by count; ties broken by label.
    std::vector<std::pair<std::string, std::uint64_t>> topK(std::size_t k) const;
    std::vector<std::pair<std::string, std::uint64_t>> all() const; // sorted by label
    // Marginal over the given qubit indices (kept in the order given, highest listed first
    // in the rendered label, i.e. the output label follows the same convention).
    Histogram marginal(std::span<const std::size_t> qubits) const;
    Interval interval(const std::string& label, double z = 1.0) const {
        return wilson(count(label), total_, z);
    }
    std::optional<std::vector<double>> theory; // Born probabilities for labels in all() order
    static std::string labelFromIndex(std::uint64_t index, std::size_t nbits);
    static std::uint64_t indexFromLabel(const std::string& label);
    const std::map<std::string, std::uint64_t>& raw() const { return counts_; }
    FidelityClass cls = FidelityClass::Statistical;

  private:
    std::size_t nbits_ = 0;
    std::uint64_t total_ = 0;
    std::map<std::string, std::uint64_t> counts_;
};

struct Histogram1D {
    std::vector<double> edges; // size bins+1
    std::vector<std::uint64_t> counts;
    std::uint64_t total = 0;
    static Histogram1D build(std::span<const double> values,
                             std::size_t bins = 0); // 0 -> Freedman–Diaconis
    double binWidth() const { return edges.size() > 1 ? edges[1] - edges[0] : 0.0; }
};

struct Histogram2D {
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    std::size_t nx = 0, ny = 0;
    std::vector<std::uint64_t> counts; // row-major [iy * nx + ix]
    std::uint64_t total = 0;
    static Histogram2D build(std::span<const double> xs, std::span<const double> ys,
                             std::size_t nx = 128, std::size_t ny = 128,
                             std::optional<std::array<double, 4>> range = std::nullopt);
    std::uint64_t at(std::size_t ix, std::size_t iy) const { return counts[iy * nx + ix]; }
};

} // namespace qlab::data
