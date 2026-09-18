#include "Pulse/Physics.hpp"
#include <cmath>
#include <numbers>

namespace qlab::pulse::physics {
namespace {
constexpr double kPi = std::numbers::pi;

// x − sin x without the cancellation of the direct difference at small x.
double xMinusSin(double x) {
    if (std::abs(x) >= 0.1) return x - std::sin(x);
    const double x2 = x * x;
    return x * x2 * (1.0 / 6.0 - x2 * (1.0 / 120.0 - x2 * (1.0 / 5040.0 - x2 / 362880.0)));
}
} // namespace

double rotationAngle(double kappaD, double amplitude, double envelopeAreaSeconds) {
    return kappaD * amplitude * envelopeAreaSeconds;
}
double rotationAngle(double kappaD, const Waveform& wf) {
    // The envelope's own amplitude/phase are already in area(); the rotation uses |∫e dt|.
    return kappaD * std::abs(wf.area());
}
double amplitudeForAngle(double theta, double kappaD, double envelopeAreaSeconds) {
    const double denom = kappaD * envelopeAreaSeconds;
    return denom == 0.0 ? 0.0 : theta / denom;
}
double amplitudeForAngle(double theta, double kappaD, const Waveform& wf) {
    Waveform unit = wf;
    unit.amplitude = 1.0;
    unit.phase = 0.0;
    return amplitudeForAngle(theta, kappaD, std::abs(unit.area()));
}

double crZxRate(double j, double omegaC, double delta, double alphaC) {
    if (delta == 0.0 || delta + alphaC == 0.0) return 0.0;
    return -(j * omegaC / delta) * (alphaC / (delta + alphaC));
}
double crIxRate(double j, double omegaC, double delta, double alphaC) {
    if (delta + alphaC == 0.0) return 0.0;
    return -j * omegaC / (delta + alphaC);
}
double crAmplitudeForAngle(double theta, double j, double delta, double alphaC, double kappaD,
                           double envelopeAreaSeconds) {
    // ν_ZX ∝ Ω_c = κ_d·A, so ∫ν_ZX dt = rate(Ω_c = κ_d)·A·area.
    const double perUnit = crZxRate(j, kappaD, delta, alphaC) * envelopeAreaSeconds;
    return perUnit == 0.0 ? 0.0 : theta / perUnit;
}

double msLoopDuration(double delta, int K) {
    return delta == 0.0 ? 0.0 : 2.0 * kPi * K / std::abs(delta);
}
double msDetuningForDuration(double tau, int K) {
    return tau == 0.0 ? 0.0 : 2.0 * kPi * K / std::abs(tau);
}
double msAngle(double etaI, double etaJ, double omega, double tau, double delta) {
    if (delta == 0.0) return 0.0;
    return -etaI * etaJ * omega * omega * tau / delta;
}
double msAmplitudeForAngle(double theta, double etaI, double etaJ, double tau, double delta) {
    const double denom = etaI * etaJ * tau;
    if (denom == 0.0) return 0.0;
    return std::sqrt(std::abs(theta * delta) / std::abs(denom));
}
double msMaxEntanglingDetuning(double eta, double omega, int K) {
    return 2.0 * eta * omega * std::sqrt(static_cast<double>(K));
}

MsIntegrals msIntegrals(std::span<const double> g, double dt, double delta, double t0) {
    MsIntegrals out;
    if (g.empty() || dt <= 0.0) return out;
    if (delta == 0.0) { // no detuning: a pure displacement, α = −i∫g dt, Φ = 0
        double area = 0.0;
        for (double v : g) area += v * dt;
        out.alpha = Complex(0.0, -area);
        return out;
    }
    // Segment integrals in the half-angle forms that stay exact for δ·dt ≪ 1.
    const double s = 2.0 * std::sin(0.5 * delta * dt) / delta; // ∫_seg e^{iδt} dt = s·e^{iδ t_mid}
    const double self = xMinusSin(delta * dt) / (delta * delta);  // ∫∫_{t'<t, same seg} sin δ(t−t')
    Complex alpha{};
    double cc = 0.0, ss = 0.0, phi = 0.0; // cc = ∫₀^{t_k} g cos δt', ss = ∫₀^{t_k} g sin δt'
    for (std::size_t k = 0; k < g.size(); ++k) {
        const double mid = delta * (t0 + (static_cast<double>(k) + 0.5) * dt);
        const double cInt = s * std::cos(mid), sInt = s * std::sin(mid);
        alpha += g[k] * Complex(cInt, sInt);
        phi += g[k] * g[k] * self + g[k] * (sInt * cc - cInt * ss);
        cc += g[k] * cInt;
        ss += g[k] * sInt;
    }
    // α = −i ∫ g e^{iδt} dt (T06 (11.1) written with the segment integral).
    out.alpha = Complex(0.0, -1.0) * alpha;
    out.phi = phi;
    return out;
}

} // namespace qlab::pulse::physics
