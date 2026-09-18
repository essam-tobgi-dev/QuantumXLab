// Spec 19 §5 — the shared widget vocabulary (see Widgets.hpp).
#include "UI/Widgets/Widgets.hpp"
#include "Data/Fidelity.hpp"
#include "UI/Format.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace qlab::ui::widgets {
namespace {

void textUnformatted(std::string_view s) { ImGui::TextUnformatted(s.data(), s.data() + s.size()); }

// Every label is emitted through the contrast helper, so the §8 promise (≥ 4.5:1) holds for what is
// actually drawn even where the pinned §1 hue does not clear it on its own.
Color readable(const UiContext& ctx, const Color& fg) {
    return Theme::readableText(fg, ctx.th()[Token::BgPanel]);
}

bool buttonWith(const UiContext& ctx, std::string_view label, ImVec2 size, const Color& fill, const Color& border,
                const Color& label_color) {
    const Metrics m = ctx.metrics_px();
    ImGui::PushStyleColor(ImGuiCol_Button, iv(fill));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, iv(Color(glm::vec3(fill) * 1.12f, std::max(fill.a, 0.25f))));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, iv(Color(glm::vec3(fill) * 0.88f, std::max(fill.a, 0.4f))));
    ImGui::PushStyleColor(ImGuiCol_Border, iv(border));
    ImGui::PushStyleColor(ImGuiCol_Text, iv(label_color));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, m.radiusSm);
    const std::string owned(label);
    const bool pressed = ImGui::Button(owned.c_str(), size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    return pressed;
}

} // namespace

// ---------------------------------------------------------------- text

void text(const UiContext& ctx, Token token, std::string_view s) { text(ctx, ctx.th()[token], s); }

void text(const UiContext& ctx, const Color& color, std::string_view s) {
    ImGui::PushStyleColor(ImGuiCol_Text, iv(readable(ctx, color)));
    textUnformatted(s);
    ImGui::PopStyleColor();
}

void textWrapped(const UiContext& ctx, Token token, std::string_view s) {
    ImGui::PushStyleColor(ImGuiCol_Text, iv(readable(ctx, ctx.th()[token])));
    ImGui::PushTextWrapPos(0.0f);
    textUnformatted(s);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void labelled(const UiContext& ctx, std::string_view label, std::string_view value) {
    text(ctx, Token::TextSecondary, label);
    ImGui::SameLine();
    text(ctx, Token::TextPrimary, value);
}

ImVec2 textSize(std::string_view s) { return ImGui::CalcTextSize(s.data(), s.data() + s.size()); }

float textWidth(const UiContext& ctx, FontRole role, std::string_view s) {
    if (ctx.fonts == nullptr) return textSize(s).x;
    FontScope f(*ctx.fonts, role);      // pushes the face AND its size, so the measure is honest
    return textSize(s).x;
}

// ---------------------------------------------------------------- badges

void badge(const UiContext& ctx, std::string_view label, const Color& color) {
    const Metrics m = ctx.metrics_px();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pad(m.spacing(1) + 1.0f, 1.0f);
    const ImVec2 size = textSize(label);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x + 2.0f * pad.x, p0.y + size.y + 2.0f * pad.y);
    dl->AddRectFilled(p0, p1, u32(Color(glm::vec3(color), 0.20f)), m.radiusSm);
    dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y), u32(Theme::readableText(color, ctx.th()[Token::BgPanel])),
                label.data(), label.data() + label.size());
    ImGui::Dummy(ImVec2(p1.x - p0.x, p1.y - p0.y));
}

void fidelityBadge(const UiContext& ctx, data::FidelityClass cls) {
    badge(ctx, data::fidelityName(cls), ctx.th().fidelityColor(cls));
    classTooltip(ctx, cls);
}

void simOnlyBadge(const UiContext& ctx) {
    badge(ctx, ctx.text("sim_only.badge"), ctx.th()[Token::SimOnly]);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ctx.ui(420.0f)); // spec 19 §6 max width
        textUnformatted(ctx.text("sim_only.tooltip"));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void classTooltip(const UiContext& ctx, data::FidelityClass cls) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) || !ImGui::BeginTooltip()) return;
    const std::string key = std::string("fidelity_class.tooltip.") + std::string(data::fidelityName(cls));
    text(ctx, ctx.th().fidelityColor(cls), data::fidelityName(cls));
    ImGui::PushTextWrapPos(ctx.ui(420.0f));
    textUnformatted(ctx.text(key));
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// ---------------------------------------------------------------- buttons

bool primaryButton(const UiContext& ctx, std::string_view label, ImVec2 size) {
    const Color accent = ctx.th()[Token::Accent];
    return buttonWith(ctx, label, size, accent, accent, Theme::readableText(ctx.th()[Token::TextPrimary], accent));
}

bool secondaryButton(const UiContext& ctx, std::string_view label, ImVec2 size) {
    return buttonWith(ctx, label, size, Color(glm::vec3(ctx.th()[Token::BgRaised]), 0.0f), ctx.th()[Token::Border],
                      readable(ctx, ctx.th()[Token::TextPrimary]));
}

bool dangerButton(const UiContext& ctx, std::string_view label, ImVec2 size) {
    const Color err = ctx.th()[Token::Err];
    return buttonWith(ctx, label, size, Color(glm::vec3(err), 0.0f), err, readable(ctx, err));
}

