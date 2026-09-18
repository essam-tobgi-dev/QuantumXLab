#pragma once
// Spec 17 §7 — interaction state of the laboratory viewport, entirely headless: hover and tooltip,
// selection and breadcrumbs, focus, exploded view, cutaway, X-ray, layer toggles,
// search-to-select and camera bookmarks.
#include "Graphics/Camera.hpp"
#include "Data/Fidelity.hpp"
#include "Lab/BindingRegistry.hpp"
#include "Lab/Scene.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace qlab::lab {

struct SearchHit {
    ComponentId id{0};
    std::string label;      // the instance name and component name that matched
    double score = 0.0;
    bool operator==(const SearchHit& o) const { return id == o.id; }
};

// Persisted in the project (spec 17 §7.7).
struct ViewState {
    std::array<bool, kGroupCount> layers{};
    std::array<double, kAssemblyCount> explode{};
    bool xray = false;
    bool cutaway = false;
    double cutawayAngle_deg = 0.0;
    bool cansVisible = true;
    bool labels = true;
    ViewState() { layers.fill(true); }
};

// The hover card (spec 17 §7.1): what the component is, what it is for, and what it reads now.
struct Tooltip {
    struct LiveRow {
        std::string text;           // "field: value unit"
        data::FidelityClass cls = data::FidelityClass::Model;
        bool simulatorOnly = false;
    };
    ComponentId id{0};
    std::string name;               // descriptor name, e.g. "HEMT low-noise amplifier"
    std::string displayName;        // this instance, e.g. "Attenuator 20 dB"
    std::string category;           // "stage", "amplifier", …
    std::string text;               // descriptor one-line tooltip (spec 17 §4)
    std::string function;           // the descriptor's `function` paragraph: purpose and physics
    std::string theory;             // first theory anchor, e.g. "T08#1.2-cooling-power"; may be empty
    std::vector<LiveRow> liveRows;  // up to three live spec-sheet rows, in sheet order
    std::string liveRow;            // the first of them (kept for callers that show one line)
    data::FidelityClass cls = data::FidelityClass::Model;
    bool simulatorOnly = false;
};

class Interaction {
public:
    // `bindings` may be null; it is only used for tooltips and inspector values.
    explicit Interaction(Scene& scene, const BindingRegistry* bindings = nullptr);

    // ---- hover and selection (spec 17 §7.1, §7.2)
    void hover(ComponentId id, double timeS);
    ComponentId hovered() const { return hovered_; }
    // The tooltip appears 250 ms after the cursor settles on a component.
    std::optional<Tooltip> tooltip(double timeS) const;
    void select(ComponentId id);
    void selectParent();
    void clearSelection() { selected_ = ComponentId{0}; }
    ComponentId selected() const { return selected_; }
    Inspectable inspect(ComponentId id) const { return scene_->inspect(id); }
    std::string breadcrumbText(ComponentId id) const { return scene_->breadcrumbText(id); }

    // ---- focus (spec 17 §7.3): frame the node's AABB over 400 ms
    gfx::Bookmark focusBookmark(ComponentId id, const gfx::Camera& camera) const;
    bool focus(ComponentId id, gfx::Camera& camera, double durationS = 0.4);
    // Advances a camera transition and keeps the clip planes fitted to the viewing distance
    // (spec 18 §6: the chip's micrometre features and the 7 m room share one camera).
    void update(double dt, gfx::Camera& camera) const;

    // ---- exploded view, cutaway, X-ray, layers (spec 17 §7.4 – §7.7)
    void setExplode(Assembly a, double s);
    double explode(Assembly a) const { return view_.explode[static_cast<std::size_t>(a)]; }
    void setXray(bool on) { view_.xray = on; }
    bool xray() const { return view_.xray; }
    void setCutaway(bool on, double angleDeg);
    bool cutaway() const { return view_.cutaway; }
    double cutawayAngleDeg() const { return view_.cutawayAngle_deg; }
    // World clip plane through the fridge axis; fragments with dot(n, p) + w >= 0 are kept.
    glm::dvec4 cutawayPlane() const;
    void setCansVisible(bool on) { view_.cansVisible = on; }
    bool cansVisible() const { return view_.cansVisible; }
    void setLayerVisible(Group g, bool on) { view_.layers[static_cast<std::size_t>(g)] = on; }
    bool layerVisible(Group g) const { return view_.layers[static_cast<std::size_t>(g)]; }
    bool nodeVisible(const Node& n) const;

    // ---- search-to-select (spec 17 §7.8): fuzzy over instance and component names
    std::vector<SearchHit> search(std::string_view query, std::size_t limit = 0) const;
    // Selects the best hit and, with a camera, focuses it.
    ComponentId searchSelect(std::string_view query, gfx::Camera* camera = nullptr);

    // ---- bookmarks: the layout's plus a generated one per qubit (spec 17 §7)
    const std::vector<LayoutBookmark>& bookmarks() const { return bookmarks_; }
    const LayoutBookmark* bookmark(std::string_view name) const;
    bool applyBookmark(std::string_view name, gfx::Camera& camera, double durationS = 0.4);
    void storeBookmark(std::string name, const gfx::Camera& camera);

    // ---- persistence
    const ViewState& view() const { return view_; }
    core::Json saveViewState() const;
    void loadViewState(const core::Json& j);

    Scene& scene() { return *scene_; }
    const Scene& scene() const { return *scene_; }

private:
    Scene* scene_;
    const BindingRegistry* bindings_;
    ViewState view_;
    ComponentId hovered_{0}, selected_{0};
    double hoverStart_ = 0.0;
    std::vector<LayoutBookmark> bookmarks_;
};

// Near/far planes for the current viewing distance (spec 18 §6).
void fitClipPlanes(gfx::Camera& camera);

} // namespace qlab::lab
