#pragma once
// Spec 21 §2.4 — the phase colour wheel. Phase φ = arg a ∈ (−π, π] maps to the position
// H = (φ + π)/(2π) on the 256-entry LUT sampled from the cyclic `twilight` colormap of
// gfx::Colormap. Every view, the pulse viewer (frame phase) and the 3D chip use this one map.
#include "Numerics/Types.hpp"
#include <glm/glm.hpp>
#include <span>
#include <string>
#include <vector>

namespace qlab::viz::math {

inline constexpr int kPhaseLutEntries = 256;

// φ folded into (−π, π].
double wrapPhase(double phi);
// arg a ∈ (−π, π]; 0 for a = 0 (a vanishing amplitude has no phase; views hide it by magnitude).
double phaseOf(num::Complex a);
// H = (wrapPhase(φ) + π)/(2π) ∈ (0, 1]. The map is cyclic: H = 1 (φ = π) is the same colour as
// H → 0 (φ → −π).
double phaseHue(double phi);

// The 256-entry LUT (entry i is twilight at i/256), built once.
std::span<const glm::vec3> phaseLut();
// Display (sRGB) colour at position H, linearly interpolated between LUT entries with wrap-around.
glm::vec3 phaseColorAtHue(double hue);
inline glm::vec3 phaseColor(double phi) {
    return phaseColorAtHue(phaseHue(phi));
}
inline glm::vec3 phaseColor(num::Complex a) {
    return phaseColorAtHue(phaseHue(phaseOf(a)));
}

// One tick of the phase-wheel legend (spec 21 §2.4, §4: hue is always paired with a label).
struct PhaseTick {
    double phi = 0.0;
    std::string label; // "0", "π/2", "π", "−π/2"
    glm::vec3 color{0.0f};
};
// `count` equally spaced ticks starting at φ = 0 (count = 4: 0, π/2, π, −π/2).
std::vector<PhaseTick> phaseLegend(int count = 4);

// A phase as a multiple of π when it is one (|φ − pπ/q| < 1e-9, q ≤ 16), else radians with 4
// significant figures: "0", "π/4", "−3π/8", "1.234 rad" (the rule of spec 21 §3.13 for angles).
std::string formatAngle(double radians);

} // namespace qlab::viz::math
