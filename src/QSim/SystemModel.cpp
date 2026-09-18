#include "QSim/SystemModel.hpp"
#include <cmath>

namespace qlab::qsim {
namespace {
// Stride of `site` in the flat index (little-endian: site 0 is the least significant factor).
std::size_t strideOf(std::span<const std::uint32_t> dims, std::uint32_t site) {
    std::size_t s = 1;
    for (std::uint32_t k = 0; k < site; ++k) s *= dims[k];
    return s;
}
std::size_t totalDim(std::span<const std::uint32_t> dims) {
    std::size_t d = 1;
    for (auto x : dims) d *= x;
    return d;
}
} // namespace

Matrix ladderAnnihilate(std::span<const std::uint32_t> dims, std::uint32_t site) {
    const std::size_t D = totalDim(dims);
    Matrix a(D, D);
    if (site >= dims.size()) return a;
    const std::size_t stride = strideOf(dims, site);
    const std::uint32_t d = dims[site];
    for (std::size_t idx = 0; idx < D; ++idx) {
        std::uint32_t level = static_cast<std::uint32_t>((idx / stride) % d);
        if (level == 0) continue;
        // a |n> = √n |n-1>: column idx (level n) maps to row idx - stride (level n-1).
        a(idx - stride, idx) = std::sqrt(static_cast<double>(level));
    }
    return a;
}

Matrix numberOperator(std::span<const std::uint32_t> dims, std::uint32_t site) {
    const std::size_t D = totalDim(dims);
    Matrix n(D, D);
    if (site >= dims.size()) return n;
    const std::size_t stride = strideOf(dims, site);
    const std::uint32_t d = dims[site];
    for (std::size_t idx = 0; idx < D; ++idx)
        n(idx, idx) = static_cast<double>((idx / stride) % d);
    return n;
}

Matrix levelProjector(std::span<const std::uint32_t> dims, std::uint32_t site, std::uint32_t level) {
    const std::size_t D = totalDim(dims);
    Matrix p(D, D);
    if (site >= dims.size()) return p;
    const std::size_t stride = strideOf(dims, site);
    const std::uint32_t d = dims[site];
    for (std::size_t idx = 0; idx < D; ++idx)
        if (static_cast<std::uint32_t>((idx / stride) % d) == level) p(idx, idx) = 1.0;
    return p;
}

std::pair<Matrix, Matrix> driveFromLadder(std::span<const std::uint32_t> dims, std::uint32_t site) {
    Matrix a = ladderAnnihilate(dims, site);
    const std::size_t D = a.rows;
    Matrix A(D, D), B(D, D);
    for (std::size_t i = 0; i < D; ++i)
        for (std::size_t j = 0; j < D; ++j) {
            Complex adag = std::conj(a(j, i));
            A(i, j) = a(i, j) + adag;                           // a + a† (σx on the qubit levels)
            B(i, j) = Complex(0, 1) * (adag - a(i, j));         // i(a† − a) (σy), the Ω_y operator of T05 (7.1)
        }
    return {std::move(A), std::move(B)};
}

} // namespace qlab::qsim
