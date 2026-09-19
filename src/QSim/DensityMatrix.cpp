#include "QSim/DensityMatrix.hpp"
#include "Core/Log.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Sampling.hpp"
#include "Numerics/Tensor.hpp"
#include "QSim/StateVector.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qsim {
namespace {
struct SiteIter { // enumerate base indices with the given sites' digits zero
    std::vector<std::uint32_t> dims;
    std::vector<std::size_t> strides;
    std::vector<bool> isTarget;
    std::size_t D;
    template <class F> void each(F&& f) const {
        std::vector<std::uint32_t> digit(dims.size(), 0);
        for (std::size_t i = 0; i < D; ++i) {
            bool base = true;
            std::size_t r = i;
            for (std::size_t s = 0; s < dims.size(); ++s) {
                digit[s] = r % dims[s];
                r /= dims[s];
                if (isTarget[s] && digit[s]) {
                    base = false;
                    break;
                }
            }
            if (base)
                f(i);
        }
    }
};
} // namespace

std::uint32_t DensityMatrixBackend::maxQubits() {
    return std::min<std::uint32_t>(maxQubitsFor(2 * sizeof(Complex), true), 13);
}

Capabilities DensityMatrixBackend::capabilities() const {
    return {Kind::DensityMatrix, maxQubits(), true, false, true, true, true, false, true};
}

Status DensityMatrixBackend::allocate(std::uint32_t n, std::uint32_t levels) {
    if (levels < 2 || levels > 3)
        return fail(err::Unsupported, "density-matrix sites support 2 or 3 levels");
    if (n >
        13) // refuse before building the per-site list: 2^13 is the dimension cap of spec 07 §3.1
        return fail(
            err::TooLarge,
            std::format(
                "{} sites of {} levels need {:.3g} GiB; the density-matrix cap is 13 qubits", n,
                levels, 32.0 * std::pow(double(levels), 2.0 * double(n)) / double(1ull << 30)));
    std::vector<std::uint32_t> d(n, levels);
    return allocateMixed(d);
}

Status DensityMatrixBackend::allocateMixed(std::span<const std::uint32_t> dims) {
    // Spec 07 §3.1: D ≤ 2^13 (13 qubits, or 8 sites at d = 3) and two buffers within 80 % of free
    // RAM. The dimension is accumulated in floating point so that a huge request cannot wrap
    // around.
    constexpr double kMaxDim = 8192.0;
    double dimD = 1.0;
    for (auto d : dims) {
        if (d < 2 || d > 3)
            return fail(err::Unsupported, "site dimension must be 2 or 3");
        dimD *= double(d);
    }
    const double bytes = 2.0 * 16.0 * dimD * dimD;
    const double budget = 0.8 * double(availableMemoryBytes());
    if (dimD > kMaxDim || bytes > budget)
        return fail(err::TooLarge, std::format("density matrix of dimension {:.4g} ({} sites) "
                                               "needs {:.3g} GiB; the cap is dimension 8192 "
                                               "(13 qubits) within {:.3g} GiB of usable memory",
                                               dimD, dims.size(), bytes / double(1ull << 30),
                                               budget / double(1ull << 30)));
    const std::size_t D = static_cast<std::size_t>(dimD);
    dims_.assign(dims.begin(), dims.end());
    n_ = static_cast<std::uint32_t>(dims.size());
    levels_ = dims.empty() ? 2 : *std::max_element(dims.begin(), dims.end());
    strides_.resize(n_);
    std::size_t s = 1;
    for (std::uint32_t i = 0; i < n_; ++i) {
        strides_[i] = s;
        s *= dims_[i];
    }
    D_ = D;
    rho_ = Matrix(D, D);
    rho_(0, 0) = 1.0;
    scratch_ = Matrix(D, D);
    allocated_ = true;
    ops_ = 0;
    QXL_LOG_INFO(Sim, "DensityMatrix allocated: n={} D={} bytes={}", n_, D_, bytesAllocated());
    return {};
}

Status DensityMatrixBackend::setRho(const Matrix& rho) {
    if (rho.rows != D_ || rho.cols != D_)
        return fail(err::BadTargets, "dimension mismatch");
    rho_ = rho;
    return {};
}
Status DensityMatrixBackend::setPure(std::span<const Complex> psi) {
    if (psi.size() != D_)
        return fail(err::BadTargets, "dimension mismatch");
    for (std::size_t i = 0; i < D_; ++i)
        for (std::size_t j = 0; j < D_; ++j)
            rho_(i, j) = psi[i] * std::conj(psi[j]);
    return {};
}
double DensityMatrixBackend::stateNorm() const {
    return num::trace(rho_).real();
}
double DensityMatrixBackend::purity() const {
    return num::purity(rho_);
}
std::unique_ptr<IBackend> DensityMatrixBackend::clone() const {
    return std::make_unique<DensityMatrixBackend>(*this);
}

