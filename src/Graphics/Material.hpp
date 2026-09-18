#pragma once
// Spec 18 §3 — PBR metallic-roughness material and a small named palette for lab parts.
#include "Graphics/Ubo.hpp"
#include <glm/glm.hpp>
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
    bool transparent() const { return baseColor.a < 0.999f; }
    MaterialUbo toUbo() const;

    // Named presets (lab materials): copper, gold, aluminium, stainless, niobium, silicon,
    // sapphire, pcb, black_anodized, glass, mu_metal, plastic_white, led.
    static Material preset(std::string_view name);
};

} // namespace qlab::gfx
