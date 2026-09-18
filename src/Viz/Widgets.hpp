#pragma once
// Spec 21 §4 — pieces every view shares: the header (fidelity badge, observability badge, snapshot
// status, export, "?" theory link), the hover readout card, the phase-wheel legend and the
// min/max colour-scale legend (spec 22 §4: no scalar is encoded by colour without a legend).
// Signatures are ImGui-free; the implementations draw with ImGui inside the current window.
#include "Viz/IStateView.hpp"
#include "Data/Fidelity.hpp"
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <string_view>

namespace qlab::viz::widgets {

// Small rounded label: the text in `color` on a faint fill of the same hue. Same line as what follows.
void badge(const DrawContext& ctx, std::string_view text, const glm::vec4& color);
// "Exact" / "Numerical" / … in the class colour; "Simulator-only" in the sim_only colour.
void fidelityBadge(const DrawContext& ctx, data::FidelityClass cls);
void observabilityBadge(const DrawContext& ctx, Observability o);

// The header row of a view. `fidelity` is the class of what is currently shown.
void header(DrawContext& ctx, IStateView& view, data::FidelityClass fidelity, const Rect& bodyScreenRect);

// Hover card next to the mouse: title, then label/value rows with a class dot (spec 21 §4).
void readoutCard(const DrawContext& ctx, const HitResult& hit);

// Centred grey message for a view without data ("Run a program to see the state").
void placeholder(const DrawContext& ctx, std::string_view message);

// Phase wheel (spec 21 §2.4): a ring in the twilight LUT with ticks at 0, π/2, π, −π/2, drawn at
// `center` (screen coordinates) with outer radius `radius`.
void phaseWheel(const DrawContext& ctx, glm::vec2 center, float radius);
// Vertical colour bar with min/max labels and a title, at `screenRect`. `colorAt(t)` gives the
// display colour for t ∈ [0, 1] (bottom → top).
void colorBar(const DrawContext& ctx, const Rect& screenRect, const std::function<glm::vec3(double)>& colorAt,
              std::string_view minLabel, std::string_view maxLabel, std::string_view title);

// Height of the header row in ImGui units (0 when the context hides it).
float headerHeight(const DrawContext& ctx);

} // namespace qlab::viz::widgets
