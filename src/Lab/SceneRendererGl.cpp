// Spec 17 §9 / spec 18 §5 — submitting the prepared draw list: mesh uploads (including
// re-evaluated wiring splines and cutaway-clipped cans), instancing of repeated parts, picking ids,
// the selection label and the 3D overlays.
#include "Lab/MeshOps.hpp"
#include "Lab/RackPanel.hpp"
#include "Lab/SceneRenderer.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <glm/trigonometric.hpp>
#include <map>

namespace qlab::lab {

namespace {
std::uint64_t materialKey(const gfx::Material& m) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](float v) {
        h = (h ^ static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(v))) * 1099511628211ull;
    };
    mix(m.baseColor.r);
    mix(m.baseColor.g);
    mix(m.baseColor.b);
    mix(m.baseColor.a);
    mix(m.metallic);
    mix(m.roughness);
    // Batches bind their texture set once (spec 18 §4): two materials that differ only by set
    // must not share a batch.
    for (const char c : m.textureSet)
        h = (h ^ static_cast<std::uint64_t>(static_cast<unsigned char>(c))) * 1099511628211ull;
    mix(m.uvScale);
    mix(m.normalStrength);
    return h;
}
} // namespace

const gfx::Mesh* SceneRenderer::gpuMesh(MeshHandle handle, bool clipped,
                                        const glm::dvec4& planeObject) {
    if (!handle.valid())
        return nullptr;
    const std::uint64_t key =
        clipped ? (1ull << 63) |
                      (static_cast<std::uint64_t>(
                           std::bit_cast<std::uint32_t>(static_cast<float>(planeObject.w)))
                       << 32) |
                      handle.index
                : handle.index;
    double angle = std::atan2(planeObject.z, planeObject.x);
    std::uint32_t version = scene_->meshes().version(handle);
    auto it = gpu_.find(key);
    if (it != gpu_.end() && it->second.version == version &&
        (!clipped || std::abs(it->second.angle - angle) < 1e-9))
        return it->second.mesh ? it->second.mesh.get() : nullptr;
    const gfx::MeshData& source = scene_->meshes().data(handle);
    gfx::MeshData data = clipped ? mesh::clipByPlane(source, planeObject) : source;
    Gpu& gpu = gpu_[key];
    gpu.version = version;
    gpu.angle = angle;
    if (data.triangleCount() == 0) {
        gpu.mesh.reset();
        return nullptr;
    }
    if (!gpu.mesh)
        gpu.mesh = std::make_unique<gfx::Mesh>();
    gpu.mesh->upload(data);
    return gpu.mesh.get();
}

void SceneRenderer::submit(gfx::Renderer& renderer, const Interaction& ui) {
    const glm::dvec4 plane = ui.cutawayPlane();
    const std::vector<gfx::PointLight> lights = pointLights(eye_);
    renderer.setPointLights(lights);
    struct Batch {
        const gfx::Mesh* mesh = nullptr;
        gfx::Material material;
        gfx::SubmitFlags flags;
        std::vector<gfx::InstanceData> instances;
    };
    std::map<std::tuple<std::uint32_t, std::uint64_t, bool>, std::size_t> index;
    std::vector<Batch> batches;
    stats_.draws = 0;
    stats_.instancedBatches = 0;
    stats_.instances = 0;

    for (const DrawItem& item : items_) {
        glm::dvec4 planeObject{0.0};
        if (item.clipped)
            planeObject = glm::transpose(item.world) * plane;
        const gfx::Mesh* mesh = gpuMesh(item.mesh, item.clipped, planeObject);
        if (!mesh)
            continue;
        gfx::SubmitFlags flags;
        flags.noPick = !item.pickable;
        flags.castShadow = !item.transparent;
        flags.receiveShadow = true;
        glm::mat4 model(item.world);
        if (!item.instanceable || item.transparent) {
            renderer.submit(*mesh, item.material, model, item.id, flags);
            ++stats_.draws;
            continue;
        }
        auto key = std::make_tuple(item.mesh.index, materialKey(item.material), flags.noPick);
        auto found = index.find(key);
        if (found == index.end()) {
            found = index.emplace(key, batches.size()).first;
            batches.push_back(Batch{mesh, item.material, flags, {}});
        }
        Batch& batch = batches[found->second];
        gfx::InstanceData data{};
        data.model = model;
        data.idFlags = glm::uvec2(item.pickable ? item.id.value : 0u, 0u);
        data.color = item.color;
        batch.instances.push_back(data);
    }
    for (const Batch& batch : batches) {
        if (batch.instances.size() >= 4) { // repeated parts: one instanced draw, contiguous ids
            renderer.submitInstanced(*batch.mesh, batch.material, batch.instances, batch.flags);
            ++stats_.instancedBatches;
            stats_.instances += batch.instances.size();
            ++stats_.draws;
            continue;
        }
        for (const auto& data : batch.instances) {
            renderer.submit(*batch.mesh, batch.material, data.model, ComponentId{data.idFlags.x},
                            batch.flags);
            ++stats_.draws;
        }
    }
    renderer.setSelection(ui.selected(), ui.hovered());
    if (panelLabels_) { // nameplate labels of the rack units drawn at full detail and within range
        for (const DrawItem& item : items_) {
            if (item.lod != 0)
                continue;
            const Node* n = scene_->node(item.id);
            if (!n || n->group != Group::Rack || n->descriptorIndex < 0)
                continue;
            const ComponentDescriptor& d =
                scene_->catalog().all()[static_cast<std::size_t>(n->descriptorIndex)];
            if (d.generator != "RackUnit")
                continue;
            const int u = std::max(1, static_cast<int>(std::lround(d.geometryNumber("u", 1))));
            const double depth = d.geometry.contains("depth_m")
                                     ? d.geometryNumber("depth_m", kRackUnitDepth_m)
                                     : kRackUnitDepth_m;
            const glm::dvec3 anchor = glm::dvec3(
                item.world *
                glm::dvec4(rackNameplateLocal(u, depth) + glm::dvec3(0.0, 0.006, 0.0), 1.0));
            const double dist = glm::length(anchor - eye_);
            if (dist > panelLabelRange_m)
                continue;
            // A 9 px label on a unit that spans fewer pixels than that would overprint its
            // neighbours (eight 1U boards in a row): compare angular sizes, viewport-independent
            // for a 1000 px-tall view at the camera's field of view.
            const double unitAngle = u * kRackUnit_m / std::max(dist, 1e-6);
            const double labelAngle =
                9.0 / 1000.0 * 2.0 * std::tan(glm::radians(renderer.camera().fovDeg()) * 0.5);
            if (unitAngle < 1.6 * labelAngle)
                continue;
            renderer.text().label3D(anchor, d.modelName.empty() ? d.name : d.modelName, 9.0f,
                                    {0.95f, 0.95f, 0.95f, 1.0f}, gfx::TextAnchor::BottomLeft,
                                    {0.0f, 0.0f});
        }
    }
    if (labels_ && ui.selected().value != 0) {
        if (const Node* n = scene_->node(ui.selected()); n && n->subtreeBounds.valid()) {
            glm::dvec3 top = n->subtreeBounds.center();
            top.y = n->subtreeBounds.max.y;
            // World-space anchor: gfx::TextBatch makes it camera-relative in double precision.
            renderer.text().label3D(top, n->displayName, 14.0f, {1.0f, 0.82f, 0.35f, 1.0f},
                                    gfx::TextAnchor::BottomCenter, {0.0f, -6.0f});
        }
    }
}

