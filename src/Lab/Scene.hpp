#pragma once
// Spec 17 §1 — the laboratory scene graph: a depth-first node array (parents before children,
// ids = index + 1, so an instanced batch of siblings has a contiguous id range), double-precision
// local/world transforms, per-LOD meshes, visibility groups, bindings and bounds.
#include "Core/Json.hpp"
#include "Cryo/Wiring.hpp"
#include "Lab/Binding.hpp"
#include "Lab/Catalog.hpp"
#include "Lab/ChipLayout.hpp"
#include "Lab/Layout.hpp"
#include "Lab/MeshLibrary.hpp"
#include "Lab/Types.hpp"
#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::lab {

enum class NodeKind : std::uint8_t {
    Group,     // no geometry; organises the tree and the breadcrumb
    Component, // resolves to a component.json descriptor (spec 17 §1)
    Scenery    // room shell and props without a descriptor: rendered, never picked
};

struct LodLevel {
    double maxDistance_m = 1e9;
    MeshHandle mesh; // invalid → hidden at this level
    Detail detail = Detail::Full;
};

// A wiring run whose tube is re-evaluated when stages move (spec 17 §7.4). Control points are in
// the node's local frame at rest; each is attached to a stage (index into Scene::stageNodes(), or
// −1 for points that stay put) and follows that stage's exploded offset.
struct SplineAnchor {
    glm::dvec3 restLocal{0.0};
    int stage = -1;
};
struct SplineInfo {
    std::vector<SplineAnchor> points;
    core::Json geometry; // generator parameters except the points
    std::string generator = "Tube";
};

struct Node {
    ComponentId id{0};
    ComponentId parent{0};
    std::vector<ComponentId> children;
    NodeKind kind = NodeKind::Group;
    std::string descriptorId;
    std::int32_t descriptorIndex = -1; // into ComponentCatalog::all()
    std::string instanceName;          // unique, e.g. "att_PT2.drive_q3"
    std::string displayName;           // breadcrumb label, e.g. "Attenuator 20 dB"
    Transform local;                   // rest pose relative to the parent
    glm::dvec3 explodeOffset{0.0};     // parent-space translation from the exploded view
    glm::dmat4 world{1.0};
    Group group = Group::Room;
    std::vector<LodLevel> lods;        // ascending distance; empty for groups
    std::string material = "vertex";
    glm::vec4 tint{1.0f};
    InstanceParams params;             // $line, $k, $j, $i, $q, $e, $stage + static numbers
    std::vector<Binding> bindings;     // descriptor rows with placeholders substituted
    gfx::Aabb localBounds = emptyAabb();   // finest mesh, object space
    gfx::Aabb worldBounds = emptyAabb();   // own geometry, world space
    gfx::Aabb subtreeBounds = emptyAabb(); // own + descendants, world space
    std::uint32_t subtreeEnd = 0;      // index one past the last descendant
    Assembly assembly = Assembly::None;
    int assemblyIndex = 0;             // stage k (FridgeStages), unit slot (RackUnits)
    bool visible = true;
    bool pickable = true;
    bool can = false;                  // Can generator: cutaway clip and "hide cans" apply (spec 17 §7.5)
    bool xrayFade = false;             // FridgeExterior or shield: 12 % opacity in X-ray (spec 17 §7.6)
    std::optional<SplineInfo> spline;

    bool hasGeometry() const {
        for (const auto& l : lods)
            if (l.mesh.valid()) return true;
        return false;
    }
    MeshHandle finestMesh() const {
        for (const auto& l : lods)
            if (l.mesh.valid()) return l.mesh;
        return {};
    }
};

// Wiring line as drawn: coax runs in signal order and the attenuators it passes (pulse packets).
struct WiringRun {
    std::string lineId;
    std::size_t lineIndex = 0;
    cryo::LineKind kind = cryo::LineKind::XY;
    std::vector<ComponentId> segments;              // RT → chip for inputs, chip → RT for outputs
    std::vector<std::pair<ComponentId, double>> attenuators; // node, dB
    std::array<ComponentId, cryo::kStageCount> stageAnchor{}; // a node of this line at each stage
};

