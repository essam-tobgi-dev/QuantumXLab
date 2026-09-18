// Spec 19 §3 "Instruments" — one tab per instrument (spec 12), each drawn as a FRONT PANEL: a dark
// screen area with the trace, knob and button rows, unit-suffixed readouts in JetBrains Mono
// tabular figures, and an LED for the state machine. Knobs are drag-to-turn with a scroll fine
// step; every control maps to an `instr::Command`, so the panel holds no instrument state.
#include "UI/Format.hpp"
#include "Data/Fidelity.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <implot.h>

namespace qlab::ui {
namespace {

using widgets::iv;
using widgets::u32;

Token ledToken(instr::State s) {
    switch (s) {
    case instr::State::Off: return Token::TextDisabled;
    case instr::State::Idle: return Token::TextSecondary;
    case instr::State::Armed: return Token::Warn;
    case instr::State::Acquiring: return Token::Ok;
    case instr::State::Fault: return Token::Err;
    }
    return Token::TextDisabled;
}

// A front-panel LED: a filled circle in the state colour plus the state NAME (spec 19 §8: colour
// never carries meaning alone).
void led(const UiContext& ctx, instr::State state) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = ctx.ui(5.0f);
    dl->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r, u32(ctx.th()[ledToken(state)]));
    ImGui::Dummy(ImVec2(r * 2.5f, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    widgets::text(ctx, ledToken(state), instr::stateName(state));
}

class InstrumentsPanel final : public BasicPanel {
public:
    InstrumentsPanel()
        : BasicPanel(PanelId::Instruments, "instruments", "panels.instruments", "●", Workspace::Lab) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["selected"] = selected_;
        j["live"] = live_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object()) return;
        if (const auto it = j.find("selected"); it != j.end() && it->is_string()) selected_ = it->get<std::string>();
        if (const auto it = j.find("live"); it != j.end() && it->is_boolean()) live_ = it->get<bool>();
    }

private:
    void drawFrontPanel(UiContext& ctx, instr::IInstrument& in);
    void drawScreen(UiContext& ctx, const instr::Trace& trace);
    void drawControls(UiContext& ctx, instr::IInstrument& in);

    std::string selected_;
    bool live_ = false;
    std::map<std::string, instr::Trace> lastTrace_;
};

