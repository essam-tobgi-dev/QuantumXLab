#pragma once
// Internal scene-construction helpers shared by the Build*.cpp translation units.
#include "Cryo/Wiring.hpp"
#include "Hardware/Loader.hpp"
#include "Lab/Build.hpp"
#include "Lab/Generators.hpp"
#include "Lab/Layout.hpp"
#include "Lab/Materials.hpp"
#include "Lab/Scene.hpp"
#include <map>

namespace qlab::lab {

// One node to add: geometry comes from the descriptor's generator with `overrides` layered on,
// built once per LOD detail level and cached by parameter key.
struct NodeSpec {
    std::string descriptor; // component.json id; empty for groups and scenery
    std::string instance;   // unique instance name
    std::string display;    // breadcrumb label (empty → descriptor name)
    Transform local;
    Group group = Group::Room;
    std::string material = "vertex";
    std::string generator; // overrides the descriptor's generator (e.g. the MXC vessel)
    core::Json overrides = core::Json::object();
    InstanceParams params;
    double unitScale = 1.0;
    Assembly assembly = Assembly::None;
    int assemblyIndex = 0;
    bool cacheMesh = true; // false for unique geometry (wiring splines)
    bool pickable = true;
    glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
    std::optional<SplineInfo> spline;
    // Composite geometry authored by the builder (a desk with monitors, a bench with a microscope).
    // When set, it replaces the descriptor's single-generator mesh at every level of detail, so a
    // prop can be inspectable without losing the shape the scene was designed around.
    const gfx::MeshData* authoredMesh = nullptr;
    // Optional coarser authored mesh for the `simple` levels (a breadboard without its 2 000
    // holes); the full authored mesh is used when absent.
    const gfx::MeshData* authoredSimple = nullptr;
};

class SceneBuilder {
  public:
    SceneBuilder(Scene& scene, const LayoutSpec& layout, const RoutingSpec& routing,
                 bool flipChipBumps = false)
        : scene_(scene), layout_(layout), routing_(routing), flipChipBumps_(flipChipBumps) {}

    // Grouping node: no geometry, never picked.
    ComponentId addGroup(ComponentId parent, std::string instance, std::string display, Group group,
                         const Transform& local = {});
    // Component node: fails when the catalog has no such descriptor (spec 17 §1).
    Result<ComponentId> addComponent(ComponentId parent, NodeSpec spec);
    // Scenery: geometry without a descriptor (room shell, desks, dewars). Records a lint note the
    // first time a given id is used so the missing descriptor is visible (spec 17 §1, 25 §7).
    ComponentId addScenery(ComponentId parent, std::string instance, std::string display,
                           Group group, const Transform& local, gfx::MeshData mesh,
                           std::string material);
    // Component when the catalog has the descriptor, scenery with `mesh` otherwise.
    ComponentId addProp(ComponentId parent, NodeSpec spec, const gfx::MeshData& fallback);

    const LayoutSpec& layout() const { return layout_; }
    const RoutingSpec& routing() const { return routing_; }
    Scene& scene() { return scene_; }
    void note(std::string text) { scene_.diagnostics().push_back(std::move(text)); }

    // ---- stages (spec 17 §3.1)
    double stageHeight(cryo::Stage s) const;
    double stageRadius(cryo::Stage s) const;
    // Plate thickness from the stage descriptor (spec 17 §3.1: RT 20, 50 K 10, 4 K 12, still 10,
    // CP 10, MXC 12 mm).
    double plateThickness(cryo::Stage s) const;
    double plateUnderside(cryo::Stage s) const { return stageHeight(s) - 0.5 * plateThickness(s); }
    double plateTop(cryo::Stage s) const { return stageHeight(s) + 0.5 * plateThickness(s); }
    bool hasStage(cryo::Stage s) const;
    ComponentId stageNode(cryo::Stage s) const;
    // Fixed azimuth/radius layout of the chandelier bodies in the plate frame (fridge detail pass):
    // the pulse tube in the DC sector, the still and its pumping line in the flux sector, both
    // inside the wiring bundle radii.
    static constexpr double kPulseTubeAzimuth_deg = 345.0, kPulseTubeRadius_m = 0.16;
    static constexpr double kStillAzimuth_deg = 165.0, kStillRadius_m = 0.09;
    static constexpr double kPumpLineRadius_m = 0.025; // Ø 50 mm still pumping line
    static constexpr double kStillBodyRadius_m = 0.03, kStillBodyHeight_m = 0.04;
    static constexpr double kMcRadius_m = 0.025, kMcHeight_m = 0.06;
    static constexpr int kPostsPerStage = 6;
    static glm::dvec3 polar(double azimuth_deg, double radius_m, double y = 0.0);

    // ---- subtrees
    Status buildRoom(ComponentId root);
    Status buildLights(ComponentId room);
    Status buildFridge(ComponentId root);
    // Fridge sub-assemblies (BuildFridgeInternals.cpp): the pulse-tube stages with their braids,
    // the circulation bodies (still, pumping line, condensing line, heat exchangers, mixing
    // chamber), heaters and thermometry, and the sample stage under the mixing chamber.
    Status buildPulseTubeStages();
    Status buildCirculation();
    Status buildThermometry();
    Status buildSampleStage();
    Status buildWiring(ComponentId root, const cryo::Wiring& wiring);
    Status buildRacks(ComponentId root);
    // One rack: enclosure with 19-inch rails, units, rear loom (BuildRack.cpp).
    Status buildRack(ComponentId root, const RackSpec& rack, std::map<std::string, int>& instanceOf,
                     const std::vector<std::size_t>& outputLines);
    Status buildGasHandling(ComponentId root);
    Status buildGhsPlant(ComponentId ghs, double W, double H, double D);
    Status buildProps(ComponentId root);
    Status buildBenchInstruments(ComponentId bench);
    // Door with a real opening, the compressor window and the safety signage (BuildRoomDetail.cpp).
    Status buildDoorAndWindow(ComponentId room);
    Status buildSignage(ComponentId room);
    // Openings in the −X wall (spec 17 §2): the door leaf 0.9 × 2.1 m at z = 0.3 D, and a window
    // onto the pulse-tube compressor when the layout puts it outside. z is the opening's centre.
    struct WallOpening {
        double z = 0.0, width = 0.0, y0 = 0.0, height = 0.0;
    };
    WallOpening doorOpening() const;
    std::optional<WallOpening> windowOpening() const;
    Status buildChip(ComponentId parent, const hw::LoadedDevice& device, const ChipLayout& chip);
    Status buildIonLab(ComponentId root, const hw::LoadedDevice& device);

    ComponentId fridgeInterior() const { return fridgeInterior_; }
    ComponentId puckNode() const { return puck_; }

  private:
    Scene& scene_;
    const LayoutSpec& layout_;
    const RoutingSpec& routing_;
    ComponentId fridgeInterior_{0}, topPlate_{0}, puck_{0}, ptHead_{0};
    bool flipChipBumps_ = false;
    std::vector<std::string> missing_;
};

} // namespace qlab::lab
