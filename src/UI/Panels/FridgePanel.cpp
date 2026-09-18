// Spec 19 §3 "Fridge Dashboard" / spec 11 — the six stage temperatures with a 10-minute sparkline,
// the GHS pressures and ³He flow, the heater powers with their set-point controls, the pulse-tube
// state, the cooldown / warm-up controls with the elapsed time, and the per-stage heat-load table.
#include "UI/Format.hpp"
#include "Data/Fidelity.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <array>
#include <imgui.h>

namespace qlab::ui {
namespace {

using widgets::u32;

constexpr std::size_t kSparklineSamples = 600; // 10 min at 1 Hz (spec 19 §3)

class FridgePanel final : public BasicPanel {
public:
    FridgePanel()
        : BasicPanel(PanelId::Fridge, "fridge_dashboard", "panels.fridge_dashboard", "❖", Workspace::Lab) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["still_heater_w"] = stillHeater_;
        j["mxc_heater_w"] = mxcHeater_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object()) return;
        if (const auto it = j.find("still_heater_w"); it != j.end() && it->is_number()) stillHeater_ = it->get<double>();
        if (const auto it = j.find("mxc_heater_w"); it != j.end() && it->is_number()) mxcHeater_ = it->get<double>();
    }

private:
    void record(const cryo::ThermalSnapshot& s);
    void sparkline(const UiContext& ctx, std::size_t stage, ImVec2 size);

    std::array<std::vector<float>, cryo::kStageCount> history_;
    double lastSampleS_ = -1e300;
    double stillHeater_ = 10e-3, mxcHeater_ = 0.0;
};

void FridgePanel::record(const cryo::ThermalSnapshot& s) {
    for (std::size_t i = 0; i < cryo::kStageCount; ++i) {
        std::vector<float>& h = history_[i];
        h.push_back(static_cast<float>(s.T_K[i]));
        if (h.size() > kSparklineSamples) h.erase(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(h.size() - kSparklineSamples));
    }
}

void FridgePanel::sparkline(const UiContext& ctx, std::size_t stage, ImVec2 size) {
    const std::vector<float>& h = history_[stage];
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), u32(ctx.th()[Token::BgRaised]),
                      ctx.metrics_px().radiusSm);
    if (h.size() >= 2) {
        const auto [lo, hi] = std::minmax_element(h.begin(), h.end());
        const float span = std::max(1e-9f, *hi - *lo);
        for (std::size_t i = 1; i < h.size(); ++i) {
            const float x0 = p.x + size.x * static_cast<float>(i - 1) / static_cast<float>(h.size() - 1);
            const float x1 = p.x + size.x * static_cast<float>(i) / static_cast<float>(h.size() - 1);
            const float y0 = p.y + size.y * (1.0f - (h[i - 1] - *lo) / span);
            const float y1 = p.y + size.y * (1.0f - (h[i] - *lo) / span);
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), u32(ctx.th()[Token::Accent]), ctx.metrics_px().border);
        }
    }
    ImGui::Dummy(size);
}

