// Spec 19 §3 "Component Tree" — hierarchical tree of `lab::Node`s grouped by `Group`, a filter box,
// a visibility eye per node, and two-way synchronisation with the shared selection.
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace qlab::ui {
namespace {

class ComponentTreePanel final : public BasicPanel {
  public:
    ComponentTreePanel()
        : BasicPanel(PanelId::ComponentTree, "component_tree", "panels.component_tree", "▪",
                     Workspace::Lab) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["filter"] = std::string(filter_.data());
        j["group_by_layer"] = groupByLayer_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object())
            return;
        if (const auto it = j.find("filter"); it != j.end() && it->is_string())
            std::snprintf(filter_.data(), filter_.size(), "%s", it->get<std::string>().c_str());
        if (const auto it = j.find("group_by_layer"); it != j.end() && it->is_boolean())
            groupByLayer_ = it->get<bool>();
    }

  private:
    void drawNode(UiContext& ctx, const lab::Scene& scene, ComponentId id, int depth);
    bool matches(const lab::Scene& scene, ComponentId id) const;

    std::array<char, 96> filter_{};
    bool groupByLayer_ = true;
    ComponentId scrollTo_{0};
    std::uint64_t lastSelectionRevision_ = 0;
};

bool ComponentTreePanel::matches(const lab::Scene& scene, ComponentId id) const {
    const std::string_view needle(filter_.data());
    if (needle.empty())
        return true;
    const lab::Node* n = scene.node(id);
    if (n == nullptr)
        return false;
    const auto contains = [&](std::string_view hay) {
        return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                           [](char a, char b) {
                               return std::tolower(static_cast<unsigned char>(a)) ==
                                      std::tolower(static_cast<unsigned char>(b));
                           }) != hay.end();
    };
    if (contains(n->displayName) || contains(n->instanceName) || contains(n->descriptorId))
        return true;
    // Keep an ancestor visible when a descendant matches.
    for (ComponentId child : n->children)
        if (matches(scene, child))
            return true;
    return false;
}

void ComponentTreePanel::drawNode(UiContext& ctx, const lab::Scene& scene, ComponentId id,
                                  int depth) {
    const lab::Node* n = scene.node(id);
    if (n == nullptr || !matches(scene, id))
        return;
    ImGui::PushID(static_cast<int>(id.get()));

    // Visibility eye: the layer toggle of the node's group (spec 17 §7.7).
    if (ctx.interaction != nullptr) {
        bool on = ctx.interaction->layerVisible(n->group);
        const Color tint = on ? ctx.th()[Token::TextSecondary] : ctx.th()[Token::TextDisabled];
        widgets::text(ctx, tint, on ? "◉" : "○");
        if (ImGui::IsItemClicked())
            ctx.interaction->setLayerVisible(n->group, !on);
        ImGui::SameLine();
    }

    const bool selected = ctx.interaction != nullptr && ctx.interaction->selected() == id;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (n->children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (depth < 2 || !std::string_view(filter_.data()).empty())
        ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (scrollTo_ == id) {
        ImGui::SetScrollHereY(0.5f);
        scrollTo_ = ComponentId{0};
    }

    const std::string label = n->displayName.empty() ? n->instanceName : n->displayName;
    const bool openNode = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        if (ctx.interaction != nullptr)
            ctx.interaction->select(id);
        if (ctx.selection != nullptr)
            ctx.selection->selectComponent(id);
        if (ctx.cmd.selectComponent)
            ctx.cmd.selectComponent(id);
    }
    if (n->kind == lab::NodeKind::Scenery) {
        ImGui::SameLine();
        widgets::text(ctx, Token::TextDisabled, "scenery");
    }
    if (openNode && !n->children.empty()) {
        for (ComponentId child : n->children)
            drawNode(ctx, scene, child, depth + 1);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void ComponentTreePanel::draw(UiContext& ctx) {
    if (ctx.scene == nullptr) {
        widgets::placeholder(ctx, "No laboratory scene loaded.");
        return;
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter components…", filter_.data(), filter_.size());
    widgets::checkbox(ctx, "Group by layer", &groupByLayer_);

    // Selection made elsewhere scrolls the tree to it (spec 19 §3 "sync with selection").
    if (ctx.selection != nullptr && ctx.selection->revision() != lastSelectionRevision_) {
        lastSelectionRevision_ = ctx.selection->revision();
        scrollTo_ = ctx.selection->component();
    }

    ImGui::BeginChild("##tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (!groupByLayer_) {
        drawNode(ctx, *ctx.scene, ctx.scene->root(), 0);
    } else {
        for (int g = 0; g < lab::kGroupCount; ++g) {
            const auto group = static_cast<lab::Group>(g);
            std::vector<ComponentId> roots;
            for (const lab::Node& n : ctx.scene->nodes()) {
                if (n.group != group)
                    continue;
                const lab::Node* parent = ctx.scene->node(n.parent);
                if (parent == nullptr || parent->group != group)
                    roots.push_back(n.id);
            }
            if (roots.empty())
                continue;
            const std::string header =
                std::string(lab::groupName(group)) + "  (" + std::to_string(roots.size()) + ")";
            if (!ImGui::CollapsingHeader(header.c_str(),
                                         g < 3 ? ImGuiTreeNodeFlags_DefaultOpen : 0))
                continue;
            for (ComponentId id : roots)
                drawNode(ctx, *ctx.scene, id, 1);
        }
    }
    ImGui::EndChild();
}

} // namespace

PanelPtr makeComponentTreePanel() {
    return std::make_unique<ComponentTreePanel>();
}

} // namespace qlab::ui
