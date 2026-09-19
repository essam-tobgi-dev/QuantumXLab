// Spec 17 §7 — interaction logic.
#include "Lab/Interaction.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <glm/gtc/constants.hpp>

namespace qlab::lab {

namespace {
constexpr double kTooltipDelay_s = 0.25; // spec 17 §7.1

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Subsequence match used as the weakest fuzzy tier (spec 17 §7.8).
bool subsequence(const std::string& needle, const std::string& haystack) {
    std::size_t i = 0;
    for (char c : haystack)
        if (i < needle.size() && c == needle[i]) ++i;
    return i == needle.size();
}
} // namespace

void fitClipPlanes(gfx::Camera& camera) {
    // Spec 18 §6. With a 24-bit depth buffer and no reversed-Z on GL 4.1 the resolution at the
    // pivot distance d is about d²/(near · 2²⁴): near = d/200 gives 30 µm at a 2.4 m rack view
    // (0.2 mm panel reliefs used to z-fight at d/1000) and 0.06 µm at the 5 mm chip view. The far
    // plane only needs the room; a large far/near ratio costs nothing here.
    double d = std::max(camera.distance(), 1e-6);
    camera.setClip(std::clamp(5e-3 * d, 1e-7, 0.05), std::max(50.0, 50.0 * d));
}

Interaction::Interaction(Scene& scene, const BindingRegistry* bindings) : scene_(&scene), bindings_(bindings) {
    for (const auto& group : scene.layout().layers) { // the layout lists the layers it ships with
        Group g;
        if (groupFromName(group, g)) view_.layers[static_cast<std::size_t>(g)] = true;
    }
    for (const auto& b : scene.layout().bookmarks) {
        LayoutBookmark bm = b;
        if (!bm.scaleIsland.empty()) { // anchored to a scale island: keep the authored offset
            ComponentId island = scene.chipRoot();
            if (const Node* n = scene.node(island); n && n->subtreeBounds.valid()) {
                glm::dvec3 offset = bm.view.position - bm.view.target;
                bm.view.target = n->subtreeBounds.center();
                bm.view.position = bm.view.target + offset;
            }
        }
        bookmarks_.push_back(std::move(bm));
    }
    // one bookmark per qubit, framed on its pad (spec 17 §7)
    for (std::size_t q = 0; q < scene.qubitNodes().size(); ++q) {
        const Node* n = scene.node(scene.qubitNodes()[q]);
        if (!n || !n->subtreeBounds.valid()) continue;
        LayoutBookmark bm;
        bm.name = std::format("Qubit q[{}]", q);
        bm.scaleIsland = "chip_micro";
        bm.view.name = bm.name;
        // framed on the pad with room above it for the Bloch mini-sphere (spec 17 §8)
        bm.view.target = n->subtreeBounds.center() + glm::dvec3(0.0, 3e-4, 0.0);
        bm.view.fovDeg = 35.0;
        double r = std::max(n->subtreeBounds.radius() * 1.15, 6e-4);
        double distance = r / std::sin(glm::radians(0.5 * bm.view.fovDeg));
        bm.view.position = bm.view.target + glm::normalize(glm::dvec3(0.0, 1.0, 0.45)) * distance;
        bookmarks_.push_back(std::move(bm));
    }
}

void Interaction::hover(ComponentId id, double timeS) {
    if (id != hovered_) {
        hovered_ = id;
        hoverStart_ = timeS;
    }
}

std::optional<Tooltip> Interaction::tooltip(double timeS) const {
    if (hovered_.value == 0 || timeS - hoverStart_ < kTooltipDelay_s) return std::nullopt;
    const Node* n = scene_->node(hovered_);
    if (!n) return std::nullopt;
    const ComponentDescriptor* d = scene_->descriptor(*n);
    if (!d) return std::nullopt;
    Tooltip t;
    t.id = hovered_;
    t.name = d->name;
    t.displayName = n->displayName;
    t.category = d->category;
    t.text = d->tooltip;
    t.function = d->function;
    if (!d->theoryAnchors.empty()) t.theory = d->theoryAnchors.front();
    // Up to three live rows, in sheet order; the first one also fills the single-line fields.
    for (const SpecRow& row : d->specSheet) {
        if (!row.binding) continue;
        std::optional<BindingValue> v = bindings_ ? bindings_->resolve(n->params, row) : std::nullopt;
        Tooltip::LiveRow live;
        live.text = std::format("{}: {}", row.field, formatBindingValue(v, row.unit));
        live.cls = v ? v->cls : row.cls;
        live.simulatorOnly = row.simulatorOnly;
        if (t.liveRows.empty()) {
            t.liveRow = live.text;
            t.cls = live.cls;
            t.simulatorOnly = live.simulatorOnly;
        }
        t.liveRows.push_back(std::move(live));
        if (t.liveRows.size() == 3) break;
    }
    return t;
}

void Interaction::select(ComponentId id) {
    const Node* n = scene_->node(id);
    selected_ = n && n->pickable ? id : ComponentId{0};
}

void Interaction::selectParent() {
    if (const Node* n = scene_->node(selected_)) selected_ = n->parent;
}

gfx::Bookmark Interaction::focusBookmark(ComponentId id, const gfx::Camera& camera) const {
    gfx::Camera framed = camera;
    const Node* n = scene_->node(id);
    gfx::Aabb box = n ? (n->subtreeBounds.valid() ? n->subtreeBounds : n->worldBounds) : emptyAabb();
    if (box.valid()) framed.frame(box);
    gfx::Bookmark bm = framed.bookmark(n ? n->displayName : std::string{});
    return bm;
}

bool Interaction::focus(ComponentId id, gfx::Camera& camera, double durationS) {
    const Node* n = scene_->node(id);
    if (!n) return false;
    gfx::Aabb box = n->subtreeBounds.valid() ? n->subtreeBounds : n->worldBounds;
    if (!box.valid()) return false;
    gfx::Camera framed = camera;
    framed.frame(box);
    camera.transitionTo(framed.bookmark(n->displayName), durationS);
    if (durationS <= 0.0) camera.set(framed.bookmark(n->displayName));
    fitClipPlanes(camera);
    return true;
}

void Interaction::update(double dt, gfx::Camera& camera) const {
    camera.update(dt);
    fitClipPlanes(camera);
}

void Interaction::setExplode(Assembly a, double s) {
    view_.explode[static_cast<std::size_t>(a)] = std::clamp(s, 0.0, 1.0);
    scene_->setExplode(a, s);
}

void Interaction::setCutaway(bool on, double angleDeg) {
    view_.cutaway = on;
    view_.cutawayAngle_deg = angleDeg;
}

glm::dvec4 Interaction::cutawayPlane() const {
    double a = glm::radians(view_.cutawayAngle_deg);
    glm::dvec3 n{std::cos(a), 0.0, std::sin(a)};
    glm::dvec3 axis = scene_->layout().fridgePosition_m;
    return {n.x, n.y, n.z, -glm::dot(n, axis)};
}

bool Interaction::nodeVisible(const Node& n) const {
    if (!n.visible) return false;
    if (!layerVisible(n.group)) return false;
    if (n.can && !view_.cansVisible) return false;
    return true;
}

std::vector<SearchHit> Interaction::search(std::string_view query, std::size_t limit) const {
    std::vector<SearchHit> hits;
    std::string q = lower(query);
    if (q.empty()) return hits;
    for (const Node& n : scene_->nodes()) {
        const ComponentDescriptor* d = scene_->descriptor(n);
        std::string instance = lower(n.instanceName), display = lower(n.displayName);
        std::string component = d ? lower(d->name) : std::string{};
        std::string descriptor = lower(n.descriptorId);
        double score = 0.0;
        if (instance == q || descriptor == q) score = 1000.0;
        else if (instance.starts_with(q)) score = 700.0;
        else if (instance.find(q) != std::string::npos) score = 600.0;
        else if (descriptor.find(q) != std::string::npos) score = 550.0;
        else if (component.find(q) != std::string::npos) score = 400.0;
        else if (display.find(q) != std::string::npos) score = 350.0;
        else if (subsequence(q, instance) || subsequence(q, component)) score = 100.0;
        if (score <= 0.0) continue;
        if (n.kind == NodeKind::Group) score *= 0.5; // prefer real components over grouping nodes
        hits.push_back({n.id, n.instanceName + (d ? " — " + d->name : std::string{}), score});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
        return a.score != b.score ? a.score > b.score : a.id.value < b.id.value;
    });
    if (limit > 0 && hits.size() > limit) hits.resize(limit);
    return hits;
}

