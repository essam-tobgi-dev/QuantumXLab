#pragma once
// Spec 19 §1 — the theme tokens the views draw with. Viz sits below UI in the layer graph, so it
// cannot read `ui::Theme`; the UI fills this plain struct from its theme (or Viz loads the same
// `Assets/Lang/theme.json` itself). No view holds a colour of its own: every mark is a token, a
// per-qubit colour, or a colormap value.
#include "Core/Error.hpp"
#include "Core/Json.hpp"
#include "Data/Fidelity.hpp"
#include <array>
#include <glm/glm.hpp>
#include <string_view>

namespace qlab::viz {

struct VizTheme {
    // Display (sRGB) colours, alpha in .a.
    glm::vec4 bgPanel, bgRaised, border;
    glm::vec4 textPrimary, textSecondary, textDisabled;
    glm::vec4 accent, accentSoft;
    glm::vec4 ok, warn, err;
    glm::vec4 simOnly;                      // Simulator-only badge (spec 19 §5.5)
    std::array<glm::vec4, 5> fidelity{};    // class.exact … class.illustrative (spec 00 §5)
    std::array<glm::vec4, 8> qubits{};      // Okabe–Ito palette (spec 19 §1)
    float radiusSm = 3.0f, radiusMd = 5.0f; // px at 1× (multiplied by the DPI scale when drawn)
    float space = 8.0f;                     // space.2
    float fontPx = 13.0f, fontSmallPx = 12.0f;
    bool dark = true;

    // Badge colour of a fidelity class.
    const glm::vec4& fidelityColor(data::FidelityClass c) const {
        return fidelity[static_cast<std::size_t>(c)];
    }
    // Spec 19 §1: qubit i uses qubits[i % 8]; each wrap darkens (dark theme: lightens) by 12 %.
    glm::vec4 qubitColor(std::uint32_t qubit) const;
    // Theory overlays are dashed in the secondary text colour (spec 22 §3); fits use the accent.
    const glm::vec4& theory() const { return textSecondary; }
    // Neutral mark colour for phase-less data (marginals, unselected graph nodes).
    glm::vec4 neutral() const;

    // Built-in copies of the spec 19 §1 tables, used when the asset cannot be read.
    static VizTheme fallbackDark();
    static VizTheme fallbackLight();
    // From the `data` object of Assets/Lang/theme.json; `palette` is "dark" or "light". Missing
    // tokens keep their fallback value; a malformed colour is an error naming the token.
    static Result<VizTheme> fromJson(const core::Json& themeData, std::string_view palette);
    // Loads Assets/Lang/theme.json (envelope kind "ui.theme").
    static Result<VizTheme> load(std::string_view palette = "dark");
};

} // namespace qlab::viz
