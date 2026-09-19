#pragma once
// Spec 17 §9/§10 + spec 18 §5 — draws the scene: frustum and LOD culling with hysteresis, layer
// toggles, exploded view, cutaway clipping of cans, X-ray transparency, overlay materials,
// instancing of repeated parts, picking ids and the label of the selection.
#include "Graphics/Renderer.hpp"
#include "Lab/Interaction.hpp"
#include "Lab/Overlays.hpp"
#include <memory>
#include <unordered_map>
#include <vector>

namespace qlab::lab {

struct RenderStats {
    std::size_t considered = 0, layerHidden = 0, frustumCulled = 0, lodHidden = 0, occluded = 0;
    std::size_t draws = 0, instancedBatches = 0, instances = 0;
    std::size_t triangles = 0;
};

struct DrawItem {
    ComponentId id{0};
    MeshHandle mesh;
    std::uint32_t lod = 0;
    glm::dmat4 world{1.0};
    gfx::Material material;
    glm::vec4 color{1.0f}; // per-instance tint (also the X-ray opacity)
    bool transparent = false;
    bool clipped = false; // cutaway variant of a can mesh
    bool instanceable = true;
    bool pickable = true;
};

class SceneRenderer {
  public:
    explicit SceneRenderer(Scene& scene);
    ~SceneRenderer();
    SceneRenderer(SceneRenderer&&) = delete;

    // CPU only: culling, LOD selection, material resolution. Safe without a GL context.
    void prepare(const gfx::Camera& camera, const Interaction& ui,
                 const OverlayVisuals* overlays = nullptr);
    const std::vector<DrawItem>& items() const { return items_; }
    const RenderStats& stats() const { return stats_; }

    // Needs a GL context: uploads meshes on demand (re-uploading re-evaluated splines), batches
    // repeated parts into instanced draws and submits the selection outline and label.
    void submit(gfx::Renderer& renderer, const Interaction& ui);
    void submitOverlays(gfx::Renderer& renderer, const LabOverlays& overlays);
    void releaseGpu();

    void setLodHysteresis(double fraction) { hysteresis_ = fraction; }
    void setLabels(bool on) { labels_ = on; }
    // Model labels on the rack units' nameplates at full detail (spec 17 §3.3 amended): the
    // descriptor name in SDF text anchored to the nameplate, within `panelLabelRange_m`.
    void setPanelLabels(bool on, double range_m = 3.5) {
        panelLabels_ = on;
        panelLabelRange_m = range_m;
    }
    // Camera position of the last prepare(): world-space lines and 3D labels are submitted
    // relative to it (see submitOverlays).
    const glm::dvec3& eye() const { return eye_; }
    // The layout's ceiling luminaires as renderer point lights (≤ gfx::kMaxPointLights), positions
    // relative to `eye` (spec 17 §11 `lights`); submit() sets them every frame.
    std::vector<gfx::PointLight> pointLights(const glm::dvec3& eye) const;

  private:
    const gfx::Mesh* gpuMesh(MeshHandle handle, bool clipped, const glm::dvec4& planeObject);
    Scene* scene_;
    std::vector<DrawItem> items_;
    std::vector<std::uint8_t> lodState_;
    RenderStats stats_;
    glm::dvec3 eye_{0.0};
    double hysteresis_ = 0.10; // spec 17 §10
    bool labels_ = true;
    bool panelLabels_ = true;
    double panelLabelRange_m = 3.5;
    struct Gpu {
        std::unique_ptr<gfx::Mesh> mesh;
        std::uint32_t version = 0;
        double angle = 0.0;
    };
    // gfx::Mesh creates its GL objects on construction, so every GPU resource here is built lazily
    // inside submit(), where a context is current.
    std::unordered_map<std::uint64_t, Gpu> gpu_;
    std::unique_ptr<gfx::Mesh> blochSphere_, packetSphere_;
};

} // namespace qlab::lab