void InstrumentsPanel::drawScreen(UiContext& ctx, const instr::Trace& trace) {
    // Spec 19 §3: a dark screen area carries the trace.
    const float height = ctx.ui(160.0f);
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, iv(ctx.th()[Token::BgViewport]));
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, iv(ctx.th()[Token::BgViewport]));
    ImPlot::PushStyleColor(ImPlotCol_PlotBorder, iv(ctx.th()[Token::Border]));
    ImPlot::PushStyleColor(ImPlotCol_AxisText, iv(ctx.th()[Token::TextSecondary]));
    if (ImPlot::BeginPlot("##screen", ImVec2(-1.0f, height), ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes(trace.xUnit.c_str(), trace.yUnit.c_str());
        if (!trace.x.empty()) {
            ImPlot::SetNextLineStyle(iv(ctx.th()[Token::Ok]));
            ImPlot::PlotLine("trace", trace.x.data(), trace.y.data(), static_cast<int>(trace.x.size()));
            if (trace.complexValued()) {
                ImPlot::SetNextLineStyle(iv(ctx.th()[Token::Accent]));
                ImPlot::PlotLine("imag", trace.x.data(), trace.y_im.data(), static_cast<int>(trace.x.size()));
            }
            for (const instr::Marker& mark : trace.markers)
                ImPlot::Annotation(mark.x, mark.y, iv(ctx.th()[Token::Warn]), ImVec2(4.0f, -4.0f), true, "%s",
                                   mark.label.c_str());
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleColor(4);
}

void InstrumentsPanel::drawControls(UiContext& ctx, instr::IInstrument& in) {
    const instr::SettingSchema& schema = in.settings();
    int column = 0;
    for (const instr::SettingSpec& spec : schema.settings) {
        if (column++ % 2 != 0) ImGui::SameLine(0.0f, ctx.metrics_px().spacing(4));
        ImGui::PushID(spec.key.c_str());
        const instr::SettingValue value = in.get(spec.key);
        switch (spec.type) {
        case instr::SettingType::Bool: {
            bool on = std::holds_alternative<bool>(value) && std::get<bool>(value);
            if (widgets::checkbox(ctx, spec.key, &on, spec.key))
                (void)in.execute(instr::Command::set(spec.key, on));
            break;
        }
        case instr::SettingType::Enum: {
            std::vector<std::string_view> options;
            options.reserve(spec.options.size());
            for (const std::string& o : spec.options) options.emplace_back(o);
            int index = 0;
            if (const auto* text = std::get_if<std::string>(&value))
                for (std::size_t i = 0; i < spec.options.size(); ++i)
                    if (spec.options[i] == *text) index = static_cast<int>(i);
            if (!options.empty() && widgets::combo(ctx, spec.key, &index, options, spec.key))
                (void)in.execute(instr::Command::set(spec.key, spec.options[static_cast<std::size_t>(index)]));
            break;
        }
        case instr::SettingType::Text:
            widgets::labelled(ctx, spec.key, instr::settingToString(value));
            break;
        case instr::SettingType::Int:
        case instr::SettingType::Real: {
            // Spec 19 §3: knobs are drag-to-turn with a scroll fine step; the field does exactly that.
            double v = instr::settingNumber(value).value_or(0.0);
            const widgets::FieldSpec field{.unit = spec.unit,
                                           .step = spec.step > 0.0 ? spec.step : 0.0,
                                           .lo = spec.min,
                                           .hi = spec.max,
                                           .digits = 5,
                                           .cls = data::FidelityClass::Model,
                                           .simulatorOnly = in.simulatorOnly(),
                                           .readOnly = spec.readOnly,
                                           .undoLabel = spec.key};
            if (widgets::numberField(ctx, spec.key, &v, field) && !spec.readOnly) {
                if (spec.type == instr::SettingType::Int)
                    (void)in.execute(instr::Command::set(spec.key, static_cast<std::int64_t>(std::llround(v))));
                else
                    (void)in.execute(instr::Command::set(spec.key, v));
            }
            if (!spec.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) &&
                ImGui::BeginTooltip()) {
                ImGui::TextUnformatted(spec.description.c_str());
                ImGui::EndTooltip();
            }
            break;
        }
        }
        ImGui::PopID();
    }
}

void InstrumentsPanel::drawFrontPanel(UiContext& ctx, instr::IInstrument& in) {
    const instr::InstrumentId id = in.id();
    led(ctx, in.state());
    ImGui::SameLine(0.0f, ctx.metrics_px().spacing(4));
    const bool on = in.state() != instr::State::Off;
    if (widgets::secondaryButton(ctx, on ? "Power off" : "Power on"))
        (void)in.execute(instr::Command::of(on ? instr::Command::Kind::PowerOff : instr::Command::Kind::PowerOn));
    ImGui::SameLine();
    if (widgets::secondaryButton(ctx, "Preset")) (void)in.execute(instr::Command::of(instr::Command::Kind::Preset));
    ImGui::SameLine();
    if (in.state() == instr::State::Fault) {
        if (widgets::dangerButton(ctx, "Clear fault"))
            (void)in.execute(instr::Command::of(instr::Command::Kind::ClearFault));
        ImGui::SameLine();
        widgets::text(ctx, Token::Err, in.faultMessage());
    }
    if (in.simulatorOnly()) {
        ImGui::SameLine();
        widgets::simOnlyBadge(ctx);
    }

    // ---- acquisition
    const std::span<const instr::ChannelDesc> channels = in.channels();
    for (const instr::ChannelDesc& ch : channels) {
        if (ctx.physicalLab && ch.simulatorOnly) continue;
        ImGui::PushID(ch.name.c_str());
        if (widgets::primaryButton(ctx, std::string(ctx.text("instruments.single")) + " " + ch.name)) {
            if (auto trace = in.acquire(ch.id)) lastTrace_[id.toString() + "/" + ch.name] = std::move(*trace);
        }
        ImGui::SameLine();
        bool live = live_;
        if (widgets::toggleChip(ctx, ctx.text("instruments.live"), &live) && ctx.liveRunner != nullptr) {
            live_ = live;
            (void)ctx.liveRunner->enable(id, ch.name, live);
        }
        ImGui::SameLine();
        widgets::fidelityBadge(ctx, ch.cls);
        ImGui::PopID();
    }
    const auto it = channels.empty() ? lastTrace_.end() : lastTrace_.find(id.toString() + "/" + channels[0].name);
    static const instr::Trace kEmpty;
    drawScreen(ctx, it != lastTrace_.end() ? it->second : kEmpty);

    // ---- readouts (JetBrains Mono tabular figures, spec 19 §1)
    for (const instr::ChannelDesc& ch : channels) {
        if (ctx.physicalLab && ch.simulatorOnly) continue;
        if (const auto value = in.query(ch.name); value)
            widgets::readout(ctx, ch.name, *value,
                             widgets::FieldSpec{.unit = ch.yUnit, .cls = ch.cls, .simulatorOnly = ch.simulatorOnly});
    }
    ImGui::Separator();
    drawControls(ctx, in);

    // Clamp reports: "a physical instrument beeps; the panel shows the clamp" (spec 12 §1).
    for (const instr::SettingReport& report : in.reports())
        widgets::text(ctx, Token::Warn, report.message);
}

void InstrumentsPanel::draw(UiContext& ctx) {
    if (ctx.instruments == nullptr) {
        widgets::placeholder(ctx, "No instrument rack loaded.");
        return;
    }
    // Spec 19 §2 / 12 §12: the Simulator-only probes disappear in Physical-lab mode.
    const std::vector<instr::IInstrument*> all = ctx.instruments->all(ctx.physicalLab);
    if (all.empty()) {
        widgets::placeholder(ctx, "No instruments available in this mode.");
        return;
    }
    if (!ImGui::BeginTabBar("##instruments", ImGuiTabBarFlags_FittingPolicyScroll)) return;
    for (instr::IInstrument* in : all) {
        if (in == nullptr) continue;
        const std::string label = in->id().toString();
        if (ImGui::BeginTabItem(label.c_str())) {
            selected_ = label;
            drawFrontPanel(ctx, *in);
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

} // namespace

PanelPtr makeInstrumentsPanel() { return std::make_unique<InstrumentsPanel>(); }

} // namespace qlab::ui
