// Spec 21 §2.4 — phase colour wheel over the cyclic twilight LUT (see Phase.hpp).
#include "Viz/Math/Phase.hpp"
#include "Graphics/Colormap.hpp"
#include "Viz/Math/Format.hpp"
#include <cmath>
#include <numbers>

namespace qlab::viz::math {
namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
} // namespace

double wrapPhase(double phi) {
    if (!std::isfinite(phi)) return 0.0;
    double w = std::fmod(phi, kTwoPi);           // (−2π, 2π)
    if (w > kPi) w -= kTwoPi;
    else if (w <= -kPi) w += kTwoPi;             // −π itself folds to +π: the range is (−π, π]
    return w;
}

double phaseOf(num::Complex a) {
    if (a.real() == 0.0 && a.imag() == 0.0) return 0.0;
    double p = std::arg(a);                      // [−π, π]; −π only for a negative real with −0 imaginary
    if (p <= -kPi) p = kPi;
    return p;
}

double phaseHue(double phi) { return (wrapPhase(phi) + kPi) / kTwoPi; }

std::span<const glm::vec3> phaseLut() {
    static const std::vector<glm::vec3> lut = gfx::Colormap::get(gfx::ColormapId::Twilight).lut(kPhaseLutEntries);
    return lut;
}

glm::vec3 phaseColorAtHue(double hue) {
    const auto lut = phaseLut();
    const double n = static_cast<double>(lut.size());
    double x = (hue - std::floor(hue)) * n;      // cyclic: H = 1 lands on entry 0
    if (x >= n) x = 0.0;
    const std::size_t i0 = static_cast<std::size_t>(x) % lut.size();
    const std::size_t i1 = (i0 + 1) % lut.size();
    const float f = static_cast<float>(x - std::floor(x));
    return lut[i0] + (lut[i1] - lut[i0]) * f;
}

std::vector<PhaseTick> phaseLegend(int count) {
    std::vector<PhaseTick> ticks;
    if (count < 1) return ticks;
    for (int k = 0; k < count; ++k) {
        const double phi = wrapPhase(kTwoPi * static_cast<double>(k) / static_cast<double>(count));
        ticks.push_back({phi, formatAngle(phi), phaseColor(phi)});
    }
    return ticks;
}

std::string formatAngle(double radians) {
    if (!std::isfinite(radians)) return formatSig(radians);
    if (std::abs(radians) < 1e-9) return "0";
    // Spec 21 §3.13: a multiple pπ/q with q ≤ 16 when within 1e-9; ascending q gives lowest terms.
    for (int q = 1; q <= 16; ++q) {
        const double pReal = radians * static_cast<double>(q) / kPi;
        const double p = std::round(pReal);
        if (p == 0.0 || std::abs(p) > 1e6) continue;
        if (std::abs(radians - p * kPi / static_cast<double>(q)) >= 1e-9) continue;
        const long long ip = static_cast<long long>(p);
        std::string s = ip < 0 ? "-" : "";
        if (std::llabs(ip) != 1) s += std::to_string(std::llabs(ip));
        s += "\xCF\x80"; // π
        if (q != 1) s += "/" + std::to_string(q);
        return s;
    }
    return formatSig(radians, 4) + " rad";
}

} // namespace qlab::viz::math
