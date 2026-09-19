// Colour helpers (see Color.hpp).
#include "Viz/Math/Color.hpp"
#include "Graphics/Colormap.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz::math {
namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Narkowicz ACES fit used by post.frag: y = x(ax + b) / (x(cx + d) + e).
constexpr double kA = 2.51, kB = 0.03, kC = 2.43, kD = 0.59, kE = 0.14;

double acesForward(double x) {
    return std::clamp(x * (kA * x + kB) / (x * (kC * x + kD) + kE), 0.0, 1.0);
}

// Solves y = aces(x) for x ≥ 0: (a − c·y)x² + (b − d·y)x − e·y = 0, positive root.
double acesInverse(double y) {
    y = std::clamp(y, 0.0, 1.0);
    if (y <= 0.0)
        return 0.0;
    const double qa = kA - kC * y, qb = kB - kD * y, qc = -kE * y;
    return (-qb + std::sqrt(qb * qb - 4.0 * qa * qc)) / (2.0 * qa);
}

double srgbChannelToLinear(double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

} // namespace

std::optional<glm::vec4> colorFromHex(std::string_view hex) {
    if (hex.empty() || hex.front() != '#' || (hex.size() != 7 && hex.size() != 9))
        return std::nullopt;
    glm::vec4 out(0.0f, 0.0f, 0.0f, 1.0f);
    for (std::size_t k = 0; k < (hex.size() - 1) / 2; ++k) {
        const int hi = hexDigit(hex[1 + 2 * k]), lo = hexDigit(hex[2 + 2 * k]);
        if (hi < 0 || lo < 0)
            return std::nullopt;
        out[static_cast<glm::length_t>(k)] = static_cast<float>(hi * 16 + lo) / 255.0f;
    }
    return out;
}

glm::vec3 sceneToDisplay(glm::vec3 scene, float exposure) {
    glm::vec3 out;
    for (glm::length_t k = 0; k < 3; ++k)
        out[k] = static_cast<float>(
            std::pow(acesForward(static_cast<double>(scene[k]) * exposure), 1.0 / 2.2));
    return out;
}

glm::vec3 displayToScene(glm::vec3 display, float exposure) {
    glm::vec3 out;
    const double e = exposure > 0.0f ? exposure : 1.0;
    for (glm::length_t k = 0; k < 3; ++k) {
        const double toned = std::pow(std::clamp(static_cast<double>(display[k]), 0.0, 1.0), 2.2);
        out[k] = static_cast<float>(acesInverse(toned) / e);
    }
    return out;
}

double relativeLuminance(glm::vec3 c) {
    return 0.2126 * srgbChannelToLinear(c.r) + 0.7152 * srgbChannelToLinear(c.g) +
           0.0722 * srgbChannelToLinear(c.b);
}

double contrastRatio(glm::vec3 a, glm::vec3 b) {
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

glm::vec3 readableOn(glm::vec3 background, glm::vec3 light, glm::vec3 dark) {
    return contrastRatio(background, light) >= contrastRatio(background, dark) ? light : dark;
}

glm::vec3 sequentialColor(double t) {
    return gfx::Colormap::get(gfx::ColormapId::Viridis)
        .sample(static_cast<float>(std::clamp(t, 0.0, 1.0)));
}

glm::vec3 divergingColor(double s) {
    return gfx::Colormap::get(gfx::ColormapId::Coolwarm)
        .sample(static_cast<float>(0.5 + 0.5 * std::clamp(s, -1.0, 1.0)));
}

} // namespace qlab::viz::math
