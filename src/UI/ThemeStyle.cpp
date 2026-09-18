// Spec 19 §1/§9 — the ImGui style is owned entirely by the theme: "no default colours, no default
// fonts, no default rounding, no default padding anywhere in the shipped application".
// Every one of `ImGuiCol_COUNT` entries is assigned here, keyed by `ImGui::GetStyleColorName` so
// that an ImGui rename (NavHighlight → NavCursor in 1.91.4) cannot silently leave a default behind.
#include "UI/Theme.hpp"
#include "Viz/Math/Color.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <imgui.h>

namespace qlab::ui {
namespace {

ImVec4 iv(const Color& c) { return ImVec4(c.r, c.g, c.b, c.a); }
Color mix(const Color& a, const Color& b, float t) {
    return Color(viz::math::mixColor(glm::vec3(a), glm::vec3(b), t), a.a + (b.a - a.a) * t);
}
Color alpha(const Color& c, float a) { return Color(c.r, c.g, c.b, a); }
// Spec 19 §5.7: alternating table rows differ by 3 % lightness.
Color lightnessStep(const Color& c, bool dark, float delta) {
    return Color(viz::math::mixColor(glm::vec3(c), dark ? glm::vec3(1.0f) : glm::vec3(0.0f), delta), c.a);
}

bool ends(std::string_view s, std::string_view suffix) { return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix; }

// The §1 token behind every ImGui colour slot.
Color colorFor(std::string_view name, const Theme& t) {
    const Color base = t[Token::BgBase], panel = t[Token::BgPanel], raised = t[Token::BgRaised];
    const Color accent = t[Token::Accent], soft = t[Token::AccentSoft], border = t[Token::Border];

    if (name == "Text") return t[Token::TextPrimary];
    if (name == "TextDisabled") return t[Token::TextDisabled];
    if (name == "WindowBg") return base;
    if (name == "ChildBg") return panel;
    if (name == "PopupBg") return raised;
    if (name == "Border") return border;
    if (name == "BorderShadow") return alpha(base, 0.0f); // spec 19 §4: 1 px borders, never a shadow
    if (name == "FrameBg") return raised;
    if (name == "FrameBgHovered") return mix(raised, accent, 0.18f);
    if (name == "FrameBgActive") return mix(raised, accent, 0.32f);
    if (name == "TitleBg") return raised;
    if (name == "TitleBgActive") return mix(raised, accent, 0.12f);
    if (name == "TitleBgCollapsed") return panel;
    if (name == "MenuBarBg") return panel;
    if (name == "ScrollbarBg") return panel;
    if (name == "ScrollbarGrab") return border;
    if (name == "ScrollbarGrabHovered") return mix(border, t[Token::TextSecondary], 0.5f);
    if (name == "ScrollbarGrabActive") return accent;
    if (name == "CheckMark" || name == "SliderGrab") return accent;
    if (name == "SliderGrabActive") return lightnessStep(accent, t.dark(), 0.2f);
    if (name == "Button") return raised;
    if (name == "ButtonHovered") return mix(raised, accent, 0.28f);
    if (name == "ButtonActive") return accent;
    if (name == "Header") return soft;
    if (name == "HeaderHovered") return mix(raised, accent, 0.35f);
    if (name == "HeaderActive") return accent;
    if (name == "Separator") return border;
    if (name == "SeparatorHovered" || name == "SeparatorActive") return accent;
    if (name == "ResizeGrip") return alpha(border, 0.6f);
    if (name == "ResizeGripHovered") return mix(border, accent, 0.5f);
    if (name == "ResizeGripActive") return accent;
    // Spec 19 §4: inactive tabs use text.secondary on the panel colour; the selected tab is raised.
    if (name == "Tab") return panel;
    if (name == "TabHovered") return mix(panel, accent, 0.25f);
    if (name == "TabSelected" || name == "TabActive") return raised;
    if (ends(name, "Overline")) return name == "TabDimmedSelectedOverline" ? border : accent;
    if (name == "TabDimmed" || name == "TabUnfocused") return base;
    if (name == "TabDimmedSelected" || name == "TabUnfocusedActive") return panel;
    if (name == "DockingPreview") return soft;
    if (name == "DockingEmptyBg") return base;
    if (name == "PlotLines" || name == "PlotHistogram") return accent;
    if (name == "PlotLinesHovered" || name == "PlotHistogramHovered") return t[Token::Warn];
    if (name == "TableHeaderBg") return raised;
    if (name == "TableBorderStrong") return border;
    if (name == "TableBorderLight") return mix(border, panel, 0.5f);
    if (name == "TableRowBg") return panel;
    if (name == "TableRowBgAlt") return lightnessStep(panel, t.dark(), 0.03f);
    if (name == "TextLink") return accent;
    if (name == "TextSelectedBg") return soft;
    if (name == "DragDropTarget") return accent;
    if (name == "NavCursor" || name == "NavHighlight") return accent; // spec 19 §8 focus ring
    if (name == "NavWindowingHighlight") return alpha(accent, 0.7f);
    if (name == "NavWindowingDimBg" || name == "ModalWindowDimBg") return alpha(base, 0.6f);
    // Any slot a future ImGui adds: the panel colour, never an ImGui default.
    return ends(name, "Bg") ? panel : t[Token::TextSecondary];
}

void fillStyle(ImGuiStyle& s, const Theme& theme, float uiScale) {
    const Metrics m = theme.metricsAt(1.0f, uiScale); // logical points (see Theme.hpp)
    // A few of the §1 values are a step plus a nudge; the nudge is a logical point too, so it
    // scales with the text like everything else.
    const auto pt = [&](float logical) { return logical * std::clamp(uiScale, 0.8f, 1.6f); };
    s.Alpha = 1.0f;
    s.DisabledAlpha = 0.55f;
    s.WindowPadding = ImVec2(m.spacing(3), m.spacing(3));
    s.WindowRounding = m.radiusMd;
    s.WindowBorderSize = m.border;
    s.WindowMinSize = ImVec2(m.spacing(6) * 4.0f, m.spacing(6) * 2.0f);
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.ChildRounding = m.radiusSm;
    s.ChildBorderSize = m.border;
    s.PopupRounding = m.radiusMd;
    s.PopupBorderSize = m.border;
    s.FramePadding = ImVec2(m.spacing(2), m.spacing(1) + pt(1.0f));
    s.FrameRounding = m.radiusSm;
    s.FrameBorderSize = m.border;
    s.ItemSpacing = ImVec2(m.spacing(2), m.spacing(1) + pt(2.0f));
    s.ItemInnerSpacing = ImVec2(m.spacing(1) + pt(2.0f), m.spacing(1));
    s.CellPadding = ImVec2(m.spacing(2), m.spacing(1));
    s.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    s.IndentSpacing = m.spacing(4);
    s.ColumnsMinSpacing = m.spacing(2);
    s.ScrollbarSize = m.spacing(3);
    s.ScrollbarRounding = m.radiusLg;
    s.GrabMinSize = m.spacing(3);
    s.GrabRounding = m.radiusSm;
    s.TabRounding = m.radiusSm; // spec 19 §4
    s.TabBorderSize = m.border;
    s.TabBarBorderSize = m.border;
    s.TabBarOverlineSize = m.border * 2.0f;
    s.TableAngledHeadersAngle = 35.0f * 3.14159265358979f / 180.0f;
    s.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    s.SeparatorTextBorderSize = m.border;
    s.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    s.SeparatorTextPadding = ImVec2(m.spacing(3), m.spacing(1));
    s.DisplayWindowPadding = ImVec2(m.spacing(4), m.spacing(4));
    s.DisplaySafeAreaPadding = ImVec2(m.spacing(1), m.spacing(1));
    s.DockingSeparatorSize = m.spacing(1) / 2.0f + pt(1.0f);
    s.MouseCursorScale = 1.0f;
    s.AntiAliasedLines = true;
    s.AntiAliasedLinesUseTex = true;
    s.AntiAliasedFill = true;
    s.CurveTessellationTol = 1.1f;
    s.CircleTessellationMaxError = 0.25f;
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        s.Colors[i] = iv(colorFor(ImGui::GetStyleColorName(i), theme));
}

bool same(const ImVec4& a, const ImVec4& b) {
    constexpr float kEps = 1.0f / 512.0f;
    return std::fabs(a.x - b.x) < kEps && std::fabs(a.y - b.y) < kEps && std::fabs(a.z - b.z) < kEps &&
           std::fabs(a.w - b.w) < kEps;
}
bool same(const ImVec2& a, const ImVec2& b) { return std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.y - b.y) < 1e-3f; }

} // namespace

