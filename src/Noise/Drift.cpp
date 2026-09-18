// Spec 08 §2.6, T04 (8.2) — quasi-static detuning statistics and the Gauss–Hermite shot average.
#include "Noise/Drift.hpp"
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

namespace qlab::noise {

double driftSigmaFromT2Star(double t2StarS) {
    if (!(t2StarS > 0.0) || !std::isfinite(t2StarS)) return 0.0;
    return std::numbers::sqrt2 / (2.0 * std::numbers::pi * t2StarS);
}

double t2StarFromDriftSigma(double sigmaHz) {
    if (!(sigmaHz > 0.0)) return std::numeric_limits<double>::infinity();
    return std::numbers::sqrt2 / (2.0 * std::numbers::pi * sigmaHz);
}

double driftSigmaFromCalibration(double t2StarS, double t2S) {
    return (t2StarS > 0.0 && t2StarS < t2S) ? driftSigmaFromT2Star(t2StarS) : 0.0;
}

double sampleDetuning(double sigmaHz, core::Random& rng) {
    return sigmaHz > 0.0 ? rng.normal(0.0, sigmaHz) : 0.0;
}

Result<std::vector<DetuningNode>> gaussHermiteDetunings(double sigmaHz, std::size_t nodes) {
    if (nodes == 0 || nodes > 64) return fail(err::InvalidParameter, std::format("Gauss-Hermite rule needs 1..64 nodes, {} requested", nodes));
    if (!(sigmaHz >= 0.0) || !std::isfinite(sigmaHz)) return fail(err::InvalidParameter, "detuning sigma must be finite and >= 0");
    if (sigmaHz == 0.0) return std::vector<DetuningNode>{{0.0, 1.0}};
    // Roots of H_n by Newton iteration on the orthonormal recurrence (weight e^{−x²}):
    //   h_0 = π^{−1/4},  h_{j+1} = x √(2/(j+1)) h_j − √(j/(j+1)) h_{j−1},  h_n' = √(2n) h_{n−1},
    //   w_i = 2/(h_n'(x_i))².  Then δ_i = √2 σ x_i and the normal-law weight is w_i/√π.
    const std::size_t n = nodes;
    const double nn = static_cast<double>(n);
    std::vector<double> x(n), w(n);
    const std::size_t half = (n + 1) / 2;
    double z = 0.0;
    for (std::size_t i = 0; i < half; ++i) {
        if (i == 0) z = std::sqrt(2.0 * nn + 1.0) - 1.85575 * std::pow(2.0 * nn + 1.0, -1.0 / 6.0);
        else if (i == 1) z -= 1.14 * std::pow(nn, 0.426) / z;
        else if (i == 2) z = 1.86 * z - 0.86 * x[0];
        else if (i == 3) z = 1.91 * z - 0.91 * x[1];
        else z = 2.0 * z - x[i - 2];
        double derivative = 1.0;
        bool converged = false;
        for (int iter = 0; iter < 100 && !converged; ++iter) {
            double h = std::pow(std::numbers::pi, -0.25), hPrev = 0.0;
            for (std::size_t j = 0; j < n; ++j) {
                const double jj = static_cast<double>(j);
                const double next = z * std::sqrt(2.0 / (jj + 1.0)) * h - std::sqrt(jj / (jj + 1.0)) * hPrev;
                hPrev = h;
                h = next;
            }
            derivative = std::sqrt(2.0 * nn) * hPrev;
            const double step = h / derivative;
            z -= step;
            converged = std::abs(step) <= 1e-15 * std::max(1.0, std::abs(z));
        }
        if (!converged) return fail(ErrorCode::Internal, "Gauss-Hermite Newton iteration did not converge");
        x[i] = z;
        x[n - 1 - i] = -z;
        w[i] = w[n - 1 - i] = 2.0 / (derivative * derivative);
    }
    if (n % 2 == 1) x[half - 1] = 0.0; // the middle root is exactly zero
    std::vector<DetuningNode> out(n);
    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) total += w[i];
    for (std::size_t i = 0; i < n; ++i)
        out[i] = {std::numbers::sqrt2 * sigmaHz * x[n - 1 - i], w[n - 1 - i] / total}; // ascending detuning
    return out;
}

} // namespace qlab::noise
