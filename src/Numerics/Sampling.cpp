#include "Numerics/Sampling.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
namespace qlab::num {

AliasTable::AliasTable(std::span<const double> probs) {
    std::size_t n = probs.size();
    prob_.assign(n, 0.0);
    alias_.assign(n, 0);
    if (n == 0)
        return;
    double total = 0;
    for (double p : probs)
        total += std::max(0.0, p);
    if (total <= 0) {
        for (std::size_t i = 0; i < n; ++i) {
            prob_[i] = 1.0;
            alias_[i] = i;
        }
        return;
    }
    std::vector<double> scaled(n);
    for (std::size_t i = 0; i < n; ++i)
        scaled[i] = std::max(0.0, probs[i]) / total * static_cast<double>(n);
    std::vector<std::size_t> small, large;
    for (std::size_t i = 0; i < n; ++i)
        (scaled[i] < 1.0 ? small : large).push_back(i);
    while (!small.empty() && !large.empty()) {
        std::size_t s = small.back();
        small.pop_back();
        std::size_t l = large.back();
        large.pop_back();
        prob_[s] = scaled[s];
        alias_[s] = l;
        scaled[l] = (scaled[l] + scaled[s]) - 1.0;
        (scaled[l] < 1.0 ? small : large).push_back(l);
    }
    for (std::size_t i : large) {
        prob_[i] = 1.0;
        alias_[i] = i;
    }
    for (std::size_t i : small) {
        prob_[i] = 1.0;
        alias_[i] = i;
    }
}
std::size_t AliasTable::sample(core::Random& rng) const {
    std::size_t n = prob_.size();
    if (n == 0)
        return 0;
    double u = rng.uniform() * static_cast<double>(n);
    std::size_t i = static_cast<std::size_t>(u);
    if (i >= n)
        i = n - 1;
    double frac = u - static_cast<double>(i);
    return frac < prob_[i] ? i : alias_[i];
}
CumulativeSampler::CumulativeSampler(std::span<const double> probs) {
    cdf_.resize(probs.size());
    double acc = 0;
    for (std::size_t i = 0; i < probs.size(); ++i) {
        acc += std::max(0.0, probs[i]);
        cdf_[i] = acc;
    }
    if (acc > 0)
        for (auto& c : cdf_)
            c /= acc;
}
std::size_t CumulativeSampler::sample(core::Random& rng) const {
    if (cdf_.empty())
        return 0;
    double u = rng.uniform();
    auto it = std::lower_bound(cdf_.begin(), cdf_.end(), u);
    std::size_t i = static_cast<std::size_t>(it - cdf_.begin());
    return std::min(i, cdf_.size() - 1);
}
RealVector probabilities(std::span<const Complex> psi) {
    RealVector p(psi.size());
    for (std::size_t i = 0; i < psi.size(); ++i)
        p[i] = std::norm(psi[i]);
    return p;
}
std::vector<std::size_t> sampleCounts(std::span<const double> probs, std::size_t shots,
                                      core::Random& rng) {
    std::vector<std::size_t> counts(probs.size(), 0);
    if (probs.empty())
        return counts;
    if (shots > 64) {
        AliasTable t(probs);
        for (std::size_t s = 0; s < shots; ++s)
            ++counts[t.sample(rng)];
    } else {
        CumulativeSampler c(probs);
        for (std::size_t s = 0; s < shots; ++s)
            ++counts[c.sample(rng)];
    }
    return counts;
}
double chiSquare(std::span<const std::size_t> counts, std::span<const double> probs) {
    std::size_t total = std::accumulate(counts.begin(), counts.end(), std::size_t{0});
    double chi = 0;
    for (std::size_t i = 0; i < counts.size() && i < probs.size(); ++i) {
        double e = probs[i] * static_cast<double>(total);
        if (e > 0) {
            double d = static_cast<double>(counts[i]) - e;
            chi += d * d / e;
        }
    }
    return chi;
}
} // namespace qlab::num
