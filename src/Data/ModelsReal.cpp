#include "Data/Models.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace qlab::data::fit {
namespace {
constexpr double kPi = std::numbers::pi;
struct Stats { double ymin, ymax, ymean, xmin, xmax, yFirst, yLast; std::size_t imin, imax; };
Stats stats(std::span<const double> x, std::span<const double> y) {
    Stats s{};
    auto [mn, mx] = std::minmax_element(y.begin(), y.end());
    s.ymin = *mn; s.ymax = *mx; s.imin = static_cast<std::size_t>(mn - y.begin()); s.imax = static_cast<std::size_t>(mx - y.begin());
    auto [xn, xx] = std::minmax_element(x.begin(), x.end());
    s.xmin = *xn; s.xmax = *xx;
    s.ymean = 0; for (double v : y) s.ymean += v; s.ymean /= static_cast<double>(y.size());
    s.yFirst = y.front(); s.yLast = y.back();
    return s;
}
// Decay-time guess: log-linear regression of |y - c| against x for the first part of the data.
double decayGuess(std::span<const double> x, std::span<const double> y, double c, double A) {
    double sx = 0, sy = 0, sxx = 0, sxy = 0; std::size_t n = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        double v = (y[i] - c) / A;
        if (v <= 0.05) continue;
        double l = std::log(v);
        sx += x[i]; sy += l; sxx += x[i] * x[i]; sxy += x[i] * l; ++n;
    }
    double span = *std::max_element(x.begin(), x.end()) - *std::min_element(x.begin(), x.end());
    if (n < 2) return span > 0 ? span / 3.0 : 1.0;
    double den = static_cast<double>(n) * sxx - sx * sx;
    if (std::abs(den) < 1e-300) return span / 3.0;
    double slope = (static_cast<double>(n) * sxy - sx * sy) / den;
    if (slope >= 0) return span > 0 ? span : 1.0;
    return -1.0 / slope;
}
} // namespace

// ---------------------------------------------------------------- exp_decay
std::vector<ParamInfo> ExpDecayModel::params() const {
    return {{"A", "", {}}, {"T", "", {0.0, kInf}}, {"c", "", {}}};
}
double ExpDecayModel::eval(std::span<const double> b, double x) const { return b[0] * std::exp(-x / b[1]) + b[2]; }
bool ExpDecayModel::jacobian(std::span<const double> b, double x, std::span<double> o) const {
    double e = std::exp(-x / b[1]);
    o[0] = e; o[1] = b[0] * e * x / (b[1] * b[1]); o[2] = 1.0; return true;
}
std::vector<double> ExpDecayModel::initialGuess(std::span<const double> x, std::span<const double> y, std::span<const double>) const {
    Stats s = stats(x, y);
    double c = s.yLast, A = s.yFirst - s.yLast;
    if (std::abs(A) < 1e-12) A = s.ymax - s.ymin;
    return {A, decayGuess(x, y, c, A), c};
}

// ---------------------------------------------------------------- ramsey
std::vector<ParamInfo> RamseyModel::params() const {
    return {{"A", "", {}}, {"T", "", {0.0, kInf}}, {"delta", "", {0.0, kInf}}, {"phi", "rad", {}}, {"c", "", {}}};
}
double RamseyModel::eval(std::span<const double> b, double x) const {
    return b[0] * std::exp(-x / b[1]) * std::cos(2 * kPi * b[2] * x + b[3]) + b[4];
}
bool RamseyModel::jacobian(std::span<const double> b, double x, std::span<double> o) const {
    double e = std::exp(-x / b[1]), arg = 2 * kPi * b[2] * x + b[3], cs = std::cos(arg), sn = std::sin(arg);
    o[0] = e * cs; o[1] = b[0] * e * cs * x / (b[1] * b[1]); o[2] = -b[0] * e * sn * 2 * kPi * x;
    o[3] = -b[0] * e * sn; o[4] = 1.0; return true;
}
std::vector<double> RamseyModel::initialGuess(std::span<const double> x, std::span<const double> y, std::span<const double>) const {
    Stats s = stats(x, y);
    double ph = 0, amp = 0;
    double f = dominantFrequency(x, y, &ph, &amp);
    double A = std::max(amp, 0.5 * (s.ymax - s.ymin));
    // Envelope decay: local maxima of |y - mean| against x.
    std::vector<double> ex, ey;
    for (std::size_t i = 1; i + 1 < y.size(); ++i) {
        double d = std::abs(y[i] - s.ymean);
        if (d >= std::abs(y[i - 1] - s.ymean) && d >= std::abs(y[i + 1] - s.ymean)) { ex.push_back(x[i]); ey.push_back(d + s.ymean); }
    }
    double T = ex.size() >= 2 ? decayGuess(ex, ey, s.ymean, A) : (s.xmax - s.xmin);
    return {A, T, f, ph, s.ymean};
}

