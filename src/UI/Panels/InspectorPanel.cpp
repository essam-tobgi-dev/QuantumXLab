// Spec 19 §3 "Inspector" / 17 §5 — the selected `lab::Inspectable`: breadcrumb, name and category
// chip, function paragraph, a *Physics* section with rendered equations (spec 20 §5) and live term
// values, the *Spec sheet* table (field, live value with unit and fidelity badge, typical range as
// a faint bar), *Theory* links opening the Theory Browser at the anchor, and the *Children* list.
#include "UI/Format.hpp"
#include "Data/Fidelity.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/EquationView.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <imgui.h>
#include <limits>
#include <map>

namespace qlab::ui {
namespace {

class InspectorPanel final : public BasicPanel {
public:
    InspectorPanel() : BasicPanel(PanelId::Inspector, "inspector", "panels.inspector", "◆", Workspace::Lab) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["show_values_inline"] = inlineValues_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (j.is_object())
            if (const auto it = j.find("show_values_inline"); it != j.end() && it->is_boolean())
                inlineValues_ = it->get<bool>();
    }

private:
    void drawPhysics(UiContext& ctx, const lab::Inspectable& what, const lab::Node& node);
    void drawSpecSheet(UiContext& ctx, const lab::Inspectable& what, const lab::Node& node);

    bool inlineValues_ = true;
    std::map<std::string, EquationView> equations_;
};

void InspectorPanel::drawPhysics(UiContext& ctx, const lab::Inspectable& what, const lab::Node& node) {
    if (what.equationIds.empty() || ctx.assets == nullptr) return;
    widgets::sectionHeader(ctx, ctx.text("inspector.physics"));
    if (!what.physicsSummary.empty()) widgets::textWrapped(ctx, Token::TextSecondary, what.physicsSummary);
    for (const std::string& id : what.equationIds) {
        const EquationDoc* doc = ctx.assets->equation(id);
        if (doc == nullptr) {
            widgets::text(ctx, Token::Warn, "unknown equation '" + id + "'");
            continue;
        }
        EquationView& view = equations_[id];
        view.setDocument(doc);
        view.setWithValues(inlineValues_);
        // Spec 20 §5: live values come from the bindings of the selected node, matched by symbol.
        std::vector<TermValue> values;
        if (ctx.bindings != nullptr) {
            for (const EquationTerm& term : doc->terms) {
                for (const lab::SpecRow& row : what.specSheet) {
                    if (row.field != term.name) continue;
                    TermValue v;
                    v.symbol = term.symbol;
                    v.unit = term.unit.empty() ? row.unit : term.unit;
                    const auto resolved = ctx.bindings->resolve(node, row);
                    v.available = resolved.has_value() && resolved->available() && resolved->isNumber();
                    if (v.available) {
                        v.si = resolved->asNumber();
                        v.cls = resolved->cls;
                        v.simulatorOnly = resolved->simulatorOnly;
                    }
                    values.push_back(std::move(v));
                    break;
                }
            }
        }
        view.setValues(std::move(values));
        view.draw(ctx);
    }
    // Spec 19 §5.4: every displayed quantity carries a fidelity class; the equation carries its own.
    ImGui::SameLine();
    widgets::fidelityBadge(ctx, ctx.assets->equation(what.equationIds.front()) != nullptr
                                    ? ctx.assets->equation(what.equationIds.front())->cls
                                    : data::FidelityClass::Model);
}

void InspectorPanel::drawSpecSheet(UiContext& ctx, const lab::Inspectable& what, const lab::Node& node) {
    if (what.specSheet.empty()) return;
    widgets::sectionHeader(ctx, ctx.text("inspector.spec_sheet"));
    if (!ImGui::BeginTable("##spec", 4, widgets::tableFlags(false), ImVec2(0.0f, 0.0f))) return;
    ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthStretch, 0.35f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.28f);
    ImGui::TableSetupColumn("class", ImGuiTableColumnFlags_WidthStretch, 0.22f);
    ImGui::TableSetupColumn("typical", ImGuiTableColumnFlags_WidthStretch, 0.15f);
    for (const lab::SpecRow& row : what.specSheet) {
        // Spec 00 §6: a Simulator-only row is dropped entirely in Physical-lab mode.
        if (ctx.physicalLab && row.simulatorOnly) continue;
        std::optional<lab::BindingValue> value;
        if (ctx.bindings != nullptr) value = ctx.bindings->resolve(node, row);
        const std::string shown = lab::formatBindingValue(value, row.unit);
        const double si = value && value->isNumber() ? value->asNumber() : std::numeric_limits<double>::quiet_NaN();
        const double lo = row.typical ? row.typical->first : 0.0;
        const double hi = row.typical ? row.typical->second : 0.0;
        widgets::specRow(ctx, row.field, shown, value ? value->cls : row.cls,
                         row.simulatorOnly || (value && value->simulatorOnly), si, lo, hi);
    }
    ImGui::EndTable();
}

