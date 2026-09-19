// Spec 12 §6, spec 22 §5 — notch-resonator fit: the diameter-correction circle fit supplies the
// starting values, data::fit's `resonator_notch` model (Levenberg–Marquardt on Re and Im) refines
// them and supplies the covariance. 1/Q_i = 1/Q_l − cos φ/|Q_c| with σ(Q_i) by error propagation.
#include "Data/Models.hpp"
#include "Instruments/Vna.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::instr {
namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;

struct Circle {
    Complex centre;
    double radius = 0.0;
    bool ok = false;
};

// Algebraic least-squares circle (Kåsa): minimise Σ (x² + y² + Dx + Ey + F)². Coordinates are
// centred and scaled first so the 3×3 normal equations stay well conditioned.
Circle fitCircle(std::span<const Complex> z) {
    Circle c;
    const auto n = static_cast<double>(z.size());
    if (z.size() < 5)
        return c;
    Complex mean{};
    for (auto const& p : z)
        mean += p / n;
    double scale = 0.0;
    for (auto const& p : z)
        scale += std::abs(p - mean) / n;
    if (!(scale > 0.0))
        return c;
    double sxx = 0, sxy = 0, syy = 0, sx = 0, sy = 0, sxr = 0, syr = 0, sr = 0;
    for (auto const& p : z) {
        const double x = (p.real() - mean.real()) / scale, y = (p.imag() - mean.imag()) / scale,
                     r = x * x + y * y;
        sxx += x * x;
        sxy += x * y;
        syy += y * y;
        sx += x;
        sy += y;
        sxr += x * r;
        syr += y * r;
        sr += r;
    }
    // [sxx sxy sx; sxy syy sy; sx sy n] [D E F]ᵀ = −[sxr syr sr]ᵀ, by Cramer's rule.
    const double a[3][3] = {{sxx, sxy, sx}, {sxy, syy, sy}, {sx, sy, n}};
    const double b[3] = {-sxr, -syr, -sr};
    auto det3 = [](const double m[3][3]) {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
               m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    };
    const double det = det3(a);
    if (std::abs(det) < 1e-300)
        return c;
    double sol[3];
    for (int col = 0; col < 3; ++col) {
        double m[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                m[i][j] = j == col ? b[i] : a[i][j];
        sol[col] = det3(m) / det;
    }
    const double cx = -sol[0] / 2.0, cy = -sol[1] / 2.0;
    const double r2 = cx * cx + cy * cy - sol[2];
    if (!(r2 > 0.0))
        return c;
    c.centre = mean + scale * Complex{cx, cy};
    c.radius = scale * std::sqrt(r2);
    c.ok = true;
    return c;
}

double median(std::vector<double> v) {
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}
} // namespace

