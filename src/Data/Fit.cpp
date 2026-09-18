#include "Data/Fit.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::data::fit {

double FitResult::param(std::string_view name) const {
    for (std::size_t i = 0; i < paramNames.size(); ++i) if (paramNames[i] == name) return beta[i];
    for (auto& [n, v] : derived) if (n == name) return v;
    return std::numeric_limits<double>::quiet_NaN();
}
double FitResult::error(std::string_view name) const {
    for (std::size_t i = 0; i < paramNames.size(); ++i) if (paramNames[i] == name) return sigma[i];
    return std::numeric_limits<double>::quiet_NaN();
}
std::vector<std::pair<std::size_t, std::size_t>> FitResult::degeneratePairs(double threshold) const {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t p = covariance.size();
    for (std::size_t j = 0; j < p; ++j)
        for (std::size_t k = j + 1; k < p; ++k) {
            double d = std::sqrt(covariance[j][j] * covariance[k][k]);
            if (d > 0 && std::abs(covariance[j][k] / d) > threshold) out.emplace_back(j, k);
        }
    return out;
}

namespace linalg {
bool solveSpd(std::vector<std::vector<double>> A, std::vector<double> b, std::vector<double>& x) {
    std::size_t n = A.size();
    // Cholesky A = L L^T in place (lower).
    for (std::size_t j = 0; j < n; ++j) {
        double s = A[j][j];
        for (std::size_t k = 0; k < j; ++k) s -= A[j][k] * A[j][k];
        if (!(s > 0.0) || !std::isfinite(s)) return false;
        A[j][j] = std::sqrt(s);
        for (std::size_t i = j + 1; i < n; ++i) {
            double t = A[i][j];
            for (std::size_t k = 0; k < j; ++k) t -= A[i][k] * A[j][k];
            A[i][j] = t / A[j][j];
        }
    }
    // Forward: L y = b
    for (std::size_t i = 0; i < n; ++i) {
        double s = b[i];
        for (std::size_t k = 0; k < i; ++k) s -= A[i][k] * b[k];
        b[i] = s / A[i][i];
    }
    // Back: L^T x = y
    x.assign(n, 0.0);
    for (std::size_t ii = n; ii-- > 0;) {
        double s = b[ii];
        for (std::size_t k = ii + 1; k < n; ++k) s -= A[k][ii] * x[k];
        x[ii] = s / A[ii][ii];
    }
    return true;
}
bool invertSpd(const std::vector<std::vector<double>>& A, std::vector<std::vector<double>>& inv) {
    std::size_t n = A.size();
    inv.assign(n, std::vector<double>(n, 0.0));
    for (std::size_t c = 0; c < n; ++c) {
        std::vector<double> e(n, 0.0), col; e[c] = 1.0;
        if (!solveSpd(A, e, col)) return false;
        for (std::size_t r = 0; r < n; ++r) inv[r][c] = col[r];
    }
    return true;
}
} // namespace linalg

double dominantFrequency(std::span<const double> x, std::span<const double> y, double* phase, double* amplitude) {
    std::size_t n = x.size();
    if (n < 4) { if (phase) *phase = 0; if (amplitude) *amplitude = 0; return 0.0; }
    double mean = 0; for (double v : y) mean += v; mean /= static_cast<double>(n);
    double xmin = *std::min_element(x.begin(), x.end()), xmax = *std::max_element(x.begin(), x.end());
    double span = xmax - xmin;
    if (span <= 0) return 0.0;
    // Minimum spacing sets the Nyquist limit; scan frequencies from 1/(2 span) to 1/(2 dxmin) on a fine grid.
    std::vector<double> xs(x.begin(), x.end()); std::sort(xs.begin(), xs.end());
    double dxmin = span;
    for (std::size_t i = 1; i < n; ++i) if (xs[i] - xs[i - 1] > 0) dxmin = std::min(dxmin, xs[i] - xs[i - 1]);
    double fmax = 0.5 / dxmin, fmin = 0.5 / span;
    std::size_t m = std::min<std::size_t>(8 * n + 64, 8192);
    double best = 0, bestF = fmin, bestPh = 0;
    for (std::size_t k = 1; k <= m; ++k) {
        double f = fmin + (fmax - fmin) * static_cast<double>(k) / static_cast<double>(m);
        double re = 0, im = 0;
        for (std::size_t i = 0; i < n; ++i) {
            double a = 2.0 * std::numbers::pi * f * (x[i] - xmin);
            re += (y[i] - mean) * std::cos(a); im -= (y[i] - mean) * std::sin(a);
        }
        double mag = re * re + im * im;
        if (mag > best) { best = mag; bestF = f; bestPh = std::atan2(im, re) - 2.0 * std::numbers::pi * f * xmin; }
    }
    if (phase) { double p = std::fmod(bestPh, 2 * std::numbers::pi); if (p > std::numbers::pi) p -= 2 * std::numbers::pi; if (p < -std::numbers::pi) p += 2 * std::numbers::pi; *phase = p; }
    if (amplitude) *amplitude = 2.0 * std::sqrt(best) / static_cast<double>(n);
    return bestF;
}

