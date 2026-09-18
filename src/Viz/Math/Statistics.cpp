// Spec 21 §2.5, §3.7 — shot statistics (see Statistics.hpp).
#include "Viz/Math/Statistics.hpp"
#include "Data/Fidelity.hpp"
#include "Core/StrongType.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace qlab::viz::math {

WilsonBar wilsonBar(std::uint64_t k, std::uint64_t n, double z) {
    const data::Interval iv = data::wilson(k, n, z);
    const double pHat = n == 0 ? 0.0 : static_cast<double>(k) / static_cast<double>(n);
    return {pHat, iv.lo, iv.hi};
}

double hellingerDistance(std::span<const double> pHat, std::span<const double> p) {
    const std::size_t n = std::min(pHat.size(), p.size());
    double bc = 0.0; // Bhattacharyya coefficient Σ √(p̂ p)
    for (std::size_t i = 0; i < n; ++i) bc += std::sqrt(std::max(0.0, pHat[i]) * std::max(0.0, p[i]));
    return std::sqrt(std::max(0.0, 1.0 - bc));
}

double totalVariationDistance(std::span<const double> pHat, std::span<const double> p) {
    const std::size_t n = std::max(pHat.size(), p.size());
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += std::abs((i < pHat.size() ? pHat[i] : 0.0) - (i < p.size() ? p[i] : 0.0));
    return 0.5 * s;
}

double pauliStandardError(double expectation, std::uint64_t shots) {
    if (shots == 0) return 1.0;
    return std::sqrt(std::max(0.0, 1.0 - expectation * expectation) / static_cast<double>(shots));
}

Result<HistogramModel> buildHistogram(const data::Histogram& input, std::span<const double> idealIn,
                                      const HistogramOptions& options) {
    const std::size_t nbitsIn = input.nbits();
    if (nbitsIn > 62) return fail(ErrorCode::OutOfRange, "histogram: more than 62 classical bits");
    // Ideal distribution keyed by little-endian index. A full vector wins over Histogram::theory.
    std::map<std::uint64_t, double> ideal;
    bool hasIdeal = false;
    if (!idealIn.empty()) {
        if (nbitsIn > 24 || idealIn.size() != (std::size_t{1} << nbitsIn))
            return fail(ErrorCode::InvalidArgument, "histogram: the ideal distribution must have 2^nbits entries");
        for (std::size_t i = 0; i < idealIn.size(); ++i)
            if (idealIn[i] > 0.0) ideal[i] = idealIn[i];
        hasIdeal = true;
    } else if (input.theory && input.theory->size() == input.distinct()) {
        std::size_t k = 0;
        for (const auto& [label, c] : input.raw()) ideal[data::Histogram::indexFromLabel(label)] = (*input.theory)[k++];
        hasIdeal = true;
    }

    data::Histogram counts = input;
    std::size_t nbits = nbitsIn;
    if (!options.marginalBits.empty()) {
        std::vector<QubitIndex> subset;
        for (std::size_t b : options.marginalBits) {
            if (b >= nbitsIn) return fail(ErrorCode::OutOfRange, "histogram: marginal bit " + std::to_string(b) + " out of range");
            subset.emplace_back(static_cast<std::uint32_t>(b));
        }
        counts = input.marginal(options.marginalBits);
        nbits = options.marginalBits.size();
        std::map<std::uint64_t, double> reduced;
        for (const auto& [idx, p] : ideal) {
            std::uint64_t m = 0;
            for (std::size_t k = 0; k < subset.size(); ++k) m |= ((idx >> subset[k].get()) & 1u) << k;
            reduced[m] += p;
        }
        ideal = std::move(reduced);
    }

    HistogramModel model;
    model.nbits = nbits;
    model.shots = counts.total();
    model.distinct = counts.distinct();
    model.hasIdeal = hasIdeal;
    model.cls = hasIdeal ? data::weakest(counts.cls, data::FidelityClass::Statistical) : counts.cls;

    // Distances use every outcome, before any truncation (spec 21 §3.7).
    if (hasIdeal && model.shots > 0) {
        double bc = 0.0, l1 = 0.0, idealSeen = 0.0;
        for (const auto& [label, c] : counts.raw()) {
            const double pHat = static_cast<double>(c) / static_cast<double>(model.shots);
            const auto it = ideal.find(data::Histogram::indexFromLabel(label));
            const double p = it == ideal.end() ? 0.0 : it->second;
            bc += std::sqrt(pHat * p);
            l1 += std::abs(pHat - p);
            idealSeen += p;
        }
        double idealTotal = 0.0;
        for (const auto& [idx, p] : ideal) idealTotal += p;
        l1 += std::max(0.0, idealTotal - idealSeen); // outcomes with p > 0 that were never observed
        model.hellinger = std::sqrt(std::max(0.0, 1.0 - bc));
        model.totalVariation = 0.5 * l1;
    }

    // Bars: observed outcomes (top-k by count) plus unobserved outcomes the ideal run can produce.
    std::vector<std::pair<std::string, std::uint64_t>> rows =
        options.maxOutcomes > 0 && counts.distinct() > options.maxOutcomes ? counts.topK(options.maxOutcomes) : counts.all();
    std::uint64_t listed = 0;
    for (const auto& [label, c] : rows) listed += c;
    for (const auto& [label, c] : rows) {
        HistogramBar bar;
        bar.label = label;
        bar.index = data::Histogram::indexFromLabel(label);
        bar.count = c;
        bar.estimate = wilsonBar(c, model.shots, options.z);
        if (hasIdeal) {
            const auto it = ideal.find(bar.index);
            bar.ideal = it == ideal.end() ? 0.0 : it->second;
        }
        model.bars.push_back(std::move(bar));
    }
    if (hasIdeal)
        for (const auto& [idx, p] : ideal) {
            if (p <= 1e-12 || counts.count(data::Histogram::labelFromIndex(idx, nbits)) > 0) continue;
            if (options.maxOutcomes > 0 && model.bars.size() >= options.maxOutcomes) break;
            HistogramBar bar;
            bar.label = data::Histogram::labelFromIndex(idx, nbits);
            bar.index = idx;
            bar.estimate = wilsonBar(0, model.shots, options.z);
            bar.ideal = p;
            model.bars.push_back(std::move(bar));
        }
    if (options.order == HistogramOrder::ByCount)
        std::stable_sort(model.bars.begin(), model.bars.end(), [](const HistogramBar& a, const HistogramBar& b) {
            return a.count != b.count ? a.count > b.count : a.index < b.index;
        });
    else
        std::sort(model.bars.begin(), model.bars.end(), [](const HistogramBar& a, const HistogramBar& b) { return a.index < b.index; });
    if (listed < model.shots) {
        HistogramBar other;
        other.label = "other";
        other.other = true;
        other.count = model.shots - listed;
        other.estimate = wilsonBar(other.count, model.shots, options.z);
        model.bars.push_back(std::move(other));
    }
    double running = 0.0;
    for (HistogramBar& bar : model.bars) {
        running += bar.estimate.pHat;
        bar.cumulative = running;
    }
    return model;
}

} // namespace qlab::viz::math
