#include "Graphics/Material.hpp"
namespace qlab::gfx {
MaterialUbo Material::toUbo() const {
    MaterialUbo u{};
    u.baseColor = baseColor;
    u.emissive = glm::vec4(emissive, emissiveStrength);
    u.props = glm::vec4(metallic, roughness, ao, colormapMix);
    u.misc = glm::vec4(colormapValue, unlit ? 1.0f : 0.0f, 0, 0);
    return u;
}
Material Material::preset(std::string_view n) {
    Material m;
    // Metals: baseColor is the measured normal-incidence reflectance F0 (spec 18 §4 table).
    if (n == "copper") { m.baseColor = {0.95f, 0.64f, 0.54f, 1}; m.metallic = 1; m.roughness = 0.38f; }
    else if (n == "gold") { m.baseColor = {1.00f, 0.71f, 0.29f, 1}; m.metallic = 1; m.roughness = 0.28f; }
    else if (n == "aluminium") { m.baseColor = {0.91f, 0.92f, 0.92f, 1}; m.metallic = 1; m.roughness = 0.5f; }
    else if (n == "stainless") { m.baseColor = {0.56f, 0.57f, 0.58f, 1}; m.metallic = 1; m.roughness = 0.35f; }
    else if (n == "niobium") { m.baseColor = {0.66f, 0.65f, 0.68f, 1}; m.metallic = 1; m.roughness = 0.5f; }
    else if (n == "silicon") { m.baseColor = {0.35f, 0.37f, 0.42f, 1}; m.metallic = 0.6f; m.roughness = 0.2f; }
    else if (n == "sapphire") { m.baseColor = {0.75f, 0.85f, 1.0f, 0.55f}; m.metallic = 0; m.roughness = 0.05f; }
    else if (n == "pcb") { m.baseColor = {0.05f, 0.30f, 0.15f, 1}; m.metallic = 0; m.roughness = 0.6f; }
    else if (n == "black_anodized") { m.baseColor = {0.06f, 0.06f, 0.07f, 1}; m.metallic = 0.7f; m.roughness = 0.6f; }
    else if (n == "glass") { m.baseColor = {0.9f, 0.95f, 1.0f, 0.25f}; m.metallic = 0; m.roughness = 0.05f; }
    else if (n == "mu_metal") { m.baseColor = {0.45f, 0.45f, 0.47f, 1}; m.metallic = 0.9f; m.roughness = 0.55f; }
    else if (n == "plastic_white") { m.baseColor = {0.92f, 0.92f, 0.9f, 1}; m.metallic = 0; m.roughness = 0.7f; }
    else if (n == "led") { m.baseColor = {0.1f, 1.0f, 0.3f, 1}; m.emissive = {0.1f, 1.0f, 0.3f}; m.emissiveStrength = 4.0f; }
    return m;
}
} // namespace qlab::gfx