namespace {
enum class BType { Free, Box, Lower, Upper };
struct Transform {
    BType t; double lo, hi;
    double toBeta(double th) const {
        switch (t) {
        case BType::Free: return th;
        case BType::Box: return lo + (hi - lo) / (1.0 + std::exp(-th));
        case BType::Lower: return lo + std::exp(th);
        case BType::Upper: return hi - std::exp(th);
        }
        return th;
    }
    double toTheta(double b) const {
        switch (t) {
        case BType::Free: return b;
        case BType::Box: { double u = std::clamp((b - lo) / (hi - lo), 1e-9, 1 - 1e-9); return std::log(u / (1 - u)); }
        case BType::Lower: return std::log(std::max(b - lo, 1e-300));
        case BType::Upper: return std::log(std::max(hi - b, 1e-300));
        }
        return b;
    }
    double dBetaDTheta(double th) const {
        switch (t) {
        case BType::Free: return 1.0;
        case BType::Box: { double s = 1.0 / (1.0 + std::exp(-th)); return (hi - lo) * s * (1 - s); }
        case BType::Lower: return std::exp(th);
        case BType::Upper: return -std::exp(th);
        }
        return 1.0;
    }
};
} // namespace

Result<FitResult> fitModel(const FitModel& model, std::span<const double> x, std::span<const double> y,
                           std::span<const double> sigma, const FitOptions& opt, std::span<const double> yIm) {
    const std::size_t n = x.size();
    const std::size_t p = model.nparams();
    const bool cplx = model.complexValued();
    if (n == 0 || y.size() != n) return fail(ErrorCode::Data_ + 10, "x/y size mismatch or empty");
    if (cplx && yIm.size() != n) return fail(ErrorCode::Data_ + 11, "complex model requires yIm");
    if (!sigma.empty() && sigma.size() != n) return fail(ErrorCode::Data_ + 12, "sigma size mismatch");
    const std::size_t m = cplx ? 2 * n : n;
    if (m <= p) return fail(ErrorCode::Data_ + 13, std::format("{} residuals for {} parameters", m, p));

    auto infos = model.params();
    std::vector<double> beta = opt.initial ? *opt.initial : model.initialGuess(x, y, yIm);
    if (beta.size() != p) return fail(ErrorCode::Data_ + 14, "initial guess size mismatch");
    std::vector<Transform> tr(p);
    for (std::size_t j = 0; j < p; ++j) {
        Bound b = opt.bounds ? (*opt.bounds)[j] : infos[j].bound;
        bool hasLo = std::isfinite(b.lo), hasHi = std::isfinite(b.hi);
        tr[j] = {hasLo && hasHi ? BType::Box : hasLo ? BType::Lower : hasHi ? BType::Upper : BType::Free, b.lo, b.hi};
        beta[j] = std::clamp(beta[j], hasLo ? b.lo + 1e-12 * std::max(1.0, std::abs(b.lo)) : -kInf,
                             hasHi ? b.hi - 1e-12 * std::max(1.0, std::abs(b.hi)) : kInf);
    }
    std::vector<bool> fixed(p, false);
    for (std::size_t j = 0; j < std::min(p, opt.fixed.size()); ++j) fixed[j] = opt.fixed[j];
    std::vector<double> theta(p);
    for (std::size_t j = 0; j < p; ++j) theta[j] = tr[j].toTheta(beta[j]);
    auto w = [&](std::size_t i) { return sigma.empty() ? 1.0 : 1.0 / sigma[i]; };

    std::vector<double> r(m), Jbuf(p);
    std::vector<Complex> JbufC(p);
    std::vector<std::vector<double>> J(m, std::vector<double>(p));

    auto residuals = [&](std::span<const double> b, std::vector<double>& out) {
        double chi2 = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (cplx) {
                Complex f = model.evalC(b, x[i]);
                out[2 * i] = (y[i] - f.real()) * w(i);
                out[2 * i + 1] = (yIm[i] - f.imag()) * w(i);
                chi2 += out[2 * i] * out[2 * i] + out[2 * i + 1] * out[2 * i + 1];
            } else {
                out[i] = (y[i] - model.eval(b, x[i])) * w(i);
                chi2 += out[i] * out[i];
            }
        }
        return chi2;
    };
    auto jacobian = [&](std::span<const double> b) {
        // J[i][j] = d r_i / d theta_j = -(d f / d beta_j) * dbeta/dtheta * w_i
        std::vector<double> bp(b.begin(), b.end());
        for (std::size_t i = 0; i < n; ++i) {
            bool ok = cplx ? model.jacobianC(b, x[i], JbufC) : model.jacobian(b, x[i], Jbuf);
            for (std::size_t j = 0; j < p; ++j) {
                double dre, dim = 0;
                if (ok) { if (cplx) { dre = JbufC[j].real(); dim = JbufC[j].imag(); } else dre = Jbuf[j]; }
                else {
                    double h = 1e-6 * std::max(std::abs(b[j]), 1.0);
                    bp[j] = b[j] + h;
                    Complex fp = cplx ? model.evalC(bp, x[i]) : Complex(model.eval(bp, x[i]), 0);
                    bp[j] = b[j] - h;
                    Complex fm = cplx ? model.evalC(bp, x[i]) : Complex(model.eval(bp, x[i]), 0);
                    bp[j] = b[j];
                    dre = (fp.real() - fm.real()) / (2 * h); dim = (fp.imag() - fm.imag()) / (2 * h);
                }
                double chain = fixed[j] ? 0.0 : tr[j].dBetaDTheta(theta[j]);
                if (cplx) { J[2 * i][j] = -dre * chain * w(i); J[2 * i + 1][j] = -dim * chain * w(i); }
                else J[i][j] = -dre * chain * w(i);
            }
        }
    };

    double chi2 = residuals(beta, r);
    double lambda = opt.lambda0;
    FitResult res;
    res.iterations = 0;
    std::string msg = "max iterations";
    std::vector<std::vector<double>> JtJ(p, std::vector<double>(p));
    std::vector<double> Jtr(p), delta, thetaNew(p), betaNew(p), rNew(m);
    for (int it = 0; it < opt.maxIterations; ++it) {
        res.iterations = it + 1;
        jacobian(beta);
        for (std::size_t j = 0; j < p; ++j) {
            Jtr[j] = 0;
            for (std::size_t k = 0; k < p; ++k) JtJ[j][k] = 0;
        }
        for (std::size_t i = 0; i < m; ++i)
            for (std::size_t j = 0; j < p; ++j) {
                Jtr[j] -= J[i][j] * r[i]; // gradient direction: minimize sum r^2, r = y - f => -J^T r
                for (std::size_t k = j; k < p; ++k) JtJ[j][k] += J[i][j] * J[i][k];
            }
        for (std::size_t j = 0; j < p; ++j) for (std::size_t k = 0; k < j; ++k) JtJ[j][k] = JtJ[k][j];
        double gnorm = 0; for (double g : Jtr) gnorm += g * g;
        if (std::sqrt(gnorm) < 1e-14) { msg = "gradient zero"; res.converged = true; break; }
        bool accepted = false;
        for (int tries = 0; tries < 30 && !accepted; ++tries) {
            auto A = JtJ;
            for (std::size_t j = 0; j < p; ++j) {
                A[j][j] += lambda * std::max(JtJ[j][j], 1e-300);
                if (fixed[j]) { for (std::size_t k = 0; k < p; ++k) { A[j][k] = 0; A[k][j] = 0; } A[j][j] = 1.0; }
            }
            std::vector<double> rhs = Jtr;
            for (std::size_t j = 0; j < p; ++j) if (fixed[j]) rhs[j] = 0;
            if (!linalg::solveSpd(A, rhs, delta)) { lambda *= opt.lambdaUp; continue; }
            for (std::size_t j = 0; j < p; ++j) { thetaNew[j] = theta[j] + delta[j]; betaNew[j] = tr[j].toBeta(thetaNew[j]); }
            double chi2New = residuals(betaNew, rNew);
            if (std::isfinite(chi2New) && chi2New < chi2) {
                double dchi = chi2 - chi2New;
                double dn = 0, tn = 0; for (std::size_t j = 0; j < p; ++j) { dn += delta[j] * delta[j]; tn += theta[j] * theta[j]; }
                theta = thetaNew; beta = betaNew; r = rNew; chi2 = chi2New;
                lambda = std::max(lambda / opt.lambdaDown, 1e-15);
                accepted = true;
                if (dchi < opt.chi2RelTol * std::max(chi2, 1e-300) || std::sqrt(dn) < opt.stepRelTol * std::max(std::sqrt(tn), 1e-300)) {
                    res.converged = true; msg = "converged";
                }
            } else lambda *= opt.lambdaUp;
        }
        if (!accepted) { res.converged = true; msg = "no improving step (converged at tolerance)"; break; }
        if (res.converged) break;
    }
    // Covariance in beta space: (J_beta^T J_beta)^-1 with J_beta = J_theta / chain.
    jacobian(beta);
    std::vector<std::vector<double>> H(p, std::vector<double>(p, 0.0));
    for (std::size_t i = 0; i < m; ++i)
        for (std::size_t j = 0; j < p; ++j) {
            double cj = fixed[j] ? 0.0 : tr[j].dBetaDTheta(theta[j]);
            if (cj == 0) continue;
            for (std::size_t k = 0; k < p; ++k) {
                double ck = fixed[k] ? 0.0 : tr[k].dBetaDTheta(theta[k]);
                if (ck == 0) continue;
                H[j][k] += (J[i][j] / cj) * (J[i][k] / ck);
            }
        }
    for (std::size_t j = 0; j < p; ++j) if (fixed[j]) H[j][j] = 1.0;
    res.ndf = static_cast<double>(m - p);
    res.chi2 = chi2; res.chi2ndf = res.ndf > 0 ? chi2 / res.ndf : 0;
    double scale = sigma.empty() && res.ndf > 0 ? res.chi2ndf : 1.0;
    std::vector<std::vector<double>> cov;
    if (linalg::invertSpd(H, cov)) {
        for (auto& row : cov) for (auto& v : row) v *= scale;
    } else {
        cov.assign(p, std::vector<double>(p, std::numeric_limits<double>::quiet_NaN()));
        msg += "; covariance singular";
    }
    for (std::size_t j = 0; j < p; ++j) if (fixed[j]) { for (std::size_t k = 0; k < p; ++k) { cov[j][k] = 0; cov[k][j] = 0; } }
    res.covariance = cov;
    res.beta = beta;
    res.sigma.resize(p);
    for (std::size_t j = 0; j < p; ++j) res.sigma[j] = std::sqrt(std::max(0.0, cov[j][j]));
    res.residuals = r;
    res.message = msg;
    for (auto& pi : infos) res.paramNames.push_back(pi.name);
    res.atBound.resize(p);
    for (std::size_t j = 0; j < p; ++j) {
        double lo = tr[j].lo, hi = tr[j].hi, sc = std::max(1.0, std::abs(beta[j]));
        res.atBound[j] = (std::isfinite(lo) && std::abs(beta[j] - lo) < 1e-6 * sc) || (std::isfinite(hi) && std::abs(hi - beta[j]) < 1e-6 * sc);
    }
    // R^2 on the (real) data.
    double ymean = 0; for (double v : y) ymean += v; ymean /= static_cast<double>(n);
    double sst = 0, ssr = 0;
    for (std::size_t i = 0; i < n; ++i) {
        double f = cplx ? model.evalC(beta, x[i]).real() : model.eval(beta, x[i]);
        sst += (y[i] - ymean) * (y[i] - ymean); ssr += (y[i] - f) * (y[i] - f);
    }
    res.r2 = sst > 0 ? 1.0 - ssr / sst : 1.0;
    res.derived = model.derived(beta);
    return res;
}

} // namespace qlab::data::fit