struct SceneStats {
    std::size_t nodes = 0, components = 0, groups = 0, scenery = 0;
    std::size_t meshes = 0, cacheHits = 0, uniqueTriangles = 0;
    std::size_t trianglesFinest = 0; // Σ over nodes of the finest LOD (instances counted)
    double buildMs = 0.0;
};

class Scene {
public:
    explicit Scene(ComponentCatalog catalog);
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // ---- construction (SceneBuilder): provisional ids until finalize()
    ComponentId add(Node node, ComponentId parent);
    // Reorders depth-first, renumbers ids (and every stored id), fills subtree ranges, computes
    // transforms and bounds, and records statistics.
    Status finalize(double buildMs);

    // ---- queries
    const std::vector<Node>& nodes() const { return nodes_; }
    std::size_t size() const { return nodes_.size(); }
    const Node* node(ComponentId id) const;
    Node* node(ComponentId id);
    ComponentId root() const { return nodes_.empty() ? ComponentId{0} : nodes_.front().id; }
    const ComponentDescriptor* descriptor(const Node& n) const;
    std::vector<ComponentId> findByDescriptor(std::string_view descriptorId) const;
    ComponentId findByInstance(std::string_view instanceName) const;
    std::vector<ComponentId> breadcrumb(ComponentId id) const;        // root → id
    std::string breadcrumbText(ComponentId id, std::string_view separator = " › ") const;
    Inspectable inspect(ComponentId id) const;

    // ---- transforms, bounds, exploded view (spec 17 §7.4)
    void updateTransforms();
    // s ∈ [0, 1]; 0 restores the rest pose exactly. Stage k of the fridge moves 0.25 m·k·s away
    // from the fixed top plate, the chip package lifts its PCB and chip, rack units slide out.
    void setExplode(Assembly a, double s);
    double explode(Assembly a) const { return explode_[static_cast<std::size_t>(a)]; }

    // ---- data
    const ComponentCatalog& catalog() const { return catalog_; }
    MeshLibrary& meshes() { return meshes_; }
    const MeshLibrary& meshes() const { return meshes_; }
    LayoutSpec& layout() { return layout_; }
    const LayoutSpec& layout() const { return layout_; }
    std::optional<ChipLayout>& chipLayout() { return chip_; }
    const std::optional<ChipLayout>& chipLayout() const { return chip_; }
    std::vector<ComponentId>& qubitNodes() { return qubitNodes_; }          // pad node per qubit index
    std::span<const ComponentId> qubitNodes() const { return qubitNodes_; }
    std::vector<ComponentId>& resonatorNodes() { return resonatorNodes_; }  // per qubit index (0 if none)
    std::span<const ComponentId> resonatorNodes() const { return resonatorNodes_; }
    std::array<ComponentId, cryo::kStageCount>& stageNodes() { return stageNodes_; }
    const std::array<ComponentId, cryo::kStageCount>& stageNodes() const { return stageNodes_; }
    std::vector<WiringRun>& wiringRuns() { return runs_; }
    const std::vector<WiringRun>& wiringRuns() const { return runs_; }
    ComponentId chipRoot() const { return chipRoot_; }
    void setChipRoot(ComponentId id) { chipRoot_ = id; }
    std::vector<std::string>& diagnostics() { return diagnostics_; }
    const std::vector<std::string>& diagnostics() const { return diagnostics_; }
    const SceneStats& stats() const { return stats_; }

private:
    void regenerateSplines();
    ComponentCatalog catalog_;
    std::vector<Node> nodes_;
    MeshLibrary meshes_;
    LayoutSpec layout_;
    std::optional<ChipLayout> chip_;
    std::vector<ComponentId> qubitNodes_, resonatorNodes_;
    std::array<ComponentId, cryo::kStageCount> stageNodes_{};
    std::vector<WiringRun> runs_;
    ComponentId chipRoot_{0};
    std::array<double, kAssemblyCount> explode_{};
    std::vector<std::string> diagnostics_;
    SceneStats stats_;
};

} // namespace qlab::lab
