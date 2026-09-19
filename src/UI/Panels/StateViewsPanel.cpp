// Spec 19 §3 "State Views" — the grid host for `viz::IStateView` instances (spec 21). Each tile has
// a qubit-subset selector, a fidelity badge and, where it applies, a Simulator-only badge.
//
// Observability: the panel itself is Simulator-only. Everything it hosts is a reconstruction of the
// simulated state; the one Physical member of the catalog, the counts histogram, is also the first
// thing the Results panel draws, so Physical-lab mode loses no observable quantity by hiding this
// panel (recorded in SPEC_DEVIATIONS.md).
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <imgui.h>

namespace qlab::ui {
namespace {

class StateViewsPanel final : public BasicPanel {
  public:
    StateViewsPanel()
        : BasicPanel(PanelId::StateViews, "state_views", "panels.state_views", "✓",
                     Workspace::Analysis, viz::Observability::SimulatorOnly) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["columns"] = columns_;
        core::Json hidden = core::Json::array();
        for (const std::string& id : hidden_)
            hidden.push_back(id);
        j["hidden"] = std::move(hidden);
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object())
            return;
        if (const auto it = j.find("columns"); it != j.end() && it->is_number_integer())
            columns_ = std::clamp(it->get<int>(), 1, 4);
        if (const auto it = j.find("hidden"); it != j.end() && it->is_array()) {
            hidden_.clear();
            for (const core::Json& e : *it)
                if (e.is_string())
                    hidden_.push_back(e.get<std::string>());
        }
    }

  private:
    bool shown(const viz::IStateView& view, const UiContext& ctx) const {
        if (ctx.physicalLab && view.observability() == viz::Observability::SimulatorOnly)
            return false;
        return std::find(hidden_.begin(), hidden_.end(), view.id()) == hidden_.end();
    }
    void drawTile(UiContext& ctx, viz::IStateView& view, const viz::VizTheme& theme, ImVec2 size);

    int columns_ = 2;
    std::vector<std::string> hidden_;
};

void StateViewsPanel::drawTile(UiContext& ctx, viz::IStateView& view, const viz::VizTheme& theme,
                               ImVec2 size) {
    ImGui::PushID(view.id().data(), view.id().data() + view.id().size());
    ImGui::BeginChild("##tile", size, ImGuiChildFlags_Borders);
    // Tile chrome: subset selector and the badges that the view's own header does not carry.
    const std::uint32_t n = ctx.viewInput != nullptr ? ctx.viewInput->qubitCount() : 0;
    if (n > 0) {
        ImGui::SetNextItemWidth(ctx.ui(120.0f));
        const std::span<const QubitIndex> subset = view.qubitSubset();
        const std::string preview =
            subset.empty() ? "all qubits" : "q" + std::to_string(subset.front().get());
        if (ImGui::BeginCombo("##subset", preview.c_str())) {
            if (ImGui::Selectable("all qubits", subset.empty()))
                view.setQubitSubset({});
            for (std::uint32_t q = 0; q < n; ++q) {
                const bool on =
                    std::find(subset.begin(), subset.end(), QubitIndex{q}) != subset.end();
                const std::string label = "q" + std::to_string(q);
                if (ImGui::Selectable(label.c_str(), on)) {
                    std::vector<QubitIndex> next(subset.begin(), subset.end());
                    if (on)
                        std::erase(next, QubitIndex{q});
                    else
                        next.push_back(QubitIndex{q});
                    view.setQubitSubset(next);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    if (view.observability() == viz::Observability::SimulatorOnly) {
        widgets::simOnlyBadge(ctx);
        ImGui::SameLine();
    }
    if (ctx.viewInput != nullptr)
        widgets::fidelityBadge(ctx, view.fidelity(*ctx.viewInput));
    if (view.stale()) {
        ImGui::SameLine();
        widgets::badge(ctx, "stale", ctx.th()[Token::Warn]);
    }
    ImGui::SameLine();
    if (widgets::secondaryButton(ctx, "×"))
        hidden_.emplace_back(view.id());

    if (ctx.viewInput != nullptr)
        view.update(*ctx.viewInput);
    viz::DrawContext dc = ctx.drawContext(theme);
    view.draw(dc);
    ImGui::EndChild();
    ImGui::PopID();
}

void StateViewsPanel::draw(UiContext& ctx) {
    if (ctx.views == nullptr || ctx.views->empty()) {
        widgets::placeholder(ctx, "Run a program to see the state.");
        return;
    }
    widgets::text(ctx, Token::TextSecondary, "Columns");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ctx.ui(80.0f));
    ImGui::SliderInt("##columns", &columns_, 1, 4);
    ImGui::SameLine();
    if (widgets::secondaryButton(ctx, "Show all"))
        hidden_.clear();
    ImGui::Separator();

    // Spec 21 §1: the reduction request is the union over the OPEN views only.
    viz::ReductionRequest wanted;
    for (const std::unique_ptr<viz::IStateView>& v : *ctx.views)
        if (v && shown(*v, ctx) && ctx.viewInput != nullptr)
            wanted.merge(v->wants(*ctx.viewInput));
    if (ctx.cmd.requestReductions && !wanted.empty())
        ctx.cmd.requestReductions(wanted);

    const viz::VizTheme theme = ctx.th().viz();
    const float spacing = ctx.metrics_px().spacing(2);
    const float width =
        (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(columns_ - 1)) /
        static_cast<float>(columns_);
    const float height = std::max(ctx.ui(180.0f), ImGui::GetContentRegionAvail().y * 0.5f);
    int column = 0;
    for (const std::unique_ptr<viz::IStateView>& v : *ctx.views) {
        if (!v || !shown(*v, ctx))
            continue;
        if (column > 0)
            ImGui::SameLine(0.0f, spacing);
        drawTile(ctx, *v, theme, ImVec2(width, height));
        column = (column + 1) % columns_;
    }
}

} // namespace

PanelPtr makeStateViewsPanel() {
    return std::make_unique<StateViewsPanel>();
}

} // namespace qlab::ui