// ---------------------------------------------------------------- echo
std::vector<ParamInfo> EchoModel::params() const { return {{"A", "", {}}, {"T", "", {0.0, kInf}}, {"c", "", {}}}; }
double EchoModel::eval(std::span<const double> b, double x) const { return b[0] * std::exp(-std::pow(x / b[1], n_)) + b[2]; }
bool EchoModel::jacobian(std::span<const double> b, double x, std::span<double> o) const {
    double u = x / b[1], e = std::exp(-std::pow(u, n_));
    o[0] = e; o[1] = b[0] * e * n_ * std::pow(u, n_) / b[1]; o[2] = 1.0; return true;
}
std::vector<double> EchoModel::initialGuess(std::span<const double> x, std::span<const double> y, std::span<const double>) const {
    Stats s = stats(x, y);
    double c = s.yLast, A = s.yFirst - s.yLast;
    if (std::abs(A) < 1e-12) A = s.ymax - s.ymin;
    double T = decayGuess(x, y, c, A);
    if (n_ == 2.0) T *= std::sqrt(kPi) / 2.0; // e^{-t/T} vs e^{-(t/T)^2}: match areas
    return {A, T, c};
}

// ---------------------------------------------------------------- rabi_amp
std::vector<ParamInfo> RabiAmpModel::params() const { return {{"A", "", {}}, {"a_pi", "", {0.0, kInf}}, {"c", "", {}}}; }
double RabiAmpModel::eval(std::span<const double> b, double x) const { return b[0] * std::cos(kPi * x / b[1]) + b[2]; }
bool RabiAmpModel::jacobian(std::span<const double> b, double x, std::span<double> o) const {
    double arg = kPi * x / b[1];
    o[0] = std::cos(arg); o[1] = b[0] * std::sin(arg) * kPi * x / (b[1] * b[1]); o[2] = 1.0; return true;
}
std::vector<double> RabiAmpModel::initialGuess(std::span<const double> x, std::span<const double> y, std::span<const double>) const {
    Stats s = stats(x, y);
    double f = dominantFrequency(x, y);
    double api = f > 0 ? 1.0 / (2.0 * f) : (s.xmax - s.xmin);
    return {0.5 * (s.ymax - s.ymin) * (s.yFirst >= s.ymean ? 1.0 : -1.0), api, s.ymean};
}
std::vector<std::pair<std::string, double>> RabiAmpModel::derived(std::span<const double> b) const {
    return {{"a_pi2", b[1] / 2.0}};
}