Result<ResonatorFit> fitResonatorNotch(std::span<const double> f, std::span<const double> re,
                                       std::span<const double> im, double sigma,
                                       std::optional<double> nearHz) {
    const std::size_t n = f.size();
    if (n < 16 || re.size() != n || im.size() != n)
        return fail(err::FitFailed, "notch fit: needs at least 16 complex points");
    std::vector<double> mag(n);
    for (std::size_t i = 0; i < n; ++i)
        mag[i] = std::hypot(re[i], im[i]);
    const double baseline = median(mag);
    // The dip: deepest point, or the deepest within 1 % of the span around `nearHz`.
    std::size_t lo = 0, hi = n - 1;
    if (nearHz) {
        const double reach = std::max(0.01 * (f[n - 1] - f[0]), 4.0 * (f[1] - f[0]));
        while (lo + 1 < n && f[lo] < *nearHz - reach)
            ++lo;
        while (hi > lo && f[hi] > *nearHz + reach)
            --hi;
    }
    const std::size_t dip = static_cast<std::size_t>(
        std::min_element(mag.begin() + static_cast<std::ptrdiff_t>(lo),
                         mag.begin() + static_cast<std::ptrdiff_t>(hi) + 1) -
        mag.begin());
    if (mag[dip] > 0.97 * baseline)
        return fail(err::FitFailed, "notch fit: no resonance dip in the trace");
    // Width where |S21|² recovers half of its depth: FWHM = f_r/Q_l for a notch.
    const double half = 0.5 * (mag[dip] * mag[dip] + baseline * baseline);
    std::size_t left = dip, right = dip;
    while (left > 0 && mag[left] * mag[left] < half)
        --left;
    while (right + 1 < n && mag[right] * mag[right] < half)
        ++right;
    const double fwhm = std::max(f[right] - f[left], 2.0 * (f[1] - f[0]));
    ResonatorFit out;
    const double reach = 8.0 * fwhm; // the window: the whole circle, not the neighbours
    out.first = dip;
    out.last = dip;
    while (out.first > 0 && f[out.first] > f[dip] - reach)
        --out.first;
    while (out.last + 1 < n && f[out.last] < f[dip] + reach)
        ++out.last;
    const std::size_t m = out.last - out.first + 1;
    if (m < 16)
        return fail(
            err::FitFailed,
            std::format(
                "notch fit: only {} points across the resonance — narrow the span or add points",
                m));
    std::vector<double> wf(f.begin() + static_cast<std::ptrdiff_t>(out.first),
                           f.begin() + static_cast<std::ptrdiff_t>(out.last) + 1);
    std::vector<double> wre(re.begin() + static_cast<std::ptrdiff_t>(out.first),
                            re.begin() + static_cast<std::ptrdiff_t>(out.last) + 1);
    std::vector<double> wim(im.begin() + static_cast<std::ptrdiff_t>(out.first),
                            im.begin() + static_cast<std::ptrdiff_t>(out.last) + 1);

    // Cable delay from the phase slope of the outer fifths of the window (the resonance adds no net
    // phase there).
    std::vector<double> phase(m);
    for (std::size_t i = 0; i < m; ++i) {
        phase[i] = std::atan2(wim[i], wre[i]);
        if (i > 0)
            while (phase[i] - phase[i - 1] > std::numbers::pi)
                phase[i] -= kTwoPi;
        if (i > 0)
            while (phase[i] - phase[i - 1] < -std::numbers::pi)
                phase[i] += kTwoPi;
    }
    const std::size_t edge = std::max<std::size_t>(3, m / 5);
    double sf = 0, sp = 0, sff = 0, sfp = 0, cnt = 0;
    for (std::size_t i = 0; i < m; ++i) {
        if (i >= edge && i + edge < m)
            continue;
        const double x = wf[i] - wf[m / 2];
        sf += x;
        sp += phase[i];
        sff += x * x;
        sfp += x * phase[i];
        cnt += 1.0;
    }
    const double slope = (cnt * sfp - sf * sp) / std::max(cnt * sff - sf * sf, 1e-300);
    const double tau = -slope / kTwoPi;
    std::vector<Complex> z(m);
    for (std::size_t i = 0; i < m; ++i)
        z[i] = Complex{wre[i], wim[i]} * std::polar(1.0, kTwoPi * wf[i] * tau);
    const Circle circle = fitCircle(z);
    if (!circle.ok)
        return fail(err::FitFailed, "notch fit: the points do not form a circle");
    // Off-resonant point: diametrically opposite the resonance point. Then (diameter correction)
    // centre/P = 1 − (Q_l/2|Q_c|) e^{iφ}.
    const Complex zr = z[dip - out.first];
    const Complex offRes =
        circle.centre +
        (circle.centre - zr) * (circle.radius / std::max(std::abs(circle.centre - zr), 1e-300));
    const Complex toCentre = 1.0 - circle.centre / offRes;
    const double diameter = 2.0 * std::abs(toCentre); // Q_l/|Q_c|
    const double ql0 = f[dip] / fwhm;
    std::vector<double> initial = {f[dip],
                                   ql0,
                                   ql0 / std::clamp(diameter, 1e-3, 10.0),
                                   std::arg(toCentre),
                                   std::abs(offRes),
                                   std::arg(offRes),
                                   tau};

    auto model = data::fit::makeModel("resonator_notch");
    if (!model)
        return fail(err::FitFailed, "notch fit: the resonator_notch model is not registered");
    data::fit::FitOptions opt;
    opt.maxIterations = 400;
    opt.chi2RelTol = 1e-14;
    opt.stepRelTol = 1e-13;
    const std::vector<double> weights =
        sigma > 0.0 ? std::vector<double>(m, sigma) : std::vector<double>{};
    Result<data::fit::FitResult> fit = fail(err::FitFailed, "notch fit: not run");
    for (int pass = 0; pass < 3;
         ++pass) { // the engine stops on small steps: restart from its answer
        opt.initial = initial;
        fit = data::fit::fitModel(*model, wf, wre, weights, opt, wim);
        if (!fit)
            return fail(err::FitFailed, "notch fit: " + fit.error().message);
        double change = 0.0;
        for (std::size_t j = 0; j < 4; ++j)
            change = std::max(change, std::abs(fit->beta[j] - initial[j]) /
                                          std::max(std::abs(initial[j]), 1e-12));
        initial = fit->beta;
        if (change < 1e-11)
            break;
    }
    const auto& b = fit->beta;
    out.frHz = b[0];
    out.ql = b[1];
    out.qc = b[2];
    out.phi = b[3];
    out.amplitude = b[4];
    out.alpha = b[5];
    out.tauS = b[6];
    out.qi = data::fit::ResonatorNotchModel::internalQ(out.ql, out.qc, out.phi);
    out.kappaHz = out.frHz / out.ql;
    out.sigmaFrHz = fit->sigma[0];
    out.sigmaQl = fit->sigma[1];
    out.sigmaQc = fit->sigma[2];
    out.sigmaPhi = fit->sigma[3];
    // g = 1/Q_i = 1/Q_l − cos φ/|Q_c|; σ_g² = Jᵀ C J over (Q_l, Q_c, φ); σ(Q_i) = Q_i² σ_g.
    const double J[3] = {-1.0 / (out.ql * out.ql), std::cos(out.phi) / (out.qc * out.qc),
                         std::sin(out.phi) / out.qc};
    double var = 0.0;
    if (fit->covariance.size() >= 4)
        for (std::size_t i = 0; i < 3; ++i)
            for (std::size_t j = 0; j < 3; ++j)
                var += J[i] * fit->covariance[i + 1][j + 1] * J[j];
    out.sigmaQi = std::isfinite(out.qi) ? out.qi * out.qi * std::sqrt(std::max(var, 0.0)) : 0.0;
    out.chi2ndf = fit->chi2ndf;
    out.converged = fit->converged;
    return out;
}

} // namespace qlab::instr
