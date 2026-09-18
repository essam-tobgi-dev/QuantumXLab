// Spec 18 §4 — material table of the laboratory.
#include "Lab/Materials.hpp"
#include <array>
#include <string>

namespace qlab::lab {

namespace {
struct Entry {
    std::string_view name;
    glm::vec4 baseColor;
    float metallic;
    float roughness;
    float emissiveStrength = 0.0f;
};
// Physically based F0 / albedo values (spec 18 §4, fridge detail pass): metals carry their
// reflectance at normal incidence as the base colour, dielectrics their diffuse albedo.
constexpr std::array<Entry, 25> kMaterials{{
    {"gold_plated_cu", {1.00f, 0.71f, 0.29f, 1.0f}, 1.0f, 0.28f},
    {"copper", {0.95f, 0.64f, 0.54f, 1.0f}, 1.0f, 0.38f},
    {"aluminium", {0.91f, 0.92f, 0.92f, 1.0f}, 1.0f, 0.55f},   // bead-blasted
    {"stainless", {0.56f, 0.57f, 0.58f, 1.0f}, 1.0f, 0.35f},   // brushed
    {"nickel_plated", {0.66f, 0.61f, 0.53f, 1.0f}, 1.0f, 0.30f},
    {"g10", {0.58f, 0.57f, 0.40f, 1.0f}, 0.0f, 0.55f},          // glass-epoxy laminate (olive, not lime)
    {"mu_metal", {0.55f, 0.55f, 0.58f, 1.0f}, 1.0f, 0.60f},
    {"niobium_film", {0.62f, 0.63f, 0.70f, 1.0f}, 1.0f, 0.20f},
    {"silicon", {0.25f, 0.26f, 0.30f, 1.0f}, 0.0f, 0.15f},
    {"sapphire", {0.75f, 0.85f, 1.0f, 0.55f}, 0.0f, 0.05f},
    {"pcb_green", {0.05f, 0.30f, 0.12f, 1.0f}, 0.0f, 0.60f},
    {"rack_black", {0.05f, 0.05f, 0.06f, 1.0f}, 0.0f, 0.70f},
    {"plastic_grey", {0.40f, 0.40f, 0.42f, 1.0f}, 0.0f, 0.80f},
    {"glass", {0.90f, 0.95f, 1.0f, 0.25f}, 0.0f, 0.05f},
    {"eccosorb", {0.03f, 0.03f, 0.03f, 1.0f}, 0.0f, 0.95f},
    {"cuni", {0.72f, 0.68f, 0.62f, 1.0f}, 1.0f, 0.45f},
    {"nbti", {0.70f, 0.72f, 0.76f, 1.0f}, 1.0f, 0.35f},
    {"phosphor_bronze", {0.80f, 0.65f, 0.40f, 1.0f}, 1.0f, 0.50f},
    {"ptfe", {0.95f, 0.95f, 0.93f, 1.0f}, 0.0f, 0.35f},
    {"floor", {0.62f, 0.63f, 0.65f, 1.0f}, 0.0f, 0.25f},        // epoxy resin: soft reflections
    {"wall", {0.80f, 0.80f, 0.78f, 1.0f}, 0.0f, 0.85f},
    {"desk", {0.45f, 0.35f, 0.28f, 1.0f}, 0.0f, 0.70f},
    {"glow", {1.0f, 0.85f, 0.45f, 1.0f}, 0.0f, 0.40f, 6.0f},
    {"light_panel", {1.0f, 0.98f, 0.92f, 1.0f}, 0.0f, 0.30f, 12.0f}, // ceiling luminaire diffuser
    // Parts whose appearance comes from vertex colours (rack faceplates, panels, chip films).
    {"vertex", {1.0f, 1.0f, 1.0f, 1.0f}, 0.25f, 0.55f},
}};

} // namespace

gfx::Material labMaterial(std::string_view name) {
    gfx::Material m;
    for (const auto& e : kMaterials) {
        if (e.name != name) continue;
        m.baseColor = e.baseColor;
        m.metallic = e.metallic;
        m.roughness = e.roughness;
        if (e.emissiveStrength > 0.0f) {
            m.emissive = glm::vec3(e.baseColor);
            m.emissiveStrength = e.emissiveStrength;
        }
        return m;
    }
    m.baseColor = {0.6f, 0.6f, 0.62f, 1.0f};
    m.metallic = 0.2f;
    m.roughness = 0.6f;
    return m;
}

bool isKnownMaterial(std::string_view name) {
    for (const auto& e : kMaterials)
        if (e.name == name) return true;
    return false;
}

std::string_view coaxMaterial(std::string_view coaxId) {
    if (coaxId.rfind("SS", 0) == 0) return "stainless";
    if (coaxId.rfind("CuNi", 0) == 0) return "cuni";
    if (coaxId.rfind("NbTi", 0) == 0) return "nbti";
    if (coaxId.rfind("Cu", 0) == 0) return "copper";
    if (coaxId.rfind("DC", 0) == 0) return "phosphor_bronze";
    return "stainless";
}

double coaxRadius_m(std::string_view coaxId) {
    if (coaxId.find("219") != std::string_view::npos) return 0.5 * 2.19e-3;
    if (coaxId.rfind("DC", 0) == 0) return 1.2e-3; // one insulated pair of the 12-way loom ribbon
    return 0.5 * 0.86e-3;
}

} // namespace qlab::lab
