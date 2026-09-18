// Spec 19 §5.1–§5.3 — the unit-aware numeric field (see NumberField.hpp).
#include "UI/Widgets/NumberField.hpp"
#include "UI/Format.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>

namespace qlab::ui::widgets {
namespace {

// Per-field transient state: the typing buffer and the value the current gesture started from.
struct FieldState {
    bool typing = false;
    double gestureStart = 0.0;
    bool gestureOpen = false;
    std::array<char, 64> buffer{};
};
FieldState& stateOf(ImGuiID id) {
    static std::map<ImGuiID, FieldState> states;
    return states[id];
}

// A sensible coarse step when the caller gives none: 1/200 of the range, else 1 % of the magnitude.
double autoStep(double value, const FieldSpec& spec) {
    if (spec.step > 0.0) return spec.step;
    if (std::isfinite(spec.lo) && std::isfinite(spec.hi) && spec.hi > spec.lo) return (spec.hi - spec.lo) / 200.0;
    const double mag = std::fabs(value);
    return mag > 0.0 ? mag * 0.01 : 0.01;
}

format::DragModifiers mods() {
    const ImGuiIO& io = ImGui::GetIO();
    return format::DragModifiers{io.KeyShift, io.KeyAlt};
}

void commit(const UiContext& ctx, std::string_view label, const FieldSpec& spec, double* value, double from, double to) {
    if (from == to || value == nullptr) return;
    *value = to;
    if (ctx.undo == nullptr) return;
    const std::string name = spec.undoLabel.empty() ? std::string(label) : spec.undoLabel;
    ctx.undo->pushValue<double>(name, value, from, to, name);
}

// The field body: drag, scroll, double-click-to-type. Returns true when the value changed.
bool body(const UiContext& ctx, std::string_view label, double* value, const FieldSpec& spec, float width) {
    const Metrics m = ctx.metrics_px();
    const ImGuiID id = ImGui::GetID(label.data(), label.data() + label.size());
    FieldState& st = stateOf(id);
    const std::string text = format::value(*value, spec.unit, spec.digits, format::contextFor(spec.unit));
    bool changed = false;

    if (st.typing) {
        ImGui::SetNextItemWidth(width);
        ImGui::PushStyleColor(ImGuiCol_Text, iv(ctx.th()[Token::TextPrimary]));
        const bool done = ImGui::InputText("##edit", st.buffer.data(), st.buffer.size(),
                                           ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleColor();
        if (ImGui::IsItemDeactivated() || done) {
            const std::string typed(st.buffer.data());
            // Spec 19 §5.1: any registered unit of the same dimension is accepted.
            if (const auto parsed = format::parse(typed, spec.unit)) {
                commit(ctx, label, spec, value, *value, std::clamp(*parsed, spec.lo, spec.hi));
                changed = true;
            }
            st.typing = false;
        }
        return changed;
    }

    const ImVec2 size(width, ImGui::GetFrameHeight());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##field", size, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive() && !spec.readOnly;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Color fill = active ? ctx.th()[Token::AccentSoft] : ctx.th()[Token::BgRaised];
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), u32(fill), m.radiusSm);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), u32(hovered ? ctx.th()[Token::Accent] : ctx.th()[Token::Border]),
                m.radiusSm, 0, m.border);
    // Value in the primary tone (Simulator-only values in `sim_only`), unit suffix in the secondary.
    const std::size_t sp = text.rfind(' ');
    const std::string head = sp == std::string::npos ? text : text.substr(0, sp);
    const std::string unit = sp == std::string::npos ? std::string{} : text.substr(sp);
    const Color valueColor = spec.readOnly  ? ctx.th()[Token::TextDisabled]
                             : spec.simulatorOnly ? ctx.th()[Token::SimOnly]
                                                  : ctx.th()[Token::TextPrimary];
    const ImVec2 at(p.x + m.spacing(1) + 1.0f, p.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f);
    dl->AddText(at, u32(Theme::readableText(valueColor, fill)), head.c_str(), head.c_str() + head.size());
    if (!unit.empty())
        dl->AddText(ImVec2(at.x + textSize(head).x, at.y),
                    u32(Theme::readableText(ctx.th()[Token::TextSecondary], fill)), unit.c_str(),
                    unit.c_str() + unit.size());

    if (spec.readOnly) return false;

    // Spec 19 §5.3: the gesture opens an undo group so one drag is one entry.
    if (ImGui::IsItemActivated()) {
        st.gestureStart = *value;
        st.gestureOpen = true;
    }
    if (active) {
        const float dx = ImGui::GetIO().MouseDelta.x;
        if (dx != 0.0f) {
            *value = format::applyDrag(*value, dx, autoStep(*value, spec), mods(), spec.lo, spec.hi);
            changed = true;
        }
    }
    if (st.gestureOpen && ImGui::IsItemDeactivated()) {
        st.gestureOpen = false;
        const double to = *value;
        *value = st.gestureStart;
        commit(ctx, label, spec, value, st.gestureStart, to);
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        st.typing = true;
        st.buffer.fill('\0');
        std::snprintf(st.buffer.data(), st.buffer.size(), "%s", text.c_str());
        ImGui::SetKeyboardFocusHere();
    }
    // Spec 19 §5.2: scroll over a hovered field steps by the field's step.
    if (hovered) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            const double from = *value;
            const double to = std::clamp(from + wheel * autoStep(from, spec) * format::dragScale(mods()), spec.lo, spec.hi);
            commit(ctx, label, spec, value, from, to);
            changed = true;
        }
    }
    if (hovered) classTooltip(ctx, spec.cls);
    return changed;
}