bool toggleChip(const UiContext& ctx, std::string_view label, bool* value) {
    if (value == nullptr) return false;
    const Color on = ctx.th()[Token::Accent];
    const bool pressed = *value
                             ? buttonWith(ctx, label, ImVec2(0, 0), Color(glm::vec3(on), 0.25f), on, readable(ctx, on))
                             : secondaryButton(ctx, label);
    if (pressed) *value = !*value;
    return pressed;
}

// ---------------------------------------------------------------- structure

void sectionHeader(const UiContext& ctx, std::string_view label) {
    const Metrics m = ctx.metrics_px();
    ImGui::Dummy(ImVec2(0.0f, m.spacing(1)));
    {
        FontScope f(*ctx.fonts, FontRole::Strong);
        text(ctx, Token::TextPrimary, label);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y), u32(ctx.th()[Token::Border]),
                m.border);
    ImGui::Dummy(ImVec2(0.0f, m.spacing(1)));
}

void specRow(const UiContext& ctx, std::string_view field, std::string_view value, data::FidelityClass cls,
             bool simulatorOnly, double si, double typicalLo, double typicalHi) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    text(ctx, Token::TextSecondary, field);
    ImGui::TableNextColumn();
    {
        FontScope f(*ctx.fonts, FontRole::Readout);
        text(ctx, simulatorOnly ? ctx.th()[Token::SimOnly] : ctx.th()[Token::TextPrimary], value);
    }
    ImGui::TableNextColumn();
    fidelityBadge(ctx, cls);
    if (simulatorOnly) {
        ImGui::SameLine();
        simOnlyBadge(ctx);
    }
    ImGui::TableNextColumn();
    // Spec 19 §3: the typical range as a faint bar with the live value marked on it.
    if (typicalHi > typicalLo && std::isfinite(si)) {
        const Metrics m = ctx.metrics_px();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = std::max(24.0f, ImGui::GetContentRegionAvail().x);
        const float h = ImGui::GetTextLineHeight() * 0.35f;
        const ImVec2 a(p.x, p.y + (ImGui::GetTextLineHeight() - h) * 0.5f);
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), u32(Color(glm::vec3(ctx.th()[Token::Border]), 0.6f)),
                          m.radiusSm);
        const auto t = static_cast<float>(std::clamp((si - typicalLo) / (typicalHi - typicalLo), 0.0, 1.0));
        const float x = a.x + t * w;
        dl->AddRectFilled(ImVec2(x - m.border, a.y - h * 0.5f), ImVec2(x + m.border, a.y + h * 1.5f),
                          u32(ctx.th()[Token::Accent]));
        ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
    }
}

void progressBar(const UiContext& ctx, double fraction, std::string_view overlay, float width) {
    const Metrics m = ctx.metrics_px();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = width > 0.0f ? width : std::max(64.0f, ImGui::GetContentRegionAvail().x);
    const float h = std::max(3.0f, m.spacing(1) * 0.75f);
    const ImVec2 a(p.x, p.y + (ImGui::GetTextLineHeight() - h) * 0.5f);
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), u32(ctx.th()[Token::BgRaised]), m.radiusSm);
    if (fraction >= 0.0) {
        const auto f = static_cast<float>(std::clamp(fraction, 0.0, 1.0));
        dl->AddRectFilled(a, ImVec2(a.x + w * f, a.y + h), u32(ctx.th()[Token::Accent]), m.radiusSm);
    } else {
        // Indeterminate: a short sweep, held still under the reduced-motion setting (spec 19 §8).
        const float span = w * 0.25f;
        const auto phase = ctx.reducedMotion ? 0.0f : static_cast<float>(std::fmod(ctx.timeS * 0.6, 1.0));
        const float x0 = a.x + (w + span) * phase - span;
        dl->AddRectFilled(ImVec2(std::max(a.x, x0), a.y), ImVec2(std::min(a.x + w, x0 + span), a.y + h),
                          u32(ctx.th()[Token::Accent]), m.radiusSm);
    }
    ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
    if (!overlay.empty()) {
        ImGui::SameLine();
        text(ctx, Token::TextSecondary, overlay);
    }
}

void placeholder(const UiContext& ctx, std::string_view message) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 size = textSize(message);
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + std::max(0.0f, (avail.x - size.x) * 0.5f),
                               ImGui::GetCursorPos().y + std::max(0.0f, (avail.y - size.y) * 0.5f)));
    text(ctx, Token::TextSecondary, message);
}

void helpMarker(const UiContext& ctx, std::string_view help) {
    text(ctx, Token::TextSecondary, "?");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ctx.ui(420.0f));
        textUnformatted(help);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void simOnlyDisabledTooltip(const UiContext& ctx) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)) return;
    if (!ImGui::BeginTooltip()) return;
    ImGui::PushTextWrapPos(ctx.ui(420.0f));
    textUnformatted(ctx.text("app.physical_lab_tooltip"));
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

ImGuiTableFlags tableFlags(bool sortable) {
    ImGuiTableFlags f = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (sortable) f |= ImGuiTableFlags_Sortable;
    return f;
}

DisabledScope::DisabledScope(const UiContext& ctx, bool disabled, std::string_view reason)
    : ctx_(&ctx), on_(disabled), reason_(reason) {
    if (on_) ImGui::BeginDisabled();
}

DisabledScope::~DisabledScope() {
    if (!on_) return;
    ImGui::EndDisabled();
    if (reason_.empty()) return;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal) &&
        ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ctx_->ui(420.0f));
        ImGui::TextUnformatted(reason_.data(), reason_.data() + reason_.size());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace qlab::ui::widgets