// ---------------------------------------------------------------- rabi_time
std::vector<ParamInfo> RabiTimeModel::params() const {
    return {{"A", "", {}}, {"f", "", {0.0, kInf}}, {"phi", "rad", {}}, {"tau", "", {0.0, kInf}}, {"c", "", {}}};
}
double RabiTimeModel::eval(std::span<const double> b, double x) const {
    return b[0] * std::cos(2 * kPi * b[1] * x + b[2]) * std::exp(-x / b[3]) + b[4];
}
bool RabiTimeModel::jacobian(std::span<const double> b, double x, std::span<double> o) const {
    double arg = 2 * kPi * b[1] * x + b[2], cs = std::cos(arg), sn = std::sin(arg), e = std::exp(-x / b[3]);
    o[0] = cs * e; o[1] = -b[0] * sn * 2 * kPi * x * e; o[2] = -b[0] * sn * e;
    o[3] = b[0] * cs * e * x / (b[3] * b[3]); o[4] = 1.0; return true;
}
std::vector<double> RabiTimeModel::initialGuess(std::span<const double> x, std::span<const double> y, std::span<const double>) const {
    RamseyModel r; auto g = r.initialGuess(x, y, {});
    return {g[0], g[2], g[3], g[1], g[4]};
}

// ---------------------------------------------------------------- lorentzian
std::vector<ParamInfo> LorentzianModel::params() const { return {{"A", "", {}}, {"x0", "", {}}, {"gamma", "", {0.0, kInf}}, {"c", "", {}}}; }
double LorentzianModel::eval(std::span<const double> b, double x) const {
    double h = b[2] / 2, d = x - b[1];
    return b[0] * h * h / (d * d + h * h) + b[3];
}
bool LorentzianModel::jacobian(std::span<const double> b, double x, std::span<double> o) const {
    double h = b[2] / 2, d = x - b[1], den = d * d + h * h, L = h * h / den;
    o[0] = L; o[1] = b[0] * 2 * d * h * h / (den * den);
    o[2] = b[0] * 0.5 * (2 * h * den - h * h * 2 * h) / (den * den); o[3] = 1.0; return true;
}
std::vector<double> LorentzianModel::initialGuess(std::span<const double> x, std::span<const double> y, std::span<const double>) const {
    Stats s = stats(x, y);
    // Decide peak vs dip by which extreme is farther from the median.
    std::vector<double> ys(y.begin(), y.end()); std::nth_element(ys.begin(), ys.begin() + static_cast<std::ptrdiff_t>(ys.size() / 2), ys.end());
    double med = ys[ys.size() / 2];
    bool peak = (s.ymax - med) >= (med - s.ymin);
    std::size_t ip = peak ? s.imax : s.imin;
    double A = (peak ? s.ymax : s.ymin) - med;
    double half = med + A / 2;
    // FWHM: scan outward from the extremum until crossing half height.
    std::size_t lo = ip, hi = ip;
    while (lo > 0 && (peak ? y[lo] > half : y[lo] < half)) --lo;
    while (hi + 1 < y.size() && (peak ? y[hi] > half : y[hi] < half)) ++hi;
    double gamma = std::abs(x[hi] - x[lo]);
    if (gamma <= 0) gamma = (s.xmax - s.xmin) / 10;
    return {A, x[ip], gamma, med};
}

// ---------------------------------------------------------------- gaussian
std::vector<ParamInfo> GaussianModel::params() const { return {{"A", "", {}}, {"x0", "", {}}, {"sigma", "", {0.0, kInf}}, {"c", "", {}}}; }
double GaussianModel::eval(std::span<const double> b, double x) const {
    double d = (x - b[1]) / b[2];
    return b[0] * std::exp(-0.5 * d * d) + b[3];
}
bool GaussianModel::jacobian(std::span<const double> b, double x, std::span<double> o) const {
    double d = (x - b[1]) / b[2], e = std::exp(-0.5 * d * d);
    o[0] = e; o[1] = b[0] * e * d / b[2]; o[2] = b[0] * e * d * d / b[2]; o[3] = 1.0; return true;
}
std::vector<double> GaussianModel::initialGuess(std::span<const double> x, std::span<const double> y, std::span<const double> yi) const {
    LorentzianModel l; auto g = l.initialGuess(x, y, yi);
    return {g[0], g[1], g[2] / 2.3548, g[3]};
}