ComponentId Interaction::searchSelect(std::string_view query, gfx::Camera* camera) {
    auto hits = search(query, 1);
    if (hits.empty()) return ComponentId{0};
    select(hits.front().id);
    if (camera) focus(hits.front().id, *camera);
    return hits.front().id;
}

const LayoutBookmark* Interaction::bookmark(std::string_view name) const {
    for (const auto& b : bookmarks_)
        if (b.name == name) return &b;
    return nullptr;
}

bool Interaction::applyBookmark(std::string_view name, gfx::Camera& camera, double durationS) {
    const LayoutBookmark* b = bookmark(name);
    if (!b) return false;
    if (durationS <= 0.0) camera.set(b->view);
    else camera.transitionTo(b->view, durationS);
    if (!b->scaleIsland.empty()) { // a chip bookmark shows the chip layers only (spec 17 §7)
        if (!layersBeforeIsland_) layersBeforeIsland_ = view_.layers;
        for (int g = 0; g < kGroupCount; ++g) {
            Group group = static_cast<Group>(g);
            setLayerVisible(group, group == Group::Chip || group == Group::ChipMicro || group == Group::Overlay);
        }
    } else if (layersBeforeIsland_) { // back in the room: what the chip view hid comes back
        view_.layers = *layersBeforeIsland_;
        layersBeforeIsland_.reset();
    }
    fitClipPlanes(camera);
    return true;
}

