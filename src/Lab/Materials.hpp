#pragma once
// Spec 18 §4 — material presets used by the laboratory. Names follow the spec table
// (gold_plated_cu, copper, aluminium, stainless, mu_metal, niobium_film, silicon, pcb_green,
// rack_black, plastic_grey, glass, eccosorb) plus lab-specific finishes; parts whose appearance is
// carried by vertex colours use "vertex".
#include "Graphics/Material.hpp"
#include <string_view>

namespace qlab::lab {

gfx::Material labMaterial(std::string_view name);
bool isKnownMaterial(std::string_view name);

// Coax catalog id (cryo::Element::coax, e.g. "SS_086", "CuNi_219", "NbTi_086") → material name.
std::string_view coaxMaterial(std::string_view coaxId);
// Outer radius (m) of a coax catalog id: 0.86 mm or 2.19 mm diameter (spec 17 §3.2); a DC loom is
// drawn as a ribbon of 1.2 mm pairs (Tube `bundle`).
double coaxRadius_m(std::string_view coaxId);

} // namespace qlab::lab