// ---------------------------------------------------------------- rb_decay
std::vector<ParamInfo> RbDecayModel::params() const { return {{"A", "", {}}, {"p", "", {0.0, 1.0}}, {"B", "", {}}}; }
double RbDecayModel::eval(std::span<const double> b, double x) const { return b[0] * std::pow(b[1], x) + b[2]; }
bool RbDecayModel::jacobian(std::span<const double> b, double x, std::span<double> o) const {
    double pm = std::pow(b[1], x);
    o[0] = pm; o[1] = b[1] > 0 ? b[0] * x * pm / b[1] : 0.0; o[2] = 1.0; return true;
}
std::vector<double> RbDecayModel::initialGuess(std::span<const double> x, std::span<const double> y, std::span<const double>) const {
    Stats s = stats(x, y);
    double d = static_cast<double>(1u << nq_);
    double B = std::min(s.yLast, 1.0 / d), A = s.yFirst - B;
    if (A <= 0) A = 1.0 - 1.0 / d;
    double T = decayGuess(x, y, B, A);
    double p = std::exp(-1.0 / std::max(T, 1e-9));
    return {A, std::clamp(p, 0.01, 0.9999), B};
}
std::vector<std::pair<std::string, double>> RbDecayModel::derived(std::span<const double> b) const {
    std::vector<std::pair<std::string, double>> out{{"r", rbErrorFromP(b[1], nq_)}, {"F_avg", 1.0 - rbErrorFromP(b[1], nq_)}};
    if (il_) {
        double d = static_cast<double>(1u << nq_);
        out.emplace_back("r_gate", (1.0 - b[1] / pRef_) * (d - 1.0) / d);
    }
    return out;
}

// ---------------------------------------------------------------- linear
std::vector<ParamInfo> LinearModel::params() const { return {{"a", "", {}}, {"b", "", {}}}; }
double LinearModel::eval(std::span<const double> b, double x) const { return b[0] + b[1] * x; }
bool LinearModel::jacobian(std::span<const double>, double x, std::span<double> o) const { o[0] = 1.0; o[1] = x; return true; }
std::vector<double> LinearModel::initialGuess(std::span<const double> x, std::span<const double> y, std::span<const double>) const {
    double sx = 0, sy = 0, sxx = 0, sxy = 0; double n = static_cast<double>(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) { sx += x[i]; sy += y[i]; sxx += x[i] * x[i]; sxy += x[i] * y[i]; }
    double den = n * sxx - sx * sx;
    double slope = std::abs(den) > 1e-300 ? (n * sxy - sx * sy) / den : 0.0;
    return {(sy - slope * sx) / n, slope};
}

// ---------------------------------------------------------------- factory
std::unique_ptr<FitModel> makeModel(std::string_view id) {
    if (id == "exp_decay") return std::make_unique<ExpDecayModel>();
    if (id == "ramsey") return std::make_unique<RamseyModel>();
    if (id == "echo") return std::make_unique<EchoModel>(1.0);
    if (id == "echo_gauss") return std::make_unique<EchoModel>(2.0);
    if (id == "rabi_amp") return std::make_unique<RabiAmpModel>();
    if (id == "rabi_time") return std::make_unique<RabiTimeModel>();
    if (id == "lorentzian") return std::make_unique<LorentzianModel>();
    if (id == "gaussian") return std::make_unique<GaussianModel>();
    if (id == "resonator_notch") return std::make_unique<ResonatorNotchModel>();
    if (id == "rb_decay") return std::make_unique<RbDecayModel>(1, false);
    if (id == "rb_decay_2q") return std::make_unique<RbDecayModel>(2, false);
    if (id == "rb_interleaved") return std::make_unique<RbDecayModel>(1, true);
    if (id == "linear") return std::make_unique<LinearModel>();
    return nullptr;
}
std::vector<std::string> modelIds() {
    return {"exp_decay", "ramsey", "echo", "echo_gauss", "rabi_amp", "rabi_time", "lorentzian", "gaussian",
            "resonator_notch", "rb_decay", "rb_decay_2q", "rb_interleaved", "linear", "gmm2"};
}

} // namespace qlab::data::fit
