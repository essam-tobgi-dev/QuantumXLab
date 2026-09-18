#include "Instruments/Discriminator.hpp"
#include "Data/Gmm.hpp"
#include "Data/Histogram.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace qlab::instr {
namespace {
struct Cov { double xx, xy, yy; };

double mahalanobis2(double dx, double dy, const Cov& c) {
    const double det = std::max(c.xx * c.yy - c.xy * c.xy, 1e-300);
    return (c.yy * dx * dx - 2.0 * c.xy * dx * dy + c.xx * dy * dy) / det;
}

// EM for K Gaussians with one shared covariance, seeded by the labelled means (states: 3).
bool emShared(std::span<const double> x, std::span<const double> y, std::vector<std::array<double, 2>>& mu, Cov& cov,
              int& iterations) {
    const std::size_t n = x.size(), K = mu.size();
    std::vector<double> weight(K, 1.0 / static_cast<double>(K)), resp(n * K);
    double previous = -std::numeric_limits<double>::infinity();
    for (iterations = 0; iterations < 300; ++iterations) {
        const double det = std::max(cov.xx * cov.yy - cov.xy * cov.xy, 1e-300);
        double logLik = 0.0;
        for (std::size_t i = 0; i < n; ++i) { // E step (the common normalisation 2π√det cancels in the ratios)
            double total = 0.0;
            for (std::size_t k = 0; k < K; ++k) {
                resp[i * K + k] = weight[k] * std::exp(-0.5 * mahalanobis2(x[i] - mu[k][0], y[i] - mu[k][1], cov));
                total += resp[i * K + k];
            }
            if (total <= 0.0) { for (std::size_t k = 0; k < K; ++k) resp[i * K + k] = 1.0 / static_cast<double>(K); total = 1.0; }
            else for (std::size_t k = 0; k < K; ++k) resp[i * K + k] /= total;
            logLik += std::log(std::max(total, 1e-300)) - 0.5 * std::log(det);
        }
        Cov next{0.0, 0.0, 0.0}; // M step
        for (std::size_t k = 0; k < K; ++k) {
            double w = 0.0, sx = 0.0, sy = 0.0;
            for (std::size_t i = 0; i < n; ++i) { w += resp[i * K + k]; sx += resp[i * K + k] * x[i]; sy += resp[i * K + k] * y[i]; }
            w = std::max(w, 1e-12);
            weight[k] = w / static_cast<double>(n);
            mu[k] = {sx / w, sy / w};
            for (std::size_t i = 0; i < n; ++i) {
                const double dx = x[i] - mu[k][0], dy = y[i] - mu[k][1], r = resp[i * K + k];
                next.xx += r * dx * dx; next.xy += r * dx * dy; next.yy += r * dy * dy;
            }
        }
        cov = {next.xx / static_cast<double>(n) + 1e-12, next.xy / static_cast<double>(n), next.yy / static_cast<double>(n) + 1e-12};
        if (std::abs(logLik - previous) < 1e-9 * std::abs(logLik)) return true;
        previous = logLik;
    }
    return false;
}
} // namespace

int Discriminator::classify(double i, double q) const {
    int best = 0;
    double bestD = std::numeric_limits<double>::infinity();
    for (std::size_t s = 0; s < means.size(); ++s) {
        const double d = mahalanobis2(i - means[s][0], q - means[s][1], {sxx, sxy, syy});
        if (d < bestD) { bestD = d; best = static_cast<int>(s); }
    }
    return best;
}

double Discriminator::project(double i, double q) const {
    if (means.size() < 2) return 0.0;
    const double det = std::max(sxx * syy - sxy * sxy, 1e-300);
    const double dx = means[1][0] - means[0][0], dy = means[1][1] - means[0][1];
    const double wx = (syy * dx - sxy * dy) / det, wy = (-sxy * dx + sxx * dy) / det; // w = Σ⁻¹(μ1 − μ0)
    const double norm = std::sqrt(std::max(wx * dx + wy * dy, 1e-300));               // √(wᵀΣw) = √(wᵀΔμ)
    return (wx * (i - 0.5 * (means[0][0] + means[1][0])) + wy * (q - 0.5 * (means[0][1] + means[1][1]))) / norm;
}

double AssignmentEstimate::fidelity() const {
    return matrix.size() >= 2 ? 1.0 - 0.5 * (matrix[0][1] + matrix[1][0]) : 1.0;
}