void InspectorPanel::draw(UiContext& ctx) {
    if (ctx.scene == nullptr || ctx.interaction == nullptr) {
        widgets::placeholder(ctx, ctx.text("inspector.no_selection"));
        return;
    }
    const ComponentId id = ctx.interaction->selected();
    if (id.get() == 0) {
        widgets::placeholder(ctx, ctx.text("inspector.no_selection"));
        return;
    }
    const lab::Node* node = ctx.scene->node(id);
    if (node == nullptr) {
        widgets::placeholder(ctx, ctx.text("inspector.no_selection"));
        return;
    }
    const lab::Inspectable what = ctx.scene->inspect(id);

    // ---- breadcrumb
    widgets::text(ctx, Token::TextSecondary, ctx.scene->breadcrumbText(id));
    {
        FontScope f(*ctx.fonts, FontRole::PanelTitle);
        widgets::text(ctx, Token::TextPrimary, what.displayName.empty() ? what.name : what.displayName);
    }
    if (!what.category.empty()) {
        ImGui::SameLine();
        widgets::badge(ctx, what.category, ctx.th()[Token::Accent]);
    }
    if (what.simulatorOnly) {
        ImGui::SameLine();
        widgets::simOnlyBadge(ctx);
    }
    // Spec 17 §7.5: a can (OVC, radiation shields, magnetic shields) opens from here — the interior
    // is what the user came to see. One toggle for all cans, as the layer control does.
    if (node->can) {
        const bool open = !ctx.interaction->cansVisible();
        if (open ? widgets::secondaryButton(ctx, "Close the cans") : widgets::primaryButton(ctx, "Open — show the interior"))
            ctx.interaction->setCansVisible(open);
        ImGui::SameLine();
        bool cut = ctx.interaction->cutaway();
        if (widgets::toggleChip(ctx, "Cutaway", &cut)) ctx.interaction->setCutaway(cut, ctx.interaction->cutawayAngleDeg());
        ImGui::SameLine();
        bool xray = ctx.interaction->xray();
        if (widgets::toggleChip(ctx, "X-ray", &xray)) ctx.interaction->setXray(xray);
    }
    if (!what.hasDescriptor) {
        // Spec 17 §1: scenery and grouping nodes have no component.json (the brief lists five).
        widgets::text(ctx, Token::TextSecondary, "No component descriptor — this node is scenery.");
        return;
    }

    if (!what.function.empty()) {
        widgets::sectionHeader(ctx, ctx.text("inspector.function"));
        widgets::textWrapped(ctx, Token::TextPrimary, what.function);
    }
    drawPhysics(ctx, what, *node);
    drawSpecSheet(ctx, what, *node);

    if (!what.theoryAnchors.empty()) {
        widgets::sectionHeader(ctx, ctx.text("inspector.theory"));
        for (const std::string& anchor : what.theoryAnchors) {
            widgets::text(ctx, Token::Accent, anchor);
            if (ImGui::IsItemClicked() && ctx.cmd.openTheory) ctx.cmd.openTheory(anchor);
        }
    }
    if (!what.children.empty()) {
        widgets::sectionHeader(ctx, ctx.text("inspector.children"));
        for (ComponentId child : what.children) {
            const lab::Node* c = ctx.scene->node(child);
            if (c == nullptr) continue;
            widgets::text(ctx, Token::TextSecondary, c->displayName.empty() ? c->instanceName : c->displayName);
            if (ImGui::IsItemClicked()) {
                ctx.interaction->select(child);
                if (ctx.selection != nullptr) ctx.selection->selectComponent(child);
            }
        }
    }
}

} // namespace

PanelPtr makeInspectorPanel() { return std::make_unique<InspectorPanel>(); }

} // namespace qlab::ui
