#pragma once
// Spec 19 §5 — the shared widget vocabulary. Buttons come in exactly three kinds (primary,
// secondary, danger; §5.6), every value that has a fidelity class shows its badge (§5.4), every
// Simulator-only value carries the `sim_only` colour (§5.5), and tables alternate rows at a 3 %
// lightness delta with virtual scrolling above 200 rows (§5.7).
#include "Data/Fidelity.hpp"
#include "UI/Context.hpp"
#include <imgui.h>
#include <string_view>

namespace qlab::ui::widgets {

// ---- colour conversion
inline ImVec4 iv(const Color& c) {
    return ImVec4(c.r, c.g, c.b, c.a);
}
inline ImU32 u32(const Color& c) {
    return ImGui::ColorConvertFloat4ToU32(iv(c));
}
inline Color toColor(const ImVec4& c) {
    return Color(c.x, c.y, c.z, c.w);
}

// ---- text
void text(const UiContext& ctx, Token token, std::string_view s);
void text(const UiContext& ctx, const Color& color, std::string_view s);
void textWrapped(const UiContext& ctx, Token token, std::string_view s);
// Label in `text.secondary`, value in `text.primary`, on one line.
void labelled(const UiContext& ctx, std::string_view label, std::string_view value);
ImVec2 textSize(std::string_view s);
// Width of `s` in `role`'s face at its logical size — for laying a column out to fit.
float textWidth(const UiContext& ctx, FontRole role, std::string_view s);

// ---- badges (spec 19 §5.4, §5.5)
// Rounded chip: the text in `color`, on a 20 % fill of the same hue, on the current line.
void badge(const UiContext& ctx, std::string_view label, const Color& color);
void fidelityBadge(const UiContext& ctx, data::FidelityClass cls);
void simOnlyBadge(const UiContext& ctx);
// The §5.4 rule: the class badge appears on hover for a value, always in the Inspector.
void classTooltip(const UiContext& ctx, data::FidelityClass cls);

// ---- buttons (spec 19 §5.6 — no other kinds exist)
bool primaryButton(const UiContext& ctx, std::string_view label, ImVec2 size = ImVec2(0, 0));
bool secondaryButton(const UiContext& ctx, std::string_view label, ImVec2 size = ImVec2(0, 0));
bool dangerButton(const UiContext& ctx, std::string_view label, ImVec2 size = ImVec2(0, 0));
// A small square toggle used by the overlay and layer controls.
bool toggleChip(const UiContext& ctx, std::string_view label, bool* value);

// ---- structure
void sectionHeader(const UiContext& ctx, std::string_view label);
// One "field: value unit" row of a spec sheet, with the class badge and an optional typical-range
// bar (spec 19 §3 Inspector). `typicalLo/Hi` equal disables the bar.
void specRow(const UiContext& ctx, std::string_view field, std::string_view value,
             data::FidelityClass cls, bool simulatorOnly, double si, double typicalLo,
             double typicalHi);
// Slim progress bar of the top bar (spec 19 §5.8). `fraction` < 0 draws an indeterminate sweep.
void progressBar(const UiContext& ctx, double fraction, std::string_view overlay,
                 float width = -1.0f);
// Centred secondary message for an empty panel.
void placeholder(const UiContext& ctx, std::string_view message);
// "?" that shows `help` on hover (spec 19 §6 delay).
void helpMarker(const UiContext& ctx, std::string_view help);
// Spec 19 §2: a greyed control whose tooltip explains the Physical-lab restriction.
void simOnlyDisabledTooltip(const UiContext& ctx);

// Spec 19 §5.7 table flags: sortable, resizable, alternating rows, borders from the theme.
ImGuiTableFlags tableFlags(bool sortable = true);
// Virtual scrolling is mandatory above this many rows (spec 19 §5.7).
inline constexpr int kVirtualRowThreshold = 200;

// RAII: grey out everything inside and show the Physical-lab tooltip on hover.
class DisabledScope {
  public:
    DisabledScope(const UiContext& ctx, bool disabled, std::string_view reason = {});
    ~DisabledScope();
    DisabledScope(const DisabledScope&) = delete;
    DisabledScope& operator=(const DisabledScope&) = delete;

  private:
    const UiContext* ctx_;
    bool on_;
    std::string_view reason_;
};

} // namespace qlab::ui::widgets
