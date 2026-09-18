#include "Data/Gmm.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace qlab::data::fit {

double Gaussian2::pdf(double x, double y) const {
    double det = sxx * syy - sxy * sxy;
    if (det <= 0) return 0.0;
    double dx = x - mx, dy = y - my;
    double q = (syy * dx * dx - 2 * sxy * dx * dy + sxx * dy * dy) / det;
    return std::exp(-0.5 * q) / (2.0 * std::numbers::pi * std::sqrt(det));
}

Result<Gmm2Result> fitGmm2(std::span<const double> x, std::span<const double> y, const Gmm2Options& opt) {
    const std::size_t n = x.size();
    if (n < 4 || y.size() != n) return fail(ErrorCode::Data_ + 20, "gmm2 needs >= 4 (x,y) points");
    Gmm2Result r;
    // Initialization: labels if given, otherwise split along the principal axis of the cloud.
    std::vector<double> resp(n, 0.5);
    double mx = 0, my = 0;
    for (std::size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
    mx /= static_cast<double>(n); my /= static_cast<double>(n);
    if (!opt.labels.empty() && opt.labels.size() == n) {
        for (std::size_t i = 0; i < n; ++i) resp[i] = opt.labels[i] ? 1.0 : 0.0;
    } else {
        double cxx = 0, cxy = 0, cyy = 0;
        for (std::size_t i = 0; i < n; ++i) { double dx = x[i] - mx, dy = y[i] - my; cxx += dx * dx; cxy += dx * dy; cyy += dy * dy; }
        // principal eigenvector of [[cxx,cxy],[cxy,cyy]]
        double tr = cxx + cyy, det = cxx * cyy - cxy * cxy;
        double l1 = tr / 2 + std::sqrt(std::max(0.0, tr * tr / 4 - det));
        double vx = cxy, vy = l1 - cxx;
        if (std::abs(vx) + std::abs(vy) < 1e-300) { vx = 1; vy = 0; }
        for (std::size_t i = 0; i < n; ++i) resp[i] = ((x[i] - mx) * vx + (y[i] - my) * vy) > 0 ? 1.0 : 0.0;
    }
    double prevLL = -std::numeric_limits<double>::infinity();
    for (int it = 0; it < opt.maxIterations; ++it) {
        // M step
        for (int k = 0; k < 2; ++k) {
            double w = 0, sx = 0, sy = 0;
            for (std::size_t i = 0; i < n; ++i) { double g = k ? resp[i] : 1 - resp[i]; w += g; sx += g * x[i]; sy += g * y[i]; }
            if (w < 1e-12) w = 1e-12;
            auto& c = r.comp[static_cast<std::size_t>(k)];
            c.weight = w / static_cast<double>(n); c.mx = sx / w; c.my = sy / w;
            double sxx = 0, sxy = 0, syy = 0;
            for (std::size_t i = 0; i < n; ++i) {
                double g = k ? resp[i] : 1 - resp[i]; double dx = x[i] - c.mx, dy = y[i] - c.my;
                sxx += g * dx * dx; sxy += g * dx * dy; syy += g * dy * dy;
            }
            c.sxx = sxx / w + 1e-12; c.sxy = sxy / w; c.syy = syy / w + 1e-12;
        }
        if (opt.sharedCovariance) {
            double w0 = r.comp[0].weight, w1 = r.comp[1].weight;
            double sxx = w0 * r.comp[0].sxx + w1 * r.comp[1].sxx, sxy = w0 * r.comp[0].sxy + w1 * r.comp[1].sxy, syy = w0 * r.comp[0].syy + w1 * r.comp[1].syy;
            for (auto& c : r.comp) { c.sxx = sxx; c.sxy = sxy; c.syy = syy; }
        }
        // E step
        double ll = 0;
        for (std::size_t i = 0; i < n; ++i) {
            double p0 = r.comp[0].weight * r.comp[0].pdf(x[i], y[i]);
            double p1 = r.comp[1].weight * r.comp[1].pdf(x[i], y[i]);
            double s = p0 + p1;
            resp[i] = s > 0 ? p1 / s : 0.5;
            ll += std::log(std::max(s, 1e-300));
        }
        r.iterations = it + 1; r.logLikelihood = ll;
        if (std::abs(ll - prevLL) < opt.tol * std::abs(ll)) { r.converged = true; break; }
        prevLL = ll;
    }
    // Order components: component 0 has the smaller projection on the mean-difference axis.
    if (r.comp[0].mx > r.comp[1].mx) std::swap(r.comp[0], r.comp[1]);
    // Fisher LDA: w = Σ^{-1} (μ1 - μ0) with pooled covariance.
    double sxx = (r.comp[0].sxx + r.comp[1].sxx) / 2, sxy = (r.comp[0].sxy + r.comp[1].sxy) / 2, syy = (r.comp[0].syy + r.comp[1].syy) / 2;
    double det = sxx * syy - sxy * sxy;
    double dmx = r.comp[1].mx - r.comp[0].mx, dmy = r.comp[1].my - r.comp[0].my;
    if (det <= 1e-300) det = 1e-300;
    r.wx = (syy * dmx - sxy * dmy) / det;
    r.wy = (-sxy * dmx + sxx * dmy) / det;
    double cx = (r.comp[0].mx + r.comp[1].mx) / 2, cy = (r.comp[0].my + r.comp[1].my) / 2;
    r.b = -(r.wx * cx + r.wy * cy);
    // SNR along w: distance between projected means over projected σ.
    double proj = r.wx * dmx + r.wy * dmy;
    double var = r.wx * (sxx * r.wx + sxy * r.wy) + r.wy * (sxy * r.wx + syy * r.wy);
    r.snr = var > 0 ? std::abs(proj) / std::sqrt(var) : 0.0;
    r.assignmentError = 0.5 * std::erfc(r.snr / (2.0 * std::numbers::sqrt2));
    return r;
}

std::array<std::array<double, 2>, 2> assignmentMatrix(std::span<const double> x, std::span<const double> y,
                                                       std::span<const int> prepared, const Gmm2Result& g) {
    std::array<std::array<double, 2>, 2> m{{{0, 0}, {0, 0}}};
    std::array<double, 2> tot{0, 0};
    for (std::size_t i = 0; i < x.size(); ++i) {
        int p = prepared[i] ? 1 : 0; int r = g.classify(x[i], y[i]);
        m[static_cast<std::size_t>(p)][static_cast<std::size_t>(r)] += 1; tot[static_cast<std::size_t>(p)] += 1;
    }
    for (std::size_t p = 0; p < 2; ++p) if (tot[p] > 0) for (auto& v : m[p]) v /= tot[p];
    return m;
}

} // namespace qlab::data::fit
