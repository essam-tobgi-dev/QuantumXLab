// Spec 21 §2.3, §3.2 — basis-state selection (see Amplitudes.hpp).
#include "Viz/Math/Amplitudes.hpp"
#include "Numerics/Tensor.hpp"
#include "Viz/Math/Phase.hpp"
#include <algorithm>
#include <bit>

namespace qlab::viz::math {

AmplitudeSelection selectAmplitudes(std::span<const Complex> psi, const AmplitudeFilter& filter) {
    AmplitudeSelection out;
    out.totalStates = psi.size();
    out.nQubits = psi.empty() ? 0u : static_cast<std::uint32_t>(std::bit_width(psi.size()) - 1);
    // Pass 1: probabilities and the indices above the threshold (no BasisEntry yet: 2^20 states
    // would otherwise allocate 40 bytes each before the cut).
    std::vector<std::pair<double, std::uint64_t>> kept; // (probability, index)
    for (std::size_t i = 0; i < psi.size(); ++i) {
        const double p = std::norm(psi[i]);
        out.totalProbability += p;
        if (filter.threshold > 0.0 ? p > filter.threshold : true)
            kept.emplace_back(p, i);
    }
    out.aboveThreshold = kept.size();
    // Top-k by probability; ties broken by the smaller index so the result is deterministic.
    const auto byMagnitude = [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    };
    if (filter.maxEntries > 0 && kept.size() > filter.maxEntries) {
        std::nth_element(kept.begin(),
                         kept.begin() + static_cast<std::ptrdiff_t>(filter.maxEntries), kept.end(),
                         byMagnitude);
        kept.resize(filter.maxEntries);
        out.truncated = true;
    }
    if (filter.order == AmplitudeOrder::ByMagnitude)
        std::sort(kept.begin(), kept.end(), byMagnitude);
    else
        std::sort(kept.begin(), kept.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });
    out.entries.reserve(kept.size());
    for (const auto& [p, idx] : kept) {
        const Complex a = psi[idx];
        out.entries.push_back({idx, a, p, phaseOf(a)});
        out.shownProbability += p;
    }
    return out;
}

std::vector<double> bornProbabilities(std::span<const Complex> psi) {
    std::vector<double> p(psi.size());
    for (std::size_t i = 0; i < psi.size(); ++i)
        p[i] = std::norm(psi[i]);
    return p;
}

Result<std::vector<double>> bornProbabilities(const num::Matrix& rho, std::uint32_t nSites,
                                              std::uint32_t levels) {
    if (levels < 2 || nSites == 0 || nSites > 30)
        return fail(ErrorCode::InvalidArgument, "probabilities: bad site description");
    const std::size_t dim = num::ipow(levels, nSites);
    if (rho.rows != dim || rho.cols != dim)
        return fail(ErrorCode::InvalidArgument, "probabilities: dimension mismatch");
    std::vector<double> p(std::size_t{1} << nSites);
    for (std::size_t a = 0; a < p.size(); ++a) {
        std::size_t idx = 0, stride = 1;
        for (std::uint32_t s = 0; s < nSites; ++s, stride *= levels)
            idx += ((a >> s) & 1u) * stride;
        p[a] = rho(idx, idx).real();
    }
    return p;
}

Result<std::vector<double>> marginalProbabilities(std::span<const double> probabilities,
                                                  std::uint32_t nQubits,
                                                  std::span<const QubitIndex> subset) {
    if (nQubits == 0 || nQubits > 40 || probabilities.size() != (std::size_t{1} << nQubits))
        return fail(ErrorCode::InvalidArgument,
                    "marginal: the distribution does not have 2^n entries");
    if (subset.empty() || subset.size() > 24)
        return fail(ErrorCode::InvalidArgument, "marginal: subset must hold 1 to 24 qubits");
    std::uint64_t seen = 0;
    for (QubitIndex q : subset) {
        if (q.get() >= nQubits)
            return fail(ErrorCode::OutOfRange,
                        "marginal: qubit " + std::to_string(q.get()) + " out of range");
        if ((seen >> q.get()) & 1u)
            return fail(ErrorCode::InvalidArgument, "marginal: qubit listed twice");
        seen |= std::uint64_t{1} << q.get();
    }
    std::vector<double> out(std::size_t{1} << subset.size(), 0.0);
    for (std::size_t i = 0; i < probabilities.size(); ++i) {
        if (probabilities[i] == 0.0)
            continue;
        std::size_t m = 0;
        for (std::size_t k = 0; k < subset.size(); ++k)
            m |= ((i >> subset[k].get()) & 1u) << k;
        out[m] += probabilities[i];
    }
    return out;
}

} // namespace qlab::viz::math