void FridgePanel::draw(UiContext& ctx) {
    if (ctx.thermal == nullptr) {
        widgets::placeholder(ctx, "No cryostat model loaded.");
        return;
    }
    const cryo::ThermalSnapshot& s = *ctx.thermal;
    if (s.time_s - lastSampleS_ >= 1.0) {
        record(s);
        lastSampleS_ = s.time_s;
    }

    // ---- cooldown / warm-up controls (spec 11 §8)
    if (ctx.fridge != nullptr) {
        widgets::labelled(ctx, ctx.text("fridge.state"), cryo::fridgeStateName(ctx.fridge->state()));
        ImGui::SameLine();
        widgets::labelled(ctx, "Elapsed", format::duration(ctx.fridge->elapsed_s()));
        ImGui::SameLine();
        if (widgets::primaryButton(ctx, ctx.text("fridge.cooldown"))) ctx.fridge->startCooldown();
        ImGui::SameLine();
        if (widgets::dangerButton(ctx, ctx.text("fridge.warmup"))) ctx.fridge->startWarmup();
    }
    if (!s.steady) {
        ImGui::SameLine();
        widgets::badge(ctx, "transient", ctx.th()[Token::Warn]);
    }
    for (const std::string& w : s.warnings) widgets::text(ctx, Token::Warn, w);

    // ---- six stage readouts with their sparkline
    widgets::sectionHeader(ctx, "Stages");
    if (ImGui::BeginTable("##stages", 5, widgets::tableFlags(false))) {
        ImGui::TableSetupColumn("stage", ImGuiTableColumnFlags_WidthStretch, 0.16f);
        ImGui::TableSetupColumn("T", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn("load", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("margin", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("10 min", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < cryo::kStageCount; ++i) {
            const auto stage = static_cast<cryo::Stage>(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            widgets::text(ctx, Token::TextSecondary, cryo::stageName(stage));
            ImGui::TableNextColumn();
            {
                FontScope f(*ctx.fonts, FontRole::Readout);
                widgets::text(ctx, Token::TextPrimary, format::value(s.T_K[i], "K"));
            }
            ImGui::TableNextColumn();
            widgets::text(ctx, Token::TextSecondary, format::value(s.load_W[i], "W"));
            ImGui::TableNextColumn();
            // A negative margin means the stage is warming (spec 11 §1).
            widgets::text(ctx, s.margin_W[i] < 0.0 ? ctx.th()[Token::Err] : ctx.th()[Token::Ok],
                          format::value(s.margin_W[i], "W"));
            ImGui::TableNextColumn();
            sparkline(ctx, i, ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x), ImGui::GetTextLineHeight()));
        }
        ImGui::EndTable();
        ImGui::SameLine();
        widgets::fidelityBadge(ctx, data::FidelityClass::Numerical);
    }

    // ---- flow, heaters and the pulse tube
    widgets::sectionHeader(ctx, "Circulation");
    widgets::readout(ctx, ctx.text("fridge.flow"), s.n3_mol_s,
                     widgets::FieldSpec{.unit = "mol/s", .cls = data::FidelityClass::Numerical});
    widgets::readout(ctx, "MXC base", s.mxcBase_K, widgets::FieldSpec{.unit = "K", .cls = data::FidelityClass::Model});
    // Heater set points are commands into the cryo model (spec 11 §8), not UI state.
    if (ctx.thermalNet != nullptr) {
        stillHeater_ = ctx.thermalNet->cooling.stillHeater_W;
        mxcHeater_ = ctx.thermalNet->cooling.mxcHeater_W;
    }
    if (widgets::numberField(ctx, ctx.text("fridge.still_power"), &stillHeater_,
                             widgets::FieldSpec{.unit = "W", .step = 1e-4, .lo = 0.0, .hi = 0.1, .digits = 4,
                                                .cls = data::FidelityClass::Model, .undoLabel = "Still heater"}) &&
        ctx.thermalNet != nullptr)
        ctx.thermalNet->cooling.stillHeater_W = stillHeater_;
    if (widgets::numberField(ctx, ctx.text("fridge.mxc_power"), &mxcHeater_,
                             widgets::FieldSpec{.unit = "W", .step = 1e-6, .lo = 0.0, .hi = 1e-2, .digits = 4,
                                                .cls = data::FidelityClass::Model, .undoLabel = "MXC heater"}) &&
        ctx.thermalNet != nullptr)
        ctx.thermalNet->cooling.mxcHeater_W = mxcHeater_;
    // Pulse tube (spec 11 §1): the compressor that carries PT1 and PT2.
    if (ctx.thermalNet != nullptr) {
        bool pulseTube = ctx.thermalNet->cooling.pulseTubeOn;
        if (widgets::checkbox(ctx, "Pulse tube", &pulseTube, "Pulse tube"))
            ctx.thermalNet->cooling.pulseTubeOn = pulseTube;
    }

    // ---- heat-load table per stage (spec 11 §2)
    widgets::sectionHeader(ctx, "Heat loads");
    if (!ImGui::BeginTable("##loads", 5, widgets::tableFlags(false))) return;
    ImGui::TableSetupColumn("stage", ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableSetupColumn("conduction", ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableSetupColumn("radiation", ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableSetupColumn("dissipation", ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableSetupColumn("parasitic", ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < cryo::kStageCount; ++i) {
        const cryo::StageLoad& l = s.loads[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextSecondary, cryo::stageName(static_cast<cryo::Stage>(i)));
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextPrimary, format::value(l.conduction_W, "W"));
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextPrimary, format::value(l.radiation_W, "W"));
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextPrimary, format::value(l.dissipation_W, "W"));
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextPrimary, format::value(l.parasitic_W, "W"));
    }
    ImGui::EndTable();
}

} // namespace

PanelPtr makeFridgePanel() { return std::make_unique<FridgePanel>(); }

} // namespace qlab::ui
