#pragma once
// Internal stages of layoutChip (spec 17 §6.3), shared by ChipLayout*.cpp only.
#include "Lab/ChipLayout.hpp"

namespace qlab::lab::chipdetail {

// Positions of every site (pitch-scaled topology, or the seeded spring embedder) and pad sizes.
Status placeSites(ChipLayout& L, const hw::Device& dev, const hw::Calibration* cal,
                  const ChipLayoutOptions& o);
// One λ/4 meander per data qubit, on the side away from its neighbours (spec 17 §6.3).
void placeResonators(ChipLayout& L, const hw::Device& dev, const ChipLayoutOptions& o);
// Couplers: fixed pad-to-pad CPW segments; tunable coupler elements with stubs to both qubits.
void placeCouplers(ChipLayout& L, const hw::Device& dev, const ChipLayoutOptions& o);
// Shifts the layout so the footprint bounding box sits `edgeMargin` inside the chip, sizes the chip
// so the perimeter holds enough bond pads, and places pads on all four edges.
void frameChip(ChipLayout& L, const ChipLayoutOptions& o, std::size_t signalsNeeded);
// Feedlines (with Purcell filters), drive and flux lines routed to bond pads.
void routeLines(ChipLayout& L, const hw::Device& dev, const ChipLayoutOptions& o);
// Airbridges every `airbridgePitch` along every routed CPW and at every crossing.
void placeAirbridges(ChipLayout& L, const ChipLayoutOptions& o);
// Feedline membership: the device's lists when present, else ⌈n/8⌉ groups by position.
std::vector<std::vector<std::uint32_t>> feedlineGroups(const hw::Device& dev, const ChipLayout& L,
                                                       int maxPer);

inline glm::dvec2 unit(double angle) {
    return {std::cos(angle), std::sin(angle)};
}

} // namespace qlab::lab::chipdetail