void Interaction::storeBookmark(std::string name, const gfx::Camera& camera) {
    LayoutBookmark bm;
    bm.name = std::move(name);
    bm.view = camera.bookmark(bm.name);
    for (auto& b : bookmarks_)
        if (b.name == bm.name) {
            b = bm;
            return;
        }
    bookmarks_.push_back(std::move(bm));
}

core::Json Interaction::saveViewState() const {
    core::Json layers = core::Json::object();
    for (int g = 0; g < kGroupCount; ++g) layers[std::string(groupName(static_cast<Group>(g)))] = view_.layers[static_cast<std::size_t>(g)];
    core::Json explode = core::Json::array();
    for (double s : view_.explode) explode.push_back(s);
    return core::Json{{"layers", layers},
                      {"explode", explode},
                      {"xray", view_.xray},
                      {"cutaway", view_.cutaway},
                      {"cutaway_angle_deg", view_.cutawayAngle_deg},
                      {"cans_visible", view_.cansVisible},
                      {"labels", view_.labels}};
}

void Interaction::loadViewState(const core::Json& j) {
    if (!j.is_object()) return;
    if (auto layers = j.find("layers"); layers != j.end() && layers->is_object())
        for (auto it = layers->begin(); it != layers->end(); ++it) {
            Group g;
            if (groupFromName(it.key(), g) && it.value().is_boolean())
                view_.layers[static_cast<std::size_t>(g)] = it.value().get<bool>();
        }
    if (auto e = j.find("explode"); e != j.end() && e->is_array())
        for (std::size_t i = 0; i < e->size() && i < view_.explode.size(); ++i)
            setExplode(static_cast<Assembly>(i), (*e)[i].get<double>());
    view_.xray = j.value("xray", view_.xray);
    view_.cutawayAngle_deg = j.value("cutaway_angle_deg", view_.cutawayAngle_deg);
    view_.cutaway = j.value("cutaway", view_.cutaway);
    view_.cansVisible = j.value("cans_visible", view_.cansVisible);
    view_.labels = j.value("labels", view_.labels);
}

} // namespace qlab::lab