Matrix DensityMatrixBackend::expandToSites(const Matrix& u,
                                           std::span<const std::uint32_t> siteDims) {
    const std::size_t k = siteDims.size();
    std::size_t m = 1;
    for (auto d : siteDims)
        m *= d;
    if (m == u.rows)
        return u;
    Matrix out = Matrix::identity(m);
    auto qubitIndex = [&](std::size_t full, std::size_t& qi) -> bool {
        qi = 0;
        std::size_t r = full;
        for (std::size_t s = 0; s < k; ++s) {
            std::size_t dg = r % siteDims[s];
            r /= siteDims[s];
            if (dg > 1)
                return false;
            qi |= dg << s;
        }
        return true;
    };
    for (std::size_t a = 0; a < m; ++a) {
        std::size_t qa;
        if (!qubitIndex(a, qa))
            continue;
        for (std::size_t b = 0; b < m; ++b) {
            std::size_t qb;
            if (!qubitIndex(b, qb)) {
                out(a, b) = 0.0;
                continue;
            }
            out(a, b) = u(qa, qb);
        }
    }
    return out;
}

void DensityMatrixBackend::applyLeft(Matrix& rho, std::span<const std::uint32_t> dims,
                                     std::span<const std::uint32_t> sites, const Matrix& op,
                                     std::span<const std::size_t> ctrl) {
    const std::size_t D = rho.rows, k = sites.size();
    std::vector<std::size_t> strides(dims.size());
    std::size_t s = 1;
    for (std::size_t i = 0; i < dims.size(); ++i) {
        strides[i] = s;
        s *= dims[i];
    }
    std::vector<std::size_t> off; // offsets of the m local states
    std::size_t m = 1;
    for (auto st : sites)
        m *= dims[st];
    off.resize(m);
    for (std::size_t l = 0; l < m; ++l) {
        std::size_t o = 0, r = l;
        for (std::size_t j = 0; j < k; ++j) {
            o += (r % dims[sites[j]]) * strides[sites[j]];
            r /= dims[sites[j]];
        }
        off[l] = o;
    }
    SiteIter it{std::vector<std::uint32_t>(dims.begin(), dims.end()), strides,
                std::vector<bool>(dims.size(), false), D};
    for (auto st : sites)
        it.isTarget[st] = true;
    std::vector<Complex> v(m), r(m);
    it.each([&](std::size_t base) {
        for (auto c : ctrl)
            if (((base / strides[c]) % dims[c]) != 1)
                return;
        for (std::size_t col = 0; col < D; ++col) {
            for (std::size_t l = 0; l < m; ++l)
                v[l] = rho(base + off[l], col);
            for (std::size_t a = 0; a < m; ++a) {
                Complex acc{};
                for (std::size_t l = 0; l < m; ++l)
                    acc += op(a, l) * v[l];
                r[a] = acc;
            }
            for (std::size_t l = 0; l < m; ++l)
                rho(base + off[l], col) = r[l];
        }
    });
}

void DensityMatrixBackend::applyRightAdjoint(Matrix& rho, std::span<const std::uint32_t> dims,
                                             std::span<const std::uint32_t> sites, const Matrix& op,
                                             std::span<const std::size_t> ctrl) {
    const std::size_t D = rho.rows, k = sites.size();
    std::vector<std::size_t> strides(dims.size());
    std::size_t s = 1;
    for (std::size_t i = 0; i < dims.size(); ++i) {
        strides[i] = s;
        s *= dims[i];
    }
    std::size_t m = 1;
    for (auto st : sites)
        m *= dims[st];
    std::vector<std::size_t> off(m);
    for (std::size_t l = 0; l < m; ++l) {
        std::size_t o = 0, r = l;
        for (std::size_t j = 0; j < k; ++j) {
            o += (r % dims[sites[j]]) * strides[sites[j]];
            r /= dims[sites[j]];
        }
        off[l] = o;
    }
    SiteIter it{std::vector<std::uint32_t>(dims.begin(), dims.end()), strides,
                std::vector<bool>(dims.size(), false), D};
    for (auto st : sites)
        it.isTarget[st] = true;
    std::vector<Complex> v(m), r(m);
    it.each([&](std::size_t base) {
        for (auto c : ctrl)
            if (((base / strides[c]) % dims[c]) != 1)
                return;
        for (std::size_t row = 0; row < D; ++row) {
            for (std::size_t l = 0; l < m; ++l)
                v[l] = rho(row, base + off[l]);
            // (ρ O†)_{row, b} = Σ_l ρ_{row,l} conj(O_{b,l})
            for (std::size_t b = 0; b < m; ++b) {
                Complex acc{};
                for (std::size_t l = 0; l < m; ++l)
                    acc += v[l] * std::conj(op(b, l));
                r[b] = acc;
            }
            for (std::size_t l = 0; l < m; ++l)
                rho(row, base + off[l]) = r[l];
        }
    });
}
} // namespace qlab::qsim