float fieldWidth() {
    const float avail = ImGui::GetContentRegionAvail().x;
    return std::max(72.0f, std::min(avail, avail * 0.55f));
}

} // namespace

bool numberField(const UiContext& ctx, std::string_view label, double* value, const FieldSpec& spec) {
    if (value == nullptr) return false;
    ImGui::PushID(label.data(), label.data() + label.size());
    text(ctx, Token::TextSecondary, label);
    ImGui::SameLine();
    const bool changed = body(ctx, label, value, spec, spec.width > 0.0f ? spec.width : fieldWidth());
    if (spec.simulatorOnly) {
        ImGui::SameLine();
        simOnlyBadge(ctx);
    }
    ImGui::PopID();
    return changed;
}

bool intField(const UiContext& ctx, std::string_view label, std::int64_t* value, const FieldSpec& spec) {
    if (value == nullptr) return false;
    FieldSpec s = spec;
    if (s.step <= 0.0) s.step = 1.0;
    s.digits = 12;
    double v = static_cast<double>(*value);
    const bool changed = numberField(ctx, label, &v, s);
    if (changed) *value = static_cast<std::int64_t>(std::llround(v));
    return changed;
}

void readout(const UiContext& ctx, std::string_view label, double si, const FieldSpec& spec) {
    FieldSpec s = spec;
    s.readOnly = true;
    double v = si;
    ImGui::PushID(label.data(), label.data() + label.size());
    text(ctx, Token::TextSecondary, label);
    ImGui::SameLine();
    {
        // Spec 19 §1: readouts are JetBrains Mono with tabular figures.
        FontScope f(*ctx.fonts, FontRole::Readout);
        body(ctx, label, &v, s, s.width > 0.0f ? s.width : fieldWidth());
    }
    ImGui::SameLine();
    fidelityBadge(ctx, spec.cls);
    if (spec.simulatorOnly) {
        ImGui::SameLine();
        simOnlyBadge(ctx);
    }
    ImGui::PopID();
}

bool combo(const UiContext& ctx, std::string_view label, int* index, std::span<const std::string_view> items,
           std::string_view undoLabel) {
    if (index == nullptr || items.empty()) return false;
    ImGui::PushID(label.data(), label.data() + label.size());
    text(ctx, Token::TextSecondary, label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fieldWidth());
    const int current = std::clamp(*index, 0, static_cast<int>(items.size()) - 1);
    bool changed = false;
    const std::string preview(items[static_cast<std::size_t>(current)]);
    if (ImGui::BeginCombo("##combo", preview.c_str())) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const std::string item(items[static_cast<std::size_t>(i)]);
            if (ImGui::Selectable(item.c_str(), i == current) && i != current) {
                const std::string name = undoLabel.empty() ? std::string(label) : std::string(undoLabel);
                *index = i;
                if (ctx.undo != nullptr) ctx.undo->pushValue<int>(name, index, current, i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

bool checkbox(const UiContext& ctx, std::string_view label, bool* value, std::string_view undoLabel) {
    if (value == nullptr) return false;
    const std::string owned(label);
    const bool before = *value;
    const bool clicked = ImGui::Checkbox(owned.c_str(), value);
    if (clicked && ctx.undo != nullptr) {
        const std::string name = undoLabel.empty() ? owned : std::string(undoLabel);
        ctx.undo->pushValue<bool>(name, value, before, *value);
    }
    return clicked;
}

} // namespace qlab::ui::widgets
