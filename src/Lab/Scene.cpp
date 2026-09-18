// Spec 17 §1 — scene graph storage, depth-first ordering, queries and inspection.
#include "Lab/Scene.hpp"
#include <algorithm>
#include <format>
#include <glm/gtc/matrix_transform.hpp>

namespace qlab::lab {

Scene::Scene(ComponentCatalog catalog) : catalog_(std::move(catalog)) {}

ComponentId Scene::add(Node n, ComponentId parent) {
    n.id = ComponentId{static_cast<std::uint32_t>(nodes_.size() + 1)};
    n.parent = parent;
    n.children.clear();
    if (!n.descriptorId.empty() && n.kind == NodeKind::Component) {
        const auto& all = catalog_.all();
        auto it = std::find_if(all.begin(), all.end(), [&](const ComponentDescriptor& d) { return d.id == n.descriptorId; });
        n.descriptorIndex = it == all.end() ? -1 : static_cast<std::int32_t>(it - all.begin());
    }
    ComponentId id = n.id;
    nodes_.push_back(std::move(n));
    if (parent.value != 0) nodes_[parent.value - 1].children.push_back(id);
    return id;
}

Status Scene::finalize(double buildMs) {
    const std::size_t n = nodes_.size();
    std::vector<std::uint32_t> order;
    order.reserve(n);
    std::vector<std::uint32_t> stack;
    for (std::size_t i = n; i-- > 0;)
        if (nodes_[i].parent.value == 0) stack.push_back(static_cast<std::uint32_t>(i));
    while (!stack.empty()) {
        std::uint32_t i = stack.back();
        stack.pop_back();
        order.push_back(i);
        const auto& ch = nodes_[i].children;
        for (auto it = ch.rbegin(); it != ch.rend(); ++it) stack.push_back(it->value - 1);
    }
    if (order.size() != n) return fail(kErrScene, std::format("scene graph has {} unreachable node(s)", n - order.size()));
    std::vector<std::uint32_t> newIndex(n);
    for (std::size_t k = 0; k < n; ++k) newIndex[order[k]] = static_cast<std::uint32_t>(k);
    auto remap = [&](ComponentId& id) {
        if (id.value != 0) id = ComponentId{newIndex[id.value - 1] + 1};
    };
    std::vector<Node> sorted;
    sorted.reserve(n);
    for (std::uint32_t old : order) sorted.push_back(std::move(nodes_[old]));
    nodes_ = std::move(sorted);
    for (auto& node : nodes_) {
        remap(node.id);
        remap(node.parent);
        for (auto& c : node.children) remap(c);
    }
    for (auto& id : qubitNodes_) remap(id);
    for (auto& id : resonatorNodes_) remap(id);
    for (auto& id : stageNodes_) remap(id);
    for (auto& run : runs_) {
        for (auto& id : run.segments) remap(id);
        for (auto& a : run.attenuators) remap(a.first);
        for (auto& id : run.stageAnchor) remap(id);
    }
    remap(chipRoot_);
    std::vector<std::uint32_t> count(n, 1);
    for (std::size_t i = n; i-- > 0;) {
        const auto& node = nodes_[i];
        if (node.parent.value != 0) count[node.parent.value - 1] += count[i];
        nodes_[i].subtreeEnd = static_cast<std::uint32_t>(i + count[i]);
        if (node.parent.value != 0 && node.parent.value - 1 >= i) return fail(kErrScene, "parent after child in depth-first order");
    }
    updateTransforms();

    stats_ = SceneStats{};
    stats_.nodes = n;
    for (const auto& node : nodes_) {
        stats_.components += node.kind == NodeKind::Component ? 1 : 0;
        stats_.groups += node.kind == NodeKind::Group ? 1 : 0;
        stats_.scenery += node.kind == NodeKind::Scenery ? 1 : 0;
        stats_.trianglesFinest += meshes_.triangles(node.finestMesh());
    }
    stats_.meshes = meshes_.size();
    stats_.cacheHits = meshes_.cacheHits();
    stats_.uniqueTriangles = meshes_.uniqueTriangles();
    stats_.buildMs = buildMs;
    return {};
}

const Node* Scene::node(ComponentId id) const {
    return id.value >= 1 && id.value <= nodes_.size() ? &nodes_[id.value - 1] : nullptr;
}
Node* Scene::node(ComponentId id) { return id.value >= 1 && id.value <= nodes_.size() ? &nodes_[id.value - 1] : nullptr; }

const ComponentDescriptor* Scene::descriptor(const Node& n) const {
    if (n.descriptorIndex < 0 || static_cast<std::size_t>(n.descriptorIndex) >= catalog_.all().size()) return nullptr;
    return &catalog_.all()[static_cast<std::size_t>(n.descriptorIndex)];
}

std::vector<ComponentId> Scene::findByDescriptor(std::string_view descriptorId) const {
    std::vector<ComponentId> out;
    for (const auto& n : nodes_)
        if (n.kind == NodeKind::Component && n.descriptorId == descriptorId) out.push_back(n.id);
    return out;
}

ComponentId Scene::findByInstance(std::string_view instanceName) const {
    for (const auto& n : nodes_)
        if (n.instanceName == instanceName) return n.id;
    return ComponentId{0};
}

std::vector<ComponentId> Scene::breadcrumb(ComponentId id) const {
    std::vector<ComponentId> out;
    for (const Node* n = node(id); n; n = node(n->parent)) out.push_back(n->id);
    std::reverse(out.begin(), out.end());
    return out;
}

std::string Scene::breadcrumbText(ComponentId id, std::string_view separator) const {
    std::string out;
    for (ComponentId c : breadcrumb(id)) {
        if (c == root()) continue; // the laboratory root is implicit (spec 17 §7.2 example)
        if (!out.empty()) out += separator;
        out += node(c)->displayName;
    }
    return out;
}

Inspectable Scene::inspect(ComponentId id) const {
    Inspectable x;
    const Node* n = node(id);
    if (!n) return x;
    x.id = n->id;
    x.instanceName = n->instanceName;
    x.displayName = n->displayName;
    x.descriptorId = n->descriptorId;
    x.children = n->children;
    x.breadcrumb = breadcrumb(id);
    if (const ComponentDescriptor* d = descriptor(*n)) {
        x.hasDescriptor = true;
        x.name = d->name;
        x.category = d->category;
        x.function = d->function;
        x.physicsSummary = d->physicsSummary;
        x.tooltip = d->tooltip;
        x.equationIds = d->equationIds;
        x.specSheet = d->specSheet;
        x.theoryAnchors = d->theoryAnchors;
        x.simulatorOnly = d->simulatorOnly;
    } else {
        x.name = n->displayName;
        x.category = n->kind == NodeKind::Group ? "group" : "scenery";
    }
    return x;
}

void Scene::updateTransforms() {
    for (auto& n : nodes_) {
        glm::dmat4 parentWorld = n.parent.value != 0 ? nodes_[n.parent.value - 1].world : glm::dmat4(1.0);
        glm::dmat4 m = n.local.matrix();
        if (n.explodeOffset != glm::dvec3(0.0)) m = glm::translate(glm::dmat4(1.0), n.explodeOffset) * m;
        n.world = parentWorld * m;
        n.worldBounds = n.localBounds.valid() ? n.localBounds.transformed(n.world) : emptyAabb();
    }
    for (std::size_t i = nodes_.size(); i-- > 0;) {
        Node& n = nodes_[i];
        n.subtreeBounds = n.worldBounds;
        for (ComponentId c : n.children) mergeInto(n.subtreeBounds, nodes_[c.value - 1].subtreeBounds);
    }
}

} // namespace qlab::lab
