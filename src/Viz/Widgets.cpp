// Spec 21 §4 — shared header, badges, readout card and legends (see Widgets.hpp).
#include "Viz/Widgets.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Math/Color.hpp"
#include "Viz/Math/Phase.hpp"
#include <cmath>
#include <numbers>

namespace qlab::viz::widgets {
using namespace detail;

void badge(const DrawContext& ctx, std::string_view label, const glm::vec4& color) {
    const float s = ctx.theme ? ctx.theme->radiusSm : 3.0f;
    const ImVec2 size = textSize(label);
    const ImVec2 pad(6.0f, 2.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x + 2 * pad.x, p0.y + size.y + 2 * pad.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, toU32(glm::vec3(color), 0.16f), s);
    dl->AddRect(p0, p1, toU32(glm::vec3(color), 0.55f), s);
    drawText(dl, ImVec2(p0.x + pad.x, p0.y + pad.y), color, label);
    ImGui::Dummy(ImVec2(p1.x - p0.x, p1.y - p0.y));
}

void fidelityBadge(const DrawContext& ctx, data::FidelityClass cls) {
    badge(ctx, data::fidelityName(cls), ctx.theme->fidelityColor(cls));
}

void observabilityBadge(const DrawContext& ctx, Observability o) {
    // Physical views are the default and carry no badge; Simulator-only is always announced (spec 00 §6).
    if (o == Observability::SimulatorOnly) badge(ctx, observabilityName(o), ctx.theme->simOnly);
}

float headerHeight(const DrawContext& ctx) {
    return ctx.showHeader ? ImGui::GetTextLineHeight() + 4.0f + ImGui::GetStyle().ItemSpacing.y : 0.0f;
}

void header(DrawContext& ctx, IStateView& view, data::FidelityClass fidelity, const Rect& body) {
    if (!ctx.showHeader || !ctx.theme) return;
    ImGui::PushID(view.id().data(), view.id().data() + view.id().size());
    fidelityBadge(ctx, fidelity);
    if (view.observability() == Observability::SimulatorOnly) {
        ImGui::SameLine();
        observabilityBadge(ctx, view.observability());
    }
    if (view.stale()) {
        ImGui::SameLine();
        badge(ctx, "stale", ctx.theme->warn); // spec 21 §1: previous reduction shown until the new one arrives
    }
    const std::string status = view.statusLine();
    if (!status.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        text(ctx.theme->textSecondary, status);
    }
    // Right-aligned: export and the "?" theory link.
    const float right = ImGui::GetWindowContentRegionMax().x;
    const float wExport = textSize("Export").x + 14.0f, wHelp = textSize("?").x + 14.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - wExport - wHelp - 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(ctx.theme->textSecondary));
    if (ImGui::SmallButton("Export") && ctx.requestExport) {
        ExportRequest req;
        req.viewId = std::string(view.id());
        req.screenRect = body;
        if (auto csv = view.exportCsv(); csv && ImGui::GetIO().KeyShift) { // Shift+click: CSV where tabular
            req.format = ExportRequest::Format::Csv;
            req.csv = std::move(*csv);
        }
        ctx.requestExport(req);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(view.exportCsv() ? "PNG (Shift: CSV)" : "PNG");
    ImGui::SameLine();
    if (ImGui::SmallButton("?") && ctx.openTheory) ctx.openTheory(view.theoryAnchor());
    if (ImGui::IsItemHovered()) {
        const std::string tip = "Theory: " + std::string(view.theoryAnchor());
        ImGui::SetTooltip("%s", tip.c_str());
    }
    ImGui::PopStyleColor();
    ImGui::PopID();
}

void readoutCard(const DrawContext& ctx, const HitResult& hit) {
    if (!ctx.theme || (hit.title.empty() && hit.readout.empty())) return;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, toImVec4(ctx.theme->bgRaised));
    ImGui::PushStyleColor(ImGuiCol_Border, toImVec4(ctx.theme->border));
    if (ImGui::BeginTooltip()) {
        if (!hit.title.empty()) text(ctx.theme->textPrimary, hit.title);
        for (const ReadoutRow& row : hit.readout) {
            // Class dot, label, value: colour never carries the class alone — the header badge names it.
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 4.0f, p.y + 0.5f * h), 3.0f,
                                                        toU32(ctx.theme->fidelityColor(row.cls)));
            ImGui::Dummy(ImVec2(10.0f, h));
            ImGui::SameLine();
            text(ctx.theme->textSecondary, row.label);
            ImGui::SameLine();
            text(ctx.theme->textPrimary, row.value);
        }
        ImGui::EndTooltip();
    }
    ImGui::PopStyleColor(2);
}

