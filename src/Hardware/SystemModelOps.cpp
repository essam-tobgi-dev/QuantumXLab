#include "Hardware/SystemModel.hpp"

namespace qlab::hw {
using num::Complex;
using num::SparseMatrix;
using num::Triplet;

namespace {
// Strides for a little-endian site layout: site 0 has stride 1.
std::vector<std::size_t> strides(std::span<const std::uint32_t> dims) {
    std::vector<std::size_t> s(dims.size(), 1);
    for (std::size_t k = 1; k < dims.size(); ++k)
        s[k] = s[k - 1] * dims[k - 1];
    return s;
}
std::size_t totalDim(std::span<const std::uint32_t> dims) {
    std::size_t d = 1;
    for (auto x : dims)
        d *= x;
    return d;
}
} // namespace

std::size_t SystemModelSpec::dimension() const {
    return totalDim(siteDims);
}

SparseMatrix siteOperator(std::span<const std::uint32_t> dims, std::uint32_t site,
                          num::ConstMatrixView local) {
    const std::size_t D = totalDim(dims);
    const auto st = strides(dims);
    const std::size_t d = dims[site], stride = st[site];
    const std::size_t outer = D / (d * stride);
    std::vector<Triplet> t;
    t.reserve(D * d);
    // index = low + stride*(level) + d*stride*(high)
    for (std::size_t high = 0; high < outer; ++high)
        for (std::size_t low = 0; low < stride; ++low)
            for (std::size_t r = 0; r < d; ++r)
                for (std::size_t c = 0; c < d; ++c) {
                    Complex v = local(r, c);
                    if (v == Complex(0.0, 0.0))
                        continue;
                    t.push_back({low + stride * r + d * stride * high,
                                 low + stride * c + d * stride * high, v});
                }
    return SparseMatrix::fromTriplets(D, D, std::move(t));
}

SparseMatrix siteAnnihilate(std::span<const std::uint32_t> dims, std::uint32_t site) {
    const std::size_t d = dims[site];
    num::Matrix a(d, d);
    for (std::size_t n = 1; n < d; ++n)
        a(n - 1, n) = std::sqrt(static_cast<double>(n));
    return siteOperator(dims, site, a);
}

SparseMatrix siteNumber(std::span<const std::uint32_t> dims, std::uint32_t site) {
    const std::size_t d = dims[site];
    num::Matrix n(d, d);
    for (std::size_t k = 0; k < d; ++k)
        n(k, k) = static_cast<double>(k);
    return siteOperator(dims, site, n);
}

SparseMatrix siteProjector(std::span<const std::uint32_t> dims, std::uint32_t site,
                           std::uint32_t level) {
    const std::size_t d = dims[site];
    num::Matrix p(d, d);
    if (level < d)
        p(level, level) = 1.0;
    return siteOperator(dims, site, p);
}

} // namespace qlab::hw
