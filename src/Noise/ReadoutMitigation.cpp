// T10 §7.2 — readout-error mitigation: min ‖Mᵀp − q‖₂ subject to p ≥ 0, Σp = 1 (spec 08 §3).
// The assignment map is stored factorised (one factor per qubit or correlated group), so both M
// and M^{-1} act factor by factor; only the small factors are ever inverted densely.
#include "Noise/Readout.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace qlab::noise {
namespace {

num::RealMatrix transposeOf(const num::RealMatrix& m) {
    num::RealMatrix t(m.cols, m.rows);
    for (std::size_t i = 0; i < m.rows; ++i)
        for (std::size_t j = 0; j < m.cols; ++j) t(j, i) = m(i, j);
    return t;
}

// Gauss-Jordan with partial pivoting; false when the factor is singular (mitigation then has no
// unconstrained estimate, T10 §7.2 notes the variance blow-up that precedes it).
bool invertInPlace(num::RealMatrix& m) {
    const std::size_t n = m.rows;
    num::RealMatrix inv = num::RealMatrix::identity(n);
    for (std::size_t c = 0; c < n; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < n; ++r)
            if (std::abs(m(r, c)) > std::abs(m(pivot, c))) pivot = r;
        if (std::abs(m(pivot, c)) < 1e-14) return false;
        if (pivot != c)
            for (std::size_t j = 0; j < n; ++j) { std::swap(m(c, j), m(pivot, j)); std::swap(inv(c, j), inv(pivot, j)); }
        const double d = m(c, c);
        for (std::size_t j = 0; j < n; ++j) { m(c, j) /= d; inv(c, j) /= d; }
        for (std::size_t r = 0; r < n; ++r) {
            if (r == c) continue;
            const double f = m(r, c);
            if (f == 0.0) continue;
            for (std::size_t j = 0; j < n; ++j) { m(r, j) -= f * m(c, j); inv(r, j) -= f * inv(c, j); }
        }
    }
    m = std::move(inv);
    return true;
}

// Euclidean projection onto {p ≥ 0, Σp = 1} (Duchi et al. 2008) — the feasible set of (7.2).
void projectSimplex(std::vector<double>& v) {
    const std::size_t n = v.size();
    if (n == 0) return;
    std::vector<double> u(v);
    std::sort(u.begin(), u.end(), std::greater<double>());
    double cumulative = 0.0, theta = 0.0;
    std::size_t rho = 0;
    for (std::size_t j = 0; j < n; ++j) {
        cumulative += u[j];
        const double t = (cumulative - 1.0) / static_cast<double>(j + 1);
        if (u[j] - t > 0.0) { rho = j + 1; theta = t; }
    }
    if (rho == 0) theta = (std::accumulate(v.begin(), v.end(), 0.0) - 1.0) / static_cast<double>(n);
    for (auto& x : v) x = std::max(0.0, x - theta);
}

double norm2(std::span<const double> a, std::span<const double> b) {
    double s = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) s += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(s);
}
} // namespace

Result<std::vector<double>> probabilitiesFromCounts(const qsim::Counts& counts, std::size_t nBits) {
    if (nBits > 24) return fail(err::InvalidParameter, std::format("count histograms are limited to 24 bits, {} requested", nBits));
    std::vector<double> p(std::size_t{1} << nBits, 0.0);
    std::uint64_t total = 0;
    for (const auto& [key, n] : counts) {
        if (key.size() != nBits)
            return fail(err::BadReadout, std::format("count key '{}' has {} bits, {} expected", key, key.size(), nBits));
        std::size_t index = 0;
        for (std::size_t k = 0; k < nBits; ++k) { // MSB-first key, little-endian index
            const char ch = key[nBits - 1 - k];
            if (ch != '0' && ch != '1') return fail(err::BadReadout, std::format("count key '{}' is not binary", key));
            if (ch == '1') index |= std::size_t{1} << k;
        }
        p[index] += static_cast<double>(n);
        total += n;
    }
    if (total == 0) return fail(err::InvalidParameter, "count histogram is empty");
    for (auto& x : p) x /= static_cast<double>(total);
    return p;
}