void applyImGuiStyle(const Theme& theme, float uiScale) { fillStyle(ImGui::GetStyle(), theme, uiScale); }

std::vector<std::string> imguiStyleMismatches(const Theme& theme, float uiScale) {
    ImGuiStyle want{};
    fillStyle(want, theme, uiScale);
    const ImGuiStyle& have = ImGui::GetStyle();
    std::vector<std::string> out;
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        if (!same(have.Colors[i], want.Colors[i]))
            out.push_back(std::format("colour {} is {:.3f},{:.3f},{:.3f},{:.3f}, theme wants {:.3f},{:.3f},{:.3f},{:.3f}",
                                      ImGui::GetStyleColorName(i), have.Colors[i].x, have.Colors[i].y, have.Colors[i].z,
                                      have.Colors[i].w, want.Colors[i].x, want.Colors[i].y, want.Colors[i].z,
                                      want.Colors[i].w));
    const auto checkF = [&](std::string_view n, float h, float w) {
        if (std::fabs(h - w) > 1e-3f) out.push_back(std::format("{} is {}, theme wants {}", n, h, w));
    };
    const auto checkV = [&](std::string_view n, const ImVec2& h, const ImVec2& w) {
        if (!same(h, w)) out.push_back(std::format("{} is {},{}, theme wants {},{}", n, h.x, h.y, w.x, w.y));
    };
    checkF("Alpha", have.Alpha, want.Alpha);
    checkF("DisabledAlpha", have.DisabledAlpha, want.DisabledAlpha);
    checkV("WindowPadding", have.WindowPadding, want.WindowPadding);
    checkF("WindowRounding", have.WindowRounding, want.WindowRounding);
    checkF("WindowBorderSize", have.WindowBorderSize, want.WindowBorderSize);
    checkF("ChildRounding", have.ChildRounding, want.ChildRounding);
    checkF("PopupRounding", have.PopupRounding, want.PopupRounding);
    checkV("FramePadding", have.FramePadding, want.FramePadding);
    checkF("FrameRounding", have.FrameRounding, want.FrameRounding);
    checkF("FrameBorderSize", have.FrameBorderSize, want.FrameBorderSize);
    checkV("ItemSpacing", have.ItemSpacing, want.ItemSpacing);
    checkV("ItemInnerSpacing", have.ItemInnerSpacing, want.ItemInnerSpacing);
    checkV("CellPadding", have.CellPadding, want.CellPadding);
    checkF("IndentSpacing", have.IndentSpacing, want.IndentSpacing);
    checkF("ScrollbarSize", have.ScrollbarSize, want.ScrollbarSize);
    checkF("ScrollbarRounding", have.ScrollbarRounding, want.ScrollbarRounding);
    checkF("GrabMinSize", have.GrabMinSize, want.GrabMinSize);
    checkF("GrabRounding", have.GrabRounding, want.GrabRounding);
    checkF("TabRounding", have.TabRounding, want.TabRounding);
    checkF("TabBorderSize", have.TabBorderSize, want.TabBorderSize);
    return out;
}

} // namespace qlab::ui
