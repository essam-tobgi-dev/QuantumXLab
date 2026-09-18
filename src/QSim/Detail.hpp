#pragma once
// Internal helpers shared by the QSim backends; not part of the module's public API.
#include "QSim/Types.hpp"
#include <algorithm>
#include <bit>
#include <span>
#include <vector>

namespace qlab::qsim::detail {

// P|j⟩ = c_j |j ⊕ x⟩ with c_j = phase · i^{n_Y} · (−1)^{popcount(j & z)} (T11 (2.3), Y = iXZ). Valid for
// strings of at most 64 qubits (the amplitude-based backends).
inline Complex pauliCoefficient(const PauliString& p, std::uint64_t j) {
    static constexpr Complex kPowI[4] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    const Complex c = kPowI[std::popcount(p.xMask() & p.zMask()) & 3] * p.phase();
    return (std::popcount(j & p.zMask()) & 1) ? -c : c;
}

// Re-index a reduced state computed over `sortedSites` (ascending; sortedSites[0] least significant)
// so that requestedSites[0] is the least significant index, as for probabilities(). `sortedDims[k]`
// is the local dimension of sortedSites[k]; requestedSites is a permutation of sortedSites.
inline Matrix reorderSites(const Matrix& rho, std::span<const std::size_t> sortedSites,
                           std::span<const std::size_t> sortedDims, std::span<const std::size_t> requestedSites) {
    const std::size_t k = sortedSites.size();
    std::vector<std::size_t> pos(k), sortedStride(k);
    for (std::size_t r = 0; r < k; ++r)
        pos[r] = static_cast<std::size_t>(std::find(sortedSites.begin(), sortedSites.end(), requestedSites[r]) - sortedSites.begin());
    for (std::size_t i = 0, s = 1; i < k; s *= sortedDims[i], ++i) sortedStride[i] = s;
    const std::size_t dim = rho.rows;
    std::vector<std::size_t> map(dim); // requested-order index → sorted-order index
    for (std::size_t a = 0; a < dim; ++a) {
        std::size_t rest = a, idx = 0;
        for (std::size_t r = 0; r < k; ++r) {
            const std::size_t d = sortedDims[pos[r]];
            idx += (rest % d) * sortedStride[pos[r]];
            rest /= d;
        }
        map[a] = idx;
    }
    Matrix out(dim, dim);
    for (std::size_t a = 0; a < dim; ++a)
        for (std::size_t b = 0; b < dim; ++b) out(a, b) = rho(map[a], map[b]);
    return out;
}

} // namespace qlab::qsim::detail