Result<Mitigation> ReadoutModel::mitigate(std::span<const double> measured) const {
    QXL_TRY(checkLength(measured.size()));
    Mitigation out;
    const std::vector<double> q(measured.begin(), measured.end());

    // M^{-T} q: invert each factor's transpose and apply it along that factor (T10 §7.2 linear inversion).
    bool invertible = true;
    std::vector<num::RealMatrix> inverseT, forward, forwardT;
    for (const auto& f : factors_) {
        num::RealMatrix mt = transposeOf(f.matrix);
        forward.push_back(f.matrix);
        forwardT.push_back(mt);
        if (!invertInPlace(mt)) invertible = false;
        inverseT.push_back(std::move(mt));
    }
    if (invertible) {
        out.unconstrained = q;
        for (std::size_t n = 0; n < factors_.size(); ++n) applyAlong(out.unconstrained, factors_[n], inverseT[n]);
    }

    auto misfit = [&](const std::vector<double>& p) { // Mᵀp − q
        std::vector<double> r = p;
        for (std::size_t n = 0; n < factors_.size(); ++n) applyAlong(r, factors_[n], forwardT[n]);
        for (std::size_t i = 0; i < r.size(); ++i) r[i] -= q[i];
        return r;
    };
    auto norm = [](const std::vector<double>& v) {
        double s = 0.0;
        for (double x : v) s += x * x;
        return std::sqrt(s);
    };
    // A feasible unconstrained estimate is already the constrained optimum (zero residual).
    if (invertible && std::all_of(out.unconstrained.begin(), out.unconstrained.end(), [](double x) { return x >= -1e-12; })) {
        out.probabilities = out.unconstrained;
        for (auto& x : out.probabilities) x = std::max(0.0, x);
        out.residual = norm(misfit(out.probabilities));
        return out;
    }

    // FISTA on f(p) = ‖Mᵀp − q‖² over the simplex with the fixed step 1/L, where
    // L = 2‖M‖₂² ≤ 2 Π_factors ‖M_f‖₁‖M_f‖∞ bounds the Lipschitz constant of ∇f = 2M(Mᵀp − q).
    double lipschitz = 2.0;
    for (const auto& f : factors_) {
        double rowMax = 0.0, colMax = 0.0;
        for (std::size_t i = 0; i < f.matrix.rows; ++i) {
            double row = 0.0, col = 0.0;
            for (std::size_t j = 0; j < f.matrix.cols; ++j) { row += std::abs(f.matrix(i, j)); col += std::abs(f.matrix(j, i)); }
            rowMax = std::max(rowMax, row);
            colMax = std::max(colMax, col);
        }
        lipschitz *= rowMax * colMax;
    }
    std::vector<double> x = invertible ? out.unconstrained : q;
    projectSimplex(x);
    std::vector<double> y = x;
    double t = 1.0;
    out.converged = false;
    for (std::size_t it = 0; it < 5000 && !out.converged; ++it) {
        std::vector<double> g = misfit(y);
        for (std::size_t n = 0; n < factors_.size(); ++n) applyAlong(g, factors_[n], forward[n]);
        std::vector<double> next(y.size());
        for (std::size_t i = 0; i < y.size(); ++i) next[i] = y[i] - 2.0 * g[i] / lipschitz;
        projectSimplex(next);
        const double tNext = 0.5 * (1.0 + std::sqrt(1.0 + 4.0 * t * t));
        for (std::size_t i = 0; i < y.size(); ++i) y[i] = next[i] + (t - 1.0) / tNext * (next[i] - x[i]);
        out.converged = norm2(next, x) <= 1e-13;
        x = std::move(next);
        t = tNext;
        ++out.iterations;
    }
    out.probabilities = std::move(x);
    out.residual = norm(misfit(out.probabilities));
    return out;
}

Result<Mitigation> ReadoutModel::mitigateCounts(const qsim::Counts& measured) const {
    QXL_TRY_ASSIGN(auto q, probabilitiesFromCounts(measured, qubits_.size()));
    return mitigate(q);
}

} // namespace qlab::noise