Result<Discriminator> trainDiscriminator(std::span<const IqPoint> labelled, int states) {
    if (states != 2 && states != 3) return fail(err::BadInput, std::format("discriminator: states must be 2 or 3, got {}", states));
    const std::size_t n = labelled.size();
    const auto K = static_cast<std::size_t>(states);
    std::vector<std::size_t> count(K, 0);
    double cx = 0.0, cy = 0.0;
    for (auto const& p : labelled) {
        if (p.prepared < 0 || p.prepared >= states)
            return fail(err::BadInput, std::format("discriminator: a training shot carries the label {}", static_cast<int>(p.prepared)));
        ++count[static_cast<std::size_t>(p.prepared)];
        cx += p.i / static_cast<double>(n);
        cy += p.q / static_cast<double>(n);
    }
    for (std::size_t s = 0; s < K; ++s)
        if (count[s] < 2) return fail(err::BadInput, std::format("discriminator: no training shots prepared in |{}>", s));
    // Work in centred, unit-rms coordinates: the fit is then independent of the ADC's volt scale.
    double spread = 0.0;
    for (auto const& p : labelled) spread += ((p.i - cx) * (p.i - cx) + (p.q - cy) * (p.q - cy)) / static_cast<double>(n);
    spread = std::sqrt(std::max(spread, 1e-300));
    std::vector<double> x(n), y(n);
    std::vector<int> labels(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = (labelled[i].i - cx) / spread;
        y[i] = (labelled[i].q - cy) / spread;
        labels[i] = labelled[i].prepared;
    }
    Discriminator d;
    d.states = states;
    std::vector<std::array<double, 2>> mu(K);
    Cov cov{1.0, 0.0, 1.0};
    if (states == 2) {
        data::fit::Gmm2Options opt;
        opt.sharedCovariance = true;
        opt.labels = labels;
        auto g = data::fit::fitGmm2(x, y, opt);
        if (!g) return fail(err::FitFailed, "discriminator: " + g.error().message);
        // fitGmm2 orders its components by I; the prepared labels say which one is |1⟩.
        std::size_t agree = 0;
        for (std::size_t i = 0; i < n; ++i) agree += g->classify(x[i], y[i]) == labels[i] ? 1u : 0u;
        const bool swapped = 2 * agree < n;
        mu[0] = {g->comp[swapped ? 1 : 0].mx, g->comp[swapped ? 1 : 0].my};
        mu[1] = {g->comp[swapped ? 0 : 1].mx, g->comp[swapped ? 0 : 1].my};
        cov = {g->comp[0].sxx, g->comp[0].sxy, g->comp[0].syy};
        d.iterations = g->iterations;
        d.converged = g->converged;
    } else {
        for (std::size_t s = 0; s < K; ++s) mu[s] = {0.0, 0.0};
        for (std::size_t i = 0; i < n; ++i) {
            const auto s = static_cast<std::size_t>(labels[i]);
            mu[s][0] += x[i] / static_cast<double>(count[s]);
            mu[s][1] += y[i] / static_cast<double>(count[s]);
        }
        cov = {0.05, 0.0, 0.05};
        d.converged = emShared(x, y, mu, cov, d.iterations);
    }
    for (std::size_t s = 0; s < K; ++s) d.means.push_back({cx + spread * mu[s][0], cy + spread * mu[s][1]});
    d.sxx = cov.xx * spread * spread;
    d.sxy = cov.xy * spread * spread;
    d.syy = cov.yy * spread * spread;
    d.snr = std::sqrt(mahalanobis2(d.means[1][0] - d.means[0][0], d.means[1][1] - d.means[0][1], {d.sxx, d.sxy, d.syy}));
    return d;
}

AssignmentEstimate estimateAssignment(std::span<const IqPoint> labelled, const Discriminator& d) {
    AssignmentEstimate a;
    a.states = d.states;
    const auto K = static_cast<std::size_t>(d.states);
    std::vector<std::vector<std::uint64_t>> counts(K, std::vector<std::uint64_t>(K, 0));
    a.shots.assign(K, 0);
    for (auto const& p : labelled) {
        if (p.prepared < 0 || p.prepared >= d.states) continue;
        ++counts[static_cast<std::size_t>(p.prepared)][static_cast<std::size_t>(d.classify(p.i, p.q))];
        ++a.shots[static_cast<std::size_t>(p.prepared)];
    }
    a.matrix.assign(K, std::vector<double>(K, 0.0));
    a.sigma.assign(K, std::vector<double>(K, 0.0));
    for (std::size_t i = 0; i < K; ++i)
        for (std::size_t j = 0; j < K; ++j) {
            if (a.shots[i] == 0) { a.matrix[i][j] = i == j ? 1.0 : 0.0; continue; }
            a.matrix[i][j] = static_cast<double>(counts[i][j]) / static_cast<double>(a.shots[i]);
            const data::Interval w = data::wilson(counts[i][j], a.shots[i], 1.0);
            a.sigma[i][j] = 0.5 * (w.hi - w.lo);
        }
    return a;
}

} // namespace qlab::instr
