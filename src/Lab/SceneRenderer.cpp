// Spec 17 §10 — culling and level of detail (CPU only).
#include "Lab/SceneRenderer.hpp"
#include "Lab/Materials.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::lab {

namespace {
constexpr float kXrayOpacity = 0.12f; // spec 17 §7.6
constexpr double kTintMix = 0.6;      // albedo lerp toward the colormap value (spec 17 §8)

bool insideBox(const gfx::Aabb& box, const glm::dvec3& p, double margin) {
    return box.valid() && p.x > box.min.x - margin && p.x < box.max.x + margin &&
           p.y > box.min.y - margin && p.y < box.max.y + margin && p.z > box.min.z - margin &&
           p.z < box.max.z + margin;
}
} // namespace

SceneRenderer::SceneRenderer(Scene& scene) : scene_(&scene) {
    lodState_.assign(scene.size(), 0);
}

// Spec 17 §11 `lights[]` → gfx::PointLight. Photometry to the renderer's radiometric scale: a
// luminaire of flux Φ (lm) is treated as an isotropic source of intensity I = Φ / 4π (cd), whose
// illuminance at distance d is E = I / d² (lx). The shader attenuates a point light of radius r as
// 1 / (1 + (d/r)²) ≈ r² / d² beyond r, so an intensity w reproduces E when w = I / (E₀ r²), with
// E₀ = 1500 lx taken as the illuminance that one unit of the renderer's radiance scale displays
// as a fully lit surface (Application.cpp sets the sun to 3 units, i.e. ≈ 4500 lx of direct
// daylight, and the panels fill the shadows at a tenth of that). r is half the panel diagonal:
// inside it the panel is an area source and the inverse-square law does not apply.
std::vector<gfx::PointLight> SceneRenderer::pointLights(const glm::dvec3& eye) const {
    constexpr double kLuxPerUnit = 1500.0;
    std::vector<gfx::PointLight> out;
    for (const auto& light : scene_->layout().lights) {
        if (out.size() >= static_cast<std::size_t>(gfx::kMaxPointLights))
            break;
        const double r = std::max(0.1, 0.5 * std::hypot(light.size_m.x, light.size_m.y));
        const double intensity = light.lumens / (4.0 * 3.14159265358979) / (kLuxPerUnit * r * r);
        gfx::PointLight p{};
        // Renderer positions are camera-relative (Renderer::beginFrame subtracts the eye from
        // every model matrix), so the light is expressed relative to the same origin.
        p.position = glm::vec4(glm::vec3(light.position_m - eye), static_cast<float>(r));
        p.color = glm::vec4(light.color, static_cast<float>(intensity));
        out.push_back(p);
    }
    return out;
}
SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::prepare(const gfx::Camera& camera, const Interaction& ui,
                            const OverlayVisuals* overlays) {
    items_.clear();
    stats_ = RenderStats{};
    if (lodState_.size() != scene_->size())
        lodState_.assign(scene_->size(), 0);
    const gfx::Frustum frustum = camera.frustum();
    const glm::dvec3 eye = camera.position();
    eye_ = eye;
    const bool xray = ui.xray();
    const bool cutaway = ui.cutaway();

    // Spec 17 §10: the fridge interior is hidden inside closed cans. When the vacuum can is opaque
    // and the camera is outside it, everything it encloses is skipped.
    gfx::Aabb occluder = emptyAabb();
    if (!xray && !cutaway && ui.cansVisible() && ui.layerVisible(Group::FridgeExterior)) {
        for (ComponentId id : scene_->findByDescriptor("ovc"))
            if (const Node* n = scene_->node(id); n && n->worldBounds.valid() && ui.nodeVisible(*n))
                occluder = n->worldBounds;
        if (insideBox(occluder, eye, 0.05))
            occluder = emptyAabb(); // the camera is inside the can
    }

    const auto& nodes = scene_->nodes();
    for (std::size_t i = 0; i < nodes.size();) {
        const Node& n = nodes[i];
        // hierarchical frustum cull: a subtree outside the frustum is skipped whole
        if (n.subtreeBounds.valid() && !frustum.intersects(n.subtreeBounds)) {
            stats_.frustumCulled += n.subtreeEnd - i;
            i = std::max<std::size_t>(n.subtreeEnd, i + 1);
            continue;
        }
        ++i;
        if (n.lods.empty() || !n.hasGeometry())
            continue;
        ++stats_.considered;
        if (!ui.nodeVisible(n)) {
            ++stats_.layerHidden;
            continue;
        }
        // (a part that pokes out of the can — the still pumping line above the top plate — is kept)
        if (occluder.valid() && n.group != Group::FridgeExterior && n.group != Group::Room &&
            insideBox(occluder, n.worldBounds.min, 0.0) &&
            insideBox(occluder, n.worldBounds.max, 0.0)) {
            ++stats_.occluded;
            continue;
        }
        // LOD by camera-to-AABB-centre distance with 10 % hysteresis (spec 17 §10)
        double distance = glm::length(n.worldBounds.center() - eye);
        auto level = static_cast<std::size_t>(lodState_[i - 1]);
        level = std::min(level, n.lods.size() - 1);
        while (level + 1 < n.lods.size() &&
               distance > n.lods[level].maxDistance_m * (1.0 + hysteresis_))
            ++level;
        while (level > 0 && distance < n.lods[level - 1].maxDistance_m * (1.0 - hysteresis_))
            --level;
        lodState_[i - 1] = static_cast<std::uint8_t>(level);
        MeshHandle mesh = n.lods[level].mesh;
        if (!mesh.valid()) {
            // Spec 17 §10 group policy on top of the descriptor's table: the fridge exterior is
            // never hidden, and X-ray keeps the wiring (and the shells it shows through) visible.
            bool keep = n.group == Group::FridgeExterior ||
                        (xray && (n.group == Group::Wiring || n.xrayFade));
            if (!keep) {
                ++stats_.lodHidden;
                continue;
            }
            for (std::size_t l = n.lods.size(); l-- > 0;)
                if (n.lods[l].mesh.valid())
                    mesh = n.lods[l].mesh;
            if (!mesh.valid()) {
                ++stats_.lodHidden;
                continue;
            }
        }

        DrawItem item;
        item.id = n.id;
        item.mesh = mesh;
        item.lod = static_cast<std::uint32_t>(level);
        item.world = n.world;
        item.material = labMaterial(n.material);
        item.color = n.tint;
        item.pickable = n.pickable && n.kind == NodeKind::Component;
        if (overlays) {
            if (auto tint = overlays->albedo.find(n.id.value); tint != overlays->albedo.end())
                item.material.baseColor =
                    glm::mix(item.material.baseColor, tint->second, static_cast<float>(kTintMix));
            if (auto glow = overlays->emissive.find(n.id.value);
                glow != overlays->emissive.end() && glow->second > 0.0f) {
                item.material.emissive = glm::vec3(item.material.baseColor);
                item.material.emissiveStrength = glow->second;
                item.instanceable = false; // emissive strength is not a per-instance attribute
            }
        }
        if (xray && n.xrayFade) {
            item.material.baseColor.a = kXrayOpacity;
            item.color.a = kXrayOpacity;
        }
        item.transparent = item.material.baseColor.a < 0.999f;
        if (cutaway && n.can) {
            item.clipped = true;
            item.instanceable = false;
        }
        if (item.material.baseColor != labMaterial(n.material).baseColor ||
            item.material.emissiveStrength > 0.0f)
            item.instanceable = false;
        stats_.triangles += scene_->meshes().triangles(mesh);
        items_.push_back(std::move(item));
    }
    stats_.draws = items_.size();
}

} // namespace qlab::lab
