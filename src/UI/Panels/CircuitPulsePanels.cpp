// Spec 19 §3 "Circuit Diagram" and "Pulse Schedule" — the two program views. Both host a
// `viz::IStateView`, which draws its own header (fidelity and observability badges, status line,
// export, "?" theory link) and body; the panel adds the controls the spec §3 table names.
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include "Viz/Views/CircuitView.hpp"
#include "Viz/Views/PulseView.hpp"
#include <array>
#include <imgui.h>

namespace qlab::ui {
namespace {

// Drives a view for one frame: update from the current input, then draw with this frame's context.
void hostView(UiContext& ctx, viz::IStateView& view, const viz::VizTheme& theme) {
    if (ctx.viewInput != nullptr)
        view.update(*ctx.viewInput);
    viz::DrawContext dc = ctx.drawContext(theme);
    view.draw(dc);
}

class CircuitPanel final : public BasicPanel {
  public:
    CircuitPanel()
        : BasicPanel(PanelId::Circuit, "circuit", "panels.circuit", "◇", Workspace::Program) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["stage"] = std::string(viz::circuitStageName(view_.stage()));
        j["timed"] = view_.timed();
        j["zoom"] = view_.zoom();
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object())
            return;
        if (const auto it = j.find("stage"); it != j.end() && it->is_string())
            for (std::size_t s = 0; s < viz::kCircuitStageCount; ++s)
                if (viz::circuitStageName(static_cast<viz::CircuitStage>(s)) ==
                    it->get<std::string>())
                    view_.setStage(static_cast<viz::CircuitStage>(s));
        if (const auto it = j.find("timed"); it != j.end() && it->is_boolean())
            view_.setTimed(it->get<bool>());
        if (const auto it = j.find("zoom"); it != j.end() && it->is_number())
            view_.setZoom(it->get<double>());
    }

  private:
    viz::CircuitView view_;
};

void CircuitPanel::draw(UiContext& ctx) {
    const viz::VizTheme theme = ctx.th().viz();
    // Spec 19 §3: Source / Decomposed / Routed / Scheduled.
    for (std::size_t s = 0; s < viz::kCircuitStageCount; ++s) {
        const auto stage = static_cast<viz::CircuitStage>(s);
        if (s > 0)
            ImGui::SameLine();
        bool on = view_.stage() == stage;
        if (widgets::toggleChip(ctx, viz::circuitStageName(stage), &on) && on)
            view_.setStage(stage);
    }
    ImGui::SameLine(0.0f, ctx.metrics_px().spacing(4));
    bool timed = view_.timed();
    if (widgets::toggleChip(ctx, "Timed", &timed))
        view_.setTimed(timed);
    // `shownStage` reports what was actually drawn when the compiler produced no such stage.
    if (view_.shownStage() != view_.stage()) {
        ImGui::SameLine();
        widgets::badge(
            ctx, std::string("showing ") + std::string(viz::circuitStageName(view_.shownStage())),
            ctx.th()[Token::Warn]);
    }
    if (view_.minimap()) {
        ImGui::SameLine();
        widgets::badge(ctx, "minimap", ctx.th()[Token::Accent]);
    }
    ImGui::SameLine();
    if (widgets::secondaryButton(ctx, "SVG") && ctx.cmd.requestExport) {
        viz::ExportRequest request;
        request.viewId = std::string(view_.id());
        request.format =
            viz::ExportRequest::Format::Csv; // the SVG travels in `csv` as text (spec 23 §8)
        request.csv = view_.exportSvg();
        ctx.cmd.requestExport(request);
    }
    ImGui::Separator();
    hostView(ctx, view_, theme);

    // Spec 19 §3: gate hover shows the matrix and the duration.
    if (ctx.assets == nullptr || !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        return;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImVec2 origin = ImGui::GetItemRectMin();
    const auto hit = view_.hitTest(glm::vec2(mouse.x - origin.x, mouse.y - origin.y));
    if (!hit || hit->kind != viz::HitKind::Gate || !ImGui::BeginTooltip())
        return;
    widgets::text(ctx, Token::TextPrimary, hit->title);
    for (const viz::ReadoutRow& row : hit->readout) {
        widgets::labelled(ctx, row.label, row.value);
        ImGui::SameLine();
        widgets::fidelityBadge(ctx, row.cls);
    }
    if (const GateDocEntry* g = ctx.assets->gate(hit->title); g != nullptr)
        widgets::textWrapped(ctx, Token::TextSecondary, g->description);
    ImGui::EndTooltip();
}

// ---------------------------------------------------------------- Pulse Schedule

class PulsePanel final : public BasicPanel {
  public:
    PulsePanel()
        : BasicPanel(PanelId::Pulses, "pulses", "panels.pulses", "⬒", Workspace::Program) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["polar"] = view_.polar();
        j["window_ns"] = core::Json::array({view_.windowStartNs(), view_.windowEndNs()});
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object())
            return;
        if (const auto it = j.find("polar"); it != j.end() && it->is_boolean())
            view_.setPolar(it->get<bool>());
        if (const auto it = j.find("window_ns"); it != j.end() && it->is_array() && it->size() == 2)
            view_.setWindow((*it)[0].get<double>(), (*it)[1].get<double>());
    }

  private:
    viz::PulseView view_;
};

void PulsePanel::draw(UiContext& ctx) {
    bool polar = view_.polar();
    if (widgets::toggleChip(ctx, "Amplitude / phase", &polar))
        view_.setPolar(polar);
    ImGui::SameLine();
    if (widgets::secondaryButton(ctx, "Whole schedule"))
        view_.setWindow(0.0, 0.0);
    ImGui::SameLine();
    widgets::text(ctx, Token::TextSecondary, std::to_string(view_.rowCount()) + " channels");
    ImGui::Separator();
    hostView(ctx, view_, ctx.th().viz());
}

} // namespace

PanelPtr makeCircuitPanel() {
    return std::make_unique<CircuitPanel>();
}
PanelPtr makePulsePanel() {
    return std::make_unique<PulsePanel>();
}

} // namespace qlab::ui
