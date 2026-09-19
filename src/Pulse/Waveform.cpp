#include "Pulse/Waveform.hpp"
#include "Pulse/Errors.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

namespace qlab::pulse {
namespace {
constexpr double kPi = std::numbers::pi;

double liftedGauss(double x, double center, double sigma, double edge) {
    // exp(-(x-c)²/2σ²) lifted so that it vanishes at |x-c| = edge.
    const double c = std::exp(-(edge * edge) / (2.0 * sigma * sigma));
    const double g = std::exp(-((x - center) * (x - center)) / (2.0 * sigma * sigma));
    return (g - c) / (1.0 - c);
}
double liftedGaussDeriv(double x, double center, double sigma, double edge) {
    const double c = std::exp(-(edge * edge) / (2.0 * sigma * sigma));
    const double g = std::exp(-((x - center) * (x - center)) / (2.0 * sigma * sigma));
    return (-(x - center) / (sigma * sigma)) * g / (1.0 - c);
}
// A·e^{iφ}. std::polar requires a non-negative magnitude (it yields NaN otherwise), and the
// echoed-CR halves carry negative amplitudes (spec 10 §6.3).
Complex envelopeScale(double amplitude, double phase) {
    return {amplitude * std::cos(phase), amplitude * std::sin(phase)};
}
} // namespace

std::string_view waveformKindName(WaveformKind k) {
    switch (k) {
    case WaveformKind::Constant:
        return "constant";
    case WaveformKind::Gaussian:
        return "gaussian";
    case WaveformKind::GaussianSquare:
        return "gaussian_square";
    case WaveformKind::Drag:
        return "drag";
    case WaveformKind::Cosine:
        return "cosine";
    case WaveformKind::Sech:
        return "sech";
    case WaveformKind::Slepian:
        return "slepian";
    case WaveformKind::Samples:
        return "samples";
    }
    return "?";
}

Result<WaveformKind> waveformKindFromName(std::string_view s) {
    if (s == "constant")
        return WaveformKind::Constant;
    if (s == "gaussian")
        return WaveformKind::Gaussian;
    if (s == "gaussian_square")
        return WaveformKind::GaussianSquare;
    if (s == "drag")
        return WaveformKind::Drag;
    if (s == "cosine")
        return WaveformKind::Cosine;
    if (s == "sech")
        return WaveformKind::Sech;
    if (s == "slepian")
        return WaveformKind::Slepian;
    if (s == "samples")
        return WaveformKind::Samples;
    return fail(kErrWaveform, std::format("unknown waveform '{}'", s));
}

double gaussianShape(double t, double T, double sigma) {
    if (t < 0.0 || t > T)
        return 0.0;
    return liftedGauss(t, T / 2.0, sigma, T / 2.0);
}
double gaussianShapeDerivative(double t, double T, double sigma) {
    if (t < 0.0 || t > T)
        return 0.0;
    return liftedGaussDeriv(t, T / 2.0, sigma, T / 2.0);
}
double gaussianShapeArea(double T, double sigma) {
    const double edge = T / 2.0;
    const double c = std::exp(-(edge * edge) / (2.0 * sigma * sigma));
    const double integral =
        sigma * std::sqrt(2.0 * kPi) * std::erf(T / (2.0 * std::numbers::sqrt2 * sigma));
    return (integral - c * T) / (1.0 - c);
}

double gaussianSquareShape(double t, double T, double sigma, double rise) {
    if (t < 0.0 || t > T)
        return 0.0;
    if (rise <= 0.0)
        return 1.0;
    if (t < rise)
        return liftedGauss(t, rise, sigma, rise);
    if (t > T - rise)
        return liftedGauss(t, T - rise, sigma, rise);
    return 1.0;
}
double gaussianSquareShapeDerivative(double t, double T, double sigma, double rise) {
    if (t < 0.0 || t > T || rise <= 0.0)
        return 0.0;
    if (t < rise)
        return liftedGaussDeriv(t, rise, sigma, rise);
    if (t > T - rise)
        return liftedGaussDeriv(t, T - rise, sigma, rise);
    return 0.0;
}
double gaussianSquareShapeArea(double T, double sigma, double rise) {
    if (rise <= 0.0)
        return T;
    const double c = std::exp(-(rise * rise) / (2.0 * sigma * sigma));
    // ∫₀^rise exp(-(t-rise)²/2σ²) dt = σ√(π/2) erf(rise/(σ√2))
    const double half =
        sigma * std::sqrt(kPi / 2.0) * std::erf(rise / (sigma * std::numbers::sqrt2));
    const double edgeArea = (half - c * rise) / (1.0 - c);
    return 2.0 * edgeArea + (T - 2.0 * rise);
}

double slepianShape(double t, double T, double lambda1) {
    // First two Fourier components of the Martinis–Geller fast-adiabatic sequence
    // (T05 §9), normalised to unit peak. λ₁ = 0 reduces to the cosine shape.
    if (t < 0.0 || t > T)
        return 0.0;
    const double x = 2.0 * kPi * t / T;
    const double raw = (1.0 - std::cos(x)) + lambda1 * (1.0 - std::cos(2.0 * x));
    // Peak of |f|, f(x) = (1 − cos x) + λ(1 − cos 2x): f' = sin x (1 + 4λ cos x) vanishes at x = 0
    // (f = 0), x = π (f = 2) and, for |4λ| ≥ 1, at cos x = −1/(4λ) where f = 1 + 2λ + 1/(8λ).
    double peak = 2.0;
    if (std::abs(4.0 * lambda1) >= 1.0)
        peak = std::max(peak, std::abs(1.0 + 2.0 * lambda1 + 1.0 / (8.0 * lambda1)));
    return raw / peak;
}

Complex Waveform::sample(double t) const {
    if (kind == WaveformKind::Samples) {
        if (samples.empty() || duration <= 0.0)
            return {};
        if (t < 0.0 || t >= duration)
            return {};
        const auto n = static_cast<double>(samples.size());
        auto idx = static_cast<std::size_t>(t / duration * n);
        idx = std::min(idx, samples.size() - 1);
        return samples[idx];
    }
    if (t < 0.0 || t > duration)
        return {};
    const Complex scale = envelopeScale(amplitude, phase);
    switch (kind) {
    case WaveformKind::Constant:
        return scale;
    case WaveformKind::Gaussian:
        return scale * gaussianShape(t, duration, sigma);
    case WaveformKind::GaussianSquare:
        return scale * gaussianSquareShape(t, duration, sigma, rise);
    case WaveformKind::Drag: {
        const double g = gaussianShape(t, duration, sigma);
        const double dg = gaussianShapeDerivative(t, duration, sigma);
        return scale * Complex{g, beta * dg};
    }
    case WaveformKind::Cosine:
        return scale * 0.5 * (1.0 - std::cos(2.0 * kPi * t / duration));
    case WaveformKind::Sech: {
        const double edge = duration / 2.0;
        const double c = 1.0 / std::cosh(edge / tau);
        const double s = 1.0 / std::cosh((t - duration / 2.0) / tau);
        return scale * ((s - c) / (1.0 - c));
    }
    case WaveformKind::Slepian:
        return scale * slepianShape(t, duration, lambda1);
    case WaveformKind::Samples:
        break;
    }
    return {};
}

std::vector<Complex> Waveform::sampled(std::int64_t dtPs) const {
    std::vector<Complex> out;
    if (dtPs <= 0 || duration <= 0.0)
        return out;
    const double dt = static_cast<double>(dtPs) * 1e-12;
    const auto n = static_cast<std::size_t>(std::llround(duration / dt));
    out.reserve(n);
    for (std::size_t k = 0; k < n; ++k)
        out.push_back(sample(static_cast<double>(k) * dt));
    return out;
}

Complex Waveform::area() const {
    const Complex scale = envelopeScale(amplitude, phase);
    switch (kind) {
    case WaveformKind::Constant:
        return scale * duration;
    case WaveformKind::Gaussian:
        return scale * gaussianShapeArea(duration, sigma);
    case WaveformKind::GaussianSquare:
        return scale * gaussianSquareShapeArea(duration, sigma, rise);
    case WaveformKind::Drag:
        // The imaginary part integrates to g(T) − g(0) = 0 for the lifted gaussian.
        return scale * Complex{gaussianShapeArea(duration, sigma), 0.0};
    case WaveformKind::Cosine:
        return scale * duration / 2.0;
    case WaveformKind::Samples: {
        if (samples.empty())
            return {};
        Complex s{};
        for (auto& v : samples)
            s += v;
        return s * (duration / static_cast<double>(samples.size()));
    }
    default:
        break;
    }
    // Composite Simpson on a fine grid for the shapes without a closed form.
    const int n = 4096;
    const double h = duration / n;
    Complex acc = sample(0.0) + sample(duration);
    for (int i = 1; i < n; ++i)
        acc += sample(i * h) * ((i % 2) ? 4.0 : 2.0);
    return acc * (h / 3.0);
}

// A non-finite sample makes the result NaN (std::max alone would skip it), so validate() refuses
// it.
double Waveform::maxAbs() const {
    double m = 0.0;
    const auto take = [&m](Complex v) {
        const double a = std::abs(v);
        m = std::isfinite(a) ? std::max(m, a) : std::numeric_limits<double>::quiet_NaN();
        return std::isfinite(m);
    };
    if (kind == WaveformKind::Samples) {
        for (auto& v : samples)
            if (!take(v))
                break;
        return m;
    }
    const int n = 2048;
    for (int i = 0; i <= n; ++i)
        if (!take(sample(duration * i / n)))
            break;
    return m;
}

Waveform::Spectrum Waveform::spectrum(std::int64_t dtPs) const {
    Spectrum out;
    auto s = sampled(dtPs);
    if (s.empty())
        return out;
    std::size_t n = 1;
    while (n < s.size())
        n <<= 1;
    s.resize(n, Complex{});
    num::Vector v(s.begin(), s.end());
    num::fftInPlace(v, false);
    const double dt = static_cast<double>(dtPs) * 1e-12;
    out.freqHz = num::fftFreq(n, 1.0 / dt);
    out.magnitude.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        out.magnitude[i] = std::abs(v[i]);
    return out;
}

Result<void> Waveform::validate() const {
    if (kind == WaveformKind::Samples) {
        if (samples.empty())
            return fail(kErrWaveform, "samples waveform is empty");
    } else if (duration <= 0.0) {
        return fail(kErrWaveform, std::format("{} waveform needs T > 0", waveformKindName(kind)));
    }
    const bool needsSigma = kind == WaveformKind::Gaussian ||
                            kind == WaveformKind::GaussianSquare || kind == WaveformKind::Drag;
    if (needsSigma && sigma <= 0.0)
        return fail(kErrWaveform,
                    std::format("{} waveform needs sigma > 0", waveformKindName(kind)));
    if (kind == WaveformKind::GaussianSquare && (rise < 0.0 || 2.0 * rise > duration))
        return fail(kErrWaveform, "gaussian_square needs 0 ≤ 2·rise ≤ T");
    if (kind == WaveformKind::Sech && tau <= 0.0)
        return fail(kErrWaveform, "sech waveform needs tau > 0");
    if (!std::isfinite(amplitude) || !std::isfinite(phase) || !std::isfinite(duration))
        return fail(kErrWaveform,
                    std::format("{} waveform has a non-finite parameter", waveformKindName(kind)));
    const double peak = maxAbs();
    if (!std::isfinite(peak))
        return fail(kErrWaveform, std::format("{} envelope evaluates to a non-finite sample",
                                              waveformKindName(kind)));
    if (peak > 1.0 + 1e-9)
        return fail(kErrWaveform,
                    std::format("envelope exceeds unit amplitude (|e|max = {:.4f})", peak));
    return {};
}

Waveform Waveform::constant(double T, double amp, double ph) {
    return {WaveformKind::Constant, T, 0, 0, 0, 0, 0, amp, ph, {}};
}
Waveform Waveform::gaussian(double T, double s, double amp, double ph) {
    return {WaveformKind::Gaussian, T, s, 0, 0, 0, 0, amp, ph, {}};
}
Waveform Waveform::gaussianSquare(double T, double s, double r, double amp, double ph) {
    return {WaveformKind::GaussianSquare, T, s, r, 0, 0, 0, amp, ph, {}};
}
Waveform Waveform::drag(double T, double s, double b, double amp, double ph) {
    return {WaveformKind::Drag, T, s, 0, b, 0, 0, amp, ph, {}};
}
Waveform Waveform::cosine(double T, double amp, double ph) {
    return {WaveformKind::Cosine, T, 0, 0, 0, 0, 0, amp, ph, {}};
}
Waveform Waveform::sech(double T, double t, double amp, double ph) {
    return {WaveformKind::Sech, T, 0, 0, 0, t, 0, amp, ph, {}};
}
Waveform Waveform::slepian(double T, double l1, double amp, double ph) {
    return {WaveformKind::Slepian, T, 0, 0, 0, 0, l1, amp, ph, {}};
}
Waveform Waveform::fromSamples(std::vector<Complex> s, double dtSeconds) {
    Waveform w;
    w.kind = WaveformKind::Samples;
    w.duration = dtSeconds * static_cast<double>(s.size());
    w.samples = std::move(s);
    return w;
}

} // namespace qlab::pulse