void placeholder(const DrawContext& ctx, std::string_view message) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 size = textSize(message);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const glm::vec4 color = ctx.theme ? ctx.theme->textDisabled : glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    drawText(ImGui::GetWindowDrawList(),
             ImVec2(origin.x + 0.5f * std::max(0.0f, avail.x - size.x), origin.y + 0.5f * std::max(0.0f, avail.y - size.y)),
             color, message);
}

void phaseWheel(const DrawContext& ctx, glm::vec2 center, float radius) {
    if (!ctx.theme) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    constexpr int kSegments = 64;
    const float inner = radius * 0.62f;
    // Screen y grows downward: phase φ is drawn at angle −φ so that +π/2 (|+i⟩-like) points up.
    for (int k = 0; k < kSegments; ++k) {
        const double a0 = 2.0 * std::numbers::pi * k / kSegments, a1 = 2.0 * std::numbers::pi * (k + 1) / kSegments;
        const glm::vec3 col = math::phaseColor(0.5 * (a0 + a1));
        const ImVec2 q[4] = {
            ImVec2(center.x + inner * static_cast<float>(std::cos(a0)), center.y - inner * static_cast<float>(std::sin(a0))),
            ImVec2(center.x + radius * static_cast<float>(std::cos(a0)), center.y - radius * static_cast<float>(std::sin(a0))),
            ImVec2(center.x + radius * static_cast<float>(std::cos(a1)), center.y - radius * static_cast<float>(std::sin(a1))),
            ImVec2(center.x + inner * static_cast<float>(std::cos(a1)), center.y - inner * static_cast<float>(std::sin(a1)))};
        dl->AddQuadFilled(q[0], q[1], q[2], q[3], toU32(col));
    }
    dl->AddCircle(toImVec2(center), radius, toU32(ctx.theme->border), kSegments);
    for (const math::PhaseTick& tick : math::phaseLegend(4)) {
        const float c = static_cast<float>(std::cos(tick.phi)), s = static_cast<float>(std::sin(tick.phi));
        dl->AddLine(ImVec2(center.x + radius * c, center.y - radius * s),
                    ImVec2(center.x + (radius + 4.0f) * c, center.y - (radius + 4.0f) * s), toU32(ctx.theme->textSecondary));
        const ImVec2 sz = textSize(tick.label);
        drawText(dl, ImVec2(center.x + (radius + 8.0f + 0.5f * sz.x) * c - 0.5f * sz.x,
                            center.y - (radius + 8.0f + 0.5f * sz.y) * s - 0.5f * sz.y),
                 ctx.theme->textSecondary, tick.label);
    }
}

void colorBar(const DrawContext& ctx, const Rect& r, const std::function<glm::vec3(double)>& colorAt,
              std::string_view minLabel, std::string_view maxLabel, std::string_view title) {
    if (!ctx.theme || !colorAt) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    constexpr int kSteps = 48;
    const float x0 = static_cast<float>(r.x0), x1 = static_cast<float>(r.x1);
    for (int k = 0; k < kSteps; ++k) {
        const double t0 = static_cast<double>(k) / kSteps, t1 = static_cast<double>(k + 1) / kSteps;
        const float ya = static_cast<float>(r.y1 - t0 * r.height()), yb = static_cast<float>(r.y1 - t1 * r.height());
        dl->AddRectFilled(ImVec2(x0, yb), ImVec2(x1, ya), toU32(colorAt(0.5 * (t0 + t1))));
    }
    dl->AddRect(ImVec2(x0, static_cast<float>(r.y0)), ImVec2(x1, static_cast<float>(r.y1)), toU32(ctx.theme->border));
    const float lh = ImGui::GetTextLineHeight();
    drawText(dl, ImVec2(x1 + 4.0f, static_cast<float>(r.y0) - 0.5f * lh), ctx.theme->textSecondary, maxLabel);
    drawText(dl, ImVec2(x1 + 4.0f, static_cast<float>(r.y1) - 0.5f * lh), ctx.theme->textSecondary, minLabel);
    drawText(dl, ImVec2(x0, static_cast<float>(r.y0) - 1.4f * lh), ctx.theme->textSecondary, title);
}

} // namespace qlab::viz::widgets
