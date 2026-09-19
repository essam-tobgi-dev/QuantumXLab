#include "Data/Models.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace qlab::data::fit {
namespace {
constexpr double kPi = std::numbers::pi;
constexpr Complex kI{0.0, 1.0};
} // namespace

// Parameters: b = { f_r, Q_l, Q_c, phi, a, alpha, tau }
std::vector<ParamInfo> ResonatorNotchModel::params() const {
    return {{"f_r", "Hz", {0.0, kInf}},
            {"Q_l", "", {1.0, kInf}},
            {"Q_c", "", {1.0, kInf}},
            {"phi", "rad", {-kPi, kPi}},
            {"a", "", {0.0, kInf}},
            {"alpha", "rad", {}},
            {"tau", "s", {}}};
}

Complex ResonatorNotchModel::evalC(std::span<const double> b, double f) const {
    const double fr = b[0], Ql = b[1], Qc = b[2], phi = b[3], a = b[4], alpha = b[5], tau = b[6];
    const Complex env = a * std::exp(kI * (alpha - 2.0 * kPi * f * tau));
    const Complex den = 1.0 + 2.0 * kI * Ql * (f - fr) / fr;
    const Complex notch = (Ql / Qc) * std::exp(kI * phi) / den;
    return env * (1.0 - notch);
}

bool ResonatorNotchModel::jacobianC(std::span<const double> b, double f,
                                    std::span<Complex> o) const {
    const double fr = b[0], Ql = b[1], Qc = b[2], phi = b[3], a = b[4], alpha = b[5], tau = b[6];
    const Complex env = a * std::exp(kI * (alpha - 2.0 * kPi * f * tau));
    const Complex den = 1.0 + 2.0 * kI * Ql * (f - fr) / fr;
    const Complex eph = std::exp(kI * phi);
    const Complex notch = (Ql / Qc) * eph / den;
    const Complex S = env * (1.0 - notch);
    // d den / d fr = -2i Ql f / fr^2 ; d den / d Ql = 2i (f - fr)/fr
    const Complex dden_dfr = -2.0 * kI * Ql * f / (fr * fr);
    const Complex dden_dQl = 2.0 * kI * (f - fr) / fr;
    const Complex dnotch_dfr = -notch / den * dden_dfr;
    const Complex dnotch_dQl = (eph / Qc) / den - notch / den * dden_dQl;
    const Complex dnotch_dQc = -notch / Qc;
    const Complex dnotch_dphi = kI * notch;
    o[0] = -env * dnotch_dfr;
    o[1] = -env * dnotch_dQl;
    o[2] = -env * dnotch_dQc;
    o[3] = -env * dnotch_dphi;
    o[4] = a > 0 ? S / a : (1.0 - notch) * std::exp(kI * (alpha - 2.0 * kPi * f * tau));
    o[5] = kI * S;
    o[6] = -2.0 * kI * kPi * f * S;
    return true;
}

double ResonatorNotchModel::internalQ(double Ql, double Qc, double phi) {
    // 1/Q_i = 1/Q_l - Re(e^{-iφ}/|Q_c|)  (Probst et al. 2015 diameter-correction convention)
    double inv = 1.0 / Ql - std::cos(phi) / Qc;
    return inv > 0 ? 1.0 / inv : kInf;
}

std::vector<double> ResonatorNotchModel::initialGuess(std::span<const double> x,
                                                      std::span<const double> y,
                                                      std::span<const double> yIm) const {
    const std::size_t n = x.size();
    std::vector<double> mag(n);
    for (std::size_t i = 0; i < n; ++i)
        mag[i] = std::hypot(y[i], yIm.empty() ? 0.0 : yIm[i]);
    // Baseline amplitude from the edges; f_r from the minimum of |S21|.
    std::size_t edge = std::max<std::size_t>(1, n / 10);
    double a = 0;
    for (std::size_t i = 0; i < edge; ++i)
        a += mag[i] + mag[n - 1 - i];
    a /= static_cast<double>(2 * edge);
    if (a <= 0)
        a = 1.0;
    std::size_t imin =
        static_cast<std::size_t>(std::min_element(mag.begin(), mag.end()) - mag.begin());
    double fr = x[imin];
    double depth = mag[imin] / a; // |1 - Ql/Qc| at resonance
    // FWHM of the dip in |S21|^2 domain: find half-depth crossings.
    double half = (a + mag[imin]) / 2.0;
    std::size_t lo = imin, hi = imin;
    while (lo > 0 && mag[lo] < half)
        --lo;
    while (hi + 1 < n && mag[hi] < half)
        ++hi;
    double fwhm = std::max(x[hi] - x[lo], std::abs(x[1] - x[0]) * 2.0);
    double Ql = fr / fwhm;
    double Qc = Ql / std::max(1e-3, 1.0 - depth);
    // Electrical delay from the phase slope at the edges; alpha from the low-edge phase.
    double tau = 0.0, alpha = 0.0;
    if (!yIm.empty() && n > 2 * edge) {
        double p0 = std::atan2(yIm[0], y[0]);
        double p1 = std::atan2(yIm[n - 1], y[n - 1]);
        double dp = p1 - p0;
        while (dp > kPi)
            dp -= 2 * kPi;
        while (dp < -kPi)
            dp += 2 * kPi;
        tau = -dp / (2.0 * kPi * (x[n - 1] - x[0]));
        alpha = p0 + 2.0 * kPi * x[0] * tau;
    }
    return {fr, Ql, Qc, 0.0, a, alpha, tau};
}

std::vector<std::pair<std::string, double>>
ResonatorNotchModel::derived(std::span<const double> b) const {
    double Qi = internalQ(b[1], b[2], b[3]);
    return {{"Q_i", Qi}, {"kappa", b[0] / b[1]}, {"fwhm", b[0] / b[1]}};
}

} // namespace qlab::data::fit
