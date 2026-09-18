#pragma once
// Spec 10 §2 — pulse envelopes. A waveform is a dimensionless complex envelope e(t),
// |e| ≤ 1, on [0, T]; the physical drive is A·e(t) with A the channel amplitude (§7).
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include "Numerics/Numerics.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

namespace qlab::pulse {

using num::Complex;

enum class WaveformKind : std::uint8_t {
    Constant, Gaussian, GaussianSquare, Drag, Cosine, Sech, Slepian, Samples,
};

std::string_view waveformKindName(WaveformKind k);
Result<WaveformKind> waveformKindFromName(std::string_view s);

// Envelope parameters. Times are seconds. `amplitude` and `phase` scale the envelope:
// e(t) → amplitude · e^{i·phase} · shape(t). The shape itself peaks at 1.
struct Waveform {
    WaveformKind kind = WaveformKind::Constant;
    double duration = 0.0;   // T  [s]
    double sigma = 0.0;      // gaussian / gaussian_square width [s]
    double rise = 0.0;       // gaussian_square rise-fall time [s]
    double beta = 0.0;       // DRAG coefficient [s]
    double tau = 0.0;        // sech width [s]
    double lambda1 = 0.0;    // slepian second-Fourier-component weight
    double amplitude = 1.0;  // dimensionless scale (may be negative: echoed CR)
    double phase = 0.0;      // static phase [rad]
    std::vector<Complex> samples; // Samples kind only (already includes amplitude/phase)

    // Envelope at time t (seconds); zero outside [0, T].
    Complex sample(double t) const;
    // Zero-order-hold sampling at the device sample period (spec 10 §1).
    std::vector<Complex> sampled(std::int64_t dtPs) const;
    // ∫₀ᵀ e(t) dt  [s]. Analytic where a closed form exists, Simpson otherwise.
    Complex area() const;
    double maxAbs() const;
    // Spectrum of the sampled envelope, |E(f)| with the frequency axis in Hz (num::fft).
    struct Spectrum { std::vector<double> freqHz; std::vector<double> magnitude; };
    Spectrum spectrum(std::int64_t dtPs) const;
    // Parameter sanity: T > 0, σ > 0 where used, 2·rise ≤ T, |e| ≤ 1 after scaling.
    Result<void> validate() const;

    static Waveform constant(double T, double amp = 1.0, double phase = 0.0);
    static Waveform gaussian(double T, double sigma, double amp = 1.0, double phase = 0.0);
    static Waveform gaussianSquare(double T, double sigma, double rise, double amp = 1.0, double phase = 0.0);
    static Waveform drag(double T, double sigma, double beta, double amp = 1.0, double phase = 0.0);
    static Waveform cosine(double T, double amp = 1.0, double phase = 0.0);
    static Waveform sech(double T, double tau, double amp = 1.0, double phase = 0.0);
    static Waveform slepian(double T, double lambda1, double amp = 1.0, double phase = 0.0);
    static Waveform fromSamples(std::vector<Complex> s, double dtSeconds);

    bool operator==(const Waveform&) const = default;
};

// Shape helpers (unit peak, no amplitude/phase), exposed for the calibration routines.
double gaussianShape(double t, double T, double sigma);
double gaussianShapeDerivative(double t, double T, double sigma); // dg/dt [1/s]
double gaussianSquareShape(double t, double T, double sigma, double rise);
double gaussianSquareShapeDerivative(double t, double T, double sigma, double rise);
double slepianShape(double t, double T, double lambda1);
// ∫₀ᵀ of the lifted gaussian shape, closed form (erf).
double gaussianShapeArea(double T, double sigma);
double gaussianSquareShapeArea(double T, double sigma, double rise);

} // namespace qlab::pulse