void SceneRenderer::submitOverlays(gfx::Renderer& renderer, const LabOverlays& overlays) {
    if (!blochSphere_) {
        blochSphere_ = std::make_unique<gfx::Mesh>();
        blochSphere_->upload(gfx::shapes::sphere(1.0f, 16, 8));
    }
    if (!packetSphere_) {
        packetSphere_ = std::make_unique<gfx::Mesh>();
        packetSphere_->upload(gfx::shapes::sphere(1.0f, 12, 6));
    }
    // Bloch mini-spheres write their qubit's id so they can be picked (spec 17 §9); the vector is a
    // gizmo line and the purity is the sphere's opacity (Simulator-only, spec 00 §6).
    for (const auto& m : overlays.blochMarkers()) {
        gfx::Material mat;
        mat.baseColor = {0.72f, 0.80f, 1.0f, static_cast<float>(0.15 + 0.45 * m.purity)};
        mat.metallic = 0.0f;
        mat.roughness = 0.25f;
        glm::mat4 model(1.0f);
        model[0][0] = model[1][1] = model[2][2] = static_cast<float>(m.radius_m);
        model[3] = glm::vec4(glm::vec3(m.center), 1.0f);
        gfx::SubmitFlags flags;
        flags.castShadow = false;
        renderer.submit(*blochSphere_, mat, model, m.node, flags);
        glm::dvec3 tip = m.center + m.vector * m.radius_m;
        // World-space endpoints: gfx::LineBatch makes them camera-relative in double precision.
        renderer.linesNoDepth().arrow(m.center, tip, {1.0f, 0.85f, 0.3f, 1.0f}, 2.5f);
        ++stats_.draws;
    }
    // Pulse packets: additive glow travelling along the coax (Illustrative).
    for (const auto& p : overlays.pulsePackets()) {
        gfx::Material mat;
        mat.baseColor = {0.35f, 0.70f, 1.0f, 1.0f};
        mat.emissive = {0.35f, 0.70f, 1.0f};
        mat.emissiveStrength = static_cast<float>(2.0 + 6.0 * std::clamp(p.amplitude, 0.0, 1.0));
        mat.unlit = true;
        double r = 0.004 + 0.01 * std::clamp(p.amplitude, 0.0, 1.0);
        glm::mat4 model(1.0f);
        model[0][0] = model[1][1] = model[2][2] = static_cast<float>(r);
        model[3] = glm::vec4(glm::vec3(p.position), 1.0f);
        gfx::SubmitFlags flags;
        flags.castShadow = false;
        flags.noPick = true; // Group::Overlay writes 0 to the id buffer (spec 17 §9)
        renderer.submit(*packetSphere_, mat, model, ComponentId{0}, flags);
        ++stats_.draws;
    }
}

void SceneRenderer::releaseGpu() {
    gpu_.clear();
    blochSphere_.reset();
    packetSphere_.reset();
}

} // namespace qlab::lab
