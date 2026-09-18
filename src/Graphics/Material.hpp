#pragma once
// Spec 18 §3/§4 — PBR metallic-roughness material and a small named palette for lab parts.
// Textures (spec 18 §4, amended 2026-09-18): a material may name a texture set of
// gfx::TextureLibrary; the TEXTURED shader permutation then multiplies baseColor by the albedo
// map, the roughness factor by the roughness map, and perturbs the normal with the normal map
// through a derivative-based tangent frame (uv path) or a world-space triplanar projection.
#include "Graphics/Ubo.hpp"
#include <glm/glm.hpp>
#include <string>
#include <string_view>

namespace qlab::gfx {

struct Material {
    glm::vec4 baseColor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    glm::vec3 emissive{0};
    float emissiveStrength = 0.0f;
    float colormapMix = 0.0f;   // 0 = base colour, 1 = colormap(value)
    float colormapValue = 0.0f;
    bool unlit = false;
    // Texture set (empty = constants only). `uvScale` tiles the mesh uv (uv path) or is the
    // number of tiles per metre (triplanar). `normalStrength` scales the tangent-space xy of the
    // normal map (0.4–0.8 for a clean laboratory). With `normalizeMaps` the albedo and roughness
    // maps are divided by their means, so baseColor/roughness stay the surface's mean values and
    // the maps carry only the variation (metals keep their calibrated F0).
    std::string textureSet;
    float uvScale = 1.0f;
    bool triplanar = false;
    float normalStrength = 1.0f;
    bool normalizeMaps = true;
    bool textured() const { return !textureSet.empty(); }
    bool transparent() const { return baseColor.a < 0.999f; }
    // `albedoGain`/`roughnessGain` are the normalisation factors of the bound set (1 = none).
    MaterialUbo toUbo(const glm::vec3& albedoGain = glm::vec3(1.0f), float roughnessGain = 1.0f) const;

    // Named presets (lab materials): copper, gold, aluminium, stainless, niobium, silicon,
    // sapphire, pcb, black_anodized, glass, mu_metal, plastic_white, led.
    static Material preset(std::string_view name);
};

} // namespace qlab::gfx
