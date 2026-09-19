// Spec 19 §3 "Estimates" — hardware time, fidelity, resources and the QEC overhead (spec 15, 16 §7)
// with the assumption list EXPANDED BY DEFAULT and a fidelity badge on every row.
#include "Data/Fidelity.hpp"
#include "UI/Format.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <imgui.h>

namespace qlab::ui {
namespace {

class EstimatesPanel final : public BasicPanel {
  public:
    EstimatesPanel()
        : BasicPanel(PanelId::Estimates, "estimates", "panels.estimates", "∑",
                     Workspace::Analysis) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["assumptions_open"] = assumptionsOpen_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (j.is_object())
            if (const auto it = j.find("assumptions_open"); it != j.end() && it->is_boolean())
                assumptionsOpen_ = it->get<bool>();
    }

  private:
    void row(const UiContext& ctx, std::string_view label, std::string value,
             data::FidelityClass cls);
    bool assumptionsOpen_ = true; // spec 19 §3: expanded by default
};

void EstimatesPanel::row(const UiContext& ctx, std::string_view label, std::string value,
                         data::FidelityClass cls) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    widgets::text(ctx, Token::TextSecondary, label);
    ImGui::TableNextColumn();
    {
        FontScope f(*ctx.fonts, FontRole::Readout);
        widgets::text(ctx, Token::TextPrimary, value);
    }
    ImGui::TableNextColumn();
    widgets::fidelityBadge(ctx, cls);
}

void EstimatesPanel::draw(UiContext& ctx) {
    if (ctx.estimate == nullptr) {
        widgets::placeholder(ctx, "Compile a program to estimate its cost on hardware.");
        return;
    }
    const runtime::Estimate& e = *ctx.estimate;
    widgets::labelled(ctx, "Device", e.device);
    ImGui::SameLine();
    widgets::labelled(ctx, "Calibration",
                      e.calibrationTimestamp.empty() ? "—" : e.calibrationTimestamp);

    const auto table = [&](std::string_view id) {
        return ImGui::BeginTable(id.data(), 3, widgets::tableFlags(false), ImVec2(0.0f, 0.0f));
    };
    widgets::sectionHeader(ctx, ctx.text("estimates.wall_time"));
    if (table("##wall")) {
        row(ctx, "Total", format::duration(e.wallTime.valueS), e.cls);
        row(ctx, "Per shot", format::value(e.wallTime.perShotS, "s"), e.cls);
        row(ctx, "Reset", format::value(e.wallTime.resetS, "s"), e.cls);
        row(ctx, "Circuit", format::value(e.wallTime.circuitS, "s"), e.cls);
        row(ctx, "Readout", format::value(e.wallTime.readoutS, "s"), e.cls);
        row(ctx, "Favourable / unfavourable",
            format::duration(e.wallTime.minS) + " … " + format::duration(e.wallTime.maxS), e.cls);
        ImGui::EndTable();
    }

    widgets::sectionHeader(ctx, ctx.text("estimates.fidelity_fast"));
    if (table("##fid")) {
        row(ctx, "Product model", format::percent(e.fidelity.fast, 3), e.fidelity.cls);
        row(ctx, "Interval",
            format::percent(e.fidelity.low, 3) + " … " + format::percent(e.fidelity.high, 3),
            e.fidelity.cls);
        row(ctx, "Gates", format::percent(e.fidelity.gateProduct, 3), e.fidelity.cls);
        row(ctx, "Idles", format::percent(e.fidelity.idleProduct, 3), e.fidelity.cls);
        row(ctx, "Readout", format::percent(e.fidelity.readoutProduct, 3), e.fidelity.cls);
        if (e.fidelity.simulated) {
            const runtime::SimulatedFidelity& sim = *e.fidelity.simulated;
            row(ctx, "Classical F_c", format::percent(sim.classical, 3), sim.cls);
            row(ctx, "Hellinger", format::number(sim.hellinger), sim.cls);
            if (sim.state)
                row(ctx, "State fidelity", format::percent(*sim.state, 3), sim.cls);
        }
        ImGui::EndTable();
    }
    for (const std::string& caveat : e.fidelity.caveats)
        widgets::textWrapped(ctx, Token::Warn, caveat);

    widgets::sectionHeader(ctx, ctx.text("estimates.resources"));
    if (table("##res")) {
        row(ctx, "Qubits", format::integer(e.resources.qubits), e.cls);
        row(ctx, "Depth", format::integer(e.resources.depth), e.cls);
        row(ctx, "Two-qubit gates", format::integer(e.resources.twoQubit), e.cls);
        row(ctx, "T count", format::integer(e.resources.tCount), e.cls);
        row(ctx, "SWAPs", format::integer(e.resources.swaps), e.cls);
        row(ctx, "Circuit time", format::value(e.resources.circuitTimeS, "s"), e.cls);
        ImGui::EndTable();
    }

    widgets::sectionHeader(ctx, ctx.text("estimates.classical_cost"));
    if (table("##cost")) {
        row(ctx, "State vector", format::bytes(e.classicalCost.stateVectorBytes),
            data::FidelityClass::Model);
        row(ctx, "Density matrix", format::bytes(e.classicalCost.densityMatrixBytes),
            data::FidelityClass::Model);
        row(ctx, "Estimated time", format::duration(e.classicalCost.estimatedTimeS),
            data::FidelityClass::Model);
        row(ctx, "Host maximum qubits", format::integer(e.classicalCost.hostMaxQubits),
            data::FidelityClass::Model);
        ImGui::EndTable();
    }

    if (e.qec) {
        widgets::sectionHeader(ctx, ctx.text("estimates.qec"));
        if (table("##qec")) {
            row(ctx, "Code distance", format::integer(static_cast<std::uint64_t>(e.qec->distance)),
                data::FidelityClass::Model);
            row(ctx, "Physical qubits", format::number(e.qec->physicalQubits, 6), e.qec->cls);
            row(ctx, "Logical cycles", format::number(e.qec->logicalCycles, 6), e.qec->cls);
            row(ctx, "Fault-tolerant wall time", format::duration(e.qec->wallTimeS), e.qec->cls);
            ImGui::EndTable();
        }
    }

    if (!e.comparison.empty()) {
        widgets::sectionHeader(ctx, ctx.text("estimates.comparison"));
        if (table("##cmp")) {
            for (const runtime::DeviceComparison& c : e.comparison)
                row(ctx, c.device,
                    format::duration(c.wallTimeS) + "   " + format::percent(c.fidelityFast, 2),
                    e.cls);
            ImGui::EndTable();
        }
    }

    // Spec 19 §3: the assumption list is expanded by default; every sentence is the T12 §9 text.
    ImGui::SetNextItemOpen(assumptionsOpen_, ImGuiCond_Once);
    if (ImGui::CollapsingHeader(std::string(ctx.text("estimates.assumptions")).c_str())) {
        assumptionsOpen_ = true;
        for (const std::string& key : e.assumptions) {
            const std::string_view fromStrings =
                ctx.text(std::string("estimate_assumptions.") + key);
            const std::string_view sentence = fromStrings.starts_with("estimate_assumptions.")
                                                  ? runtime::assumptionText(key)
                                                  : fromStrings;
            widgets::textWrapped(ctx, Token::TextSecondary,
                                 sentence.empty() ? std::string_view(key) : sentence);
        }
    } else {
        assumptionsOpen_ = false;
    }
}

} // namespace

PanelPtr makeEstimatesPanel() {
    return std::make_unique<EstimatesPanel>();
}

} // namespace qlab::ui
