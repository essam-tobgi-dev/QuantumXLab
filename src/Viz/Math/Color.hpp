#pragma once
// Colour helpers of the presentation layer. Display colours are sRGB triplets in [0, 1] (what the
// theme tokens and the colormap LUTs hold). gfx::Renderer tone-maps its HDR target with the ACES
// fit and gamma 2.2 (Assets/Shaders/post.frag), so a colour that must appear EXACTLY on screen —
// a phase hue, a legend colour — is sent through `displayToScene` first (spec 21 §2.4: the same
// LUT in every view; spec 22 §4: legends must match what is drawn).
#include <glm/glm.hpp>
#include <optional>
#include <string_view>

namespace qlab::viz::math {

// "#RRGGBB" or "#RRGGBBAA" → rgba in [0, 1]; nullopt when malformed.
std::optional<glm::vec4> colorFromHex(std::string_view hex);

// post.frag: display = aces(scene · exposure)^(1/2.2) with the Narkowicz ACES fit.
glm::vec3 sceneToDisplay(glm::vec3 scene, float exposure = 1.0f);
// Exact inverse on [0, 1]: the scene-linear value gfx::Renderer turns into `display`.
glm::vec3 displayToScene(glm::vec3 display, float exposure = 1.0f);
inline glm::vec4 displayToScene(glm::vec4 display, float exposure = 1.0f) {
    return glm::vec4(displayToScene(glm::vec3(display), exposure), display.a);
}

// WCAG relative luminance of an sRGB colour and the contrast ratio (1 … 21) of two colours.
double relativeLuminance(glm::vec3 srgb);
double contrastRatio(glm::vec3 a, glm::vec3 b);
// Whichever of `light` / `dark` reads better on `background` (labels drawn on phase-coloured marks).
glm::vec3 readableOn(glm::vec3 background, glm::vec3 light, glm::vec3 dark);

// Sequential scale (viridis) for t ∈ [0, 1] and diverging scale (coolwarm) for s ∈ [−1, 1] with the
// neutral colour at s = 0 (spec 22 §4: Wigner functions and signed matrix elements).
glm::vec3 sequentialColor(double t);
glm::vec3 divergingColor(double s);

inline glm::vec3 mixColor(glm::vec3 a, glm::vec3 b, float t) { return a + (b - a) * t; }

} // namespace qlab::viz::math
