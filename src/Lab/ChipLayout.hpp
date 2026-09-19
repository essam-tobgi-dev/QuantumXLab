#pragma once
// Spec 17 §6.3 — chip placement from device.json. The layout is derived from the coupling graph
// and is deterministic: positions from the topology (heavy-hex, grid, linear, seeded
// Fruchterman–Reingold otherwise), Xmon pads sized from E_C, λ/4 resonator meanders on the
// device's feedlines, couplers, drive/flux lines routed to perimeter bond pads, airbridges and
// wirebonds. Coordinates are micrometres in the chip plane, origin at the lower-left corner.
#include "Core/Error.hpp"
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

namespace qlab::lab {

enum class ChipTopology { HeavyHex, Grid, Linear, SpringEmbedded };
std::string_view chipTopologyName(ChipTopology t);

struct ChipLayoutOptions {
    double pitch_um = 1000.0;        // centre-to-centre qubit spacing (spec 17 §6.3: 1.0 mm)
    double springQuantum_um = 250.0; // spring-embedder positions snap to this grid
    int springIterations = 500;
    std::uint64_t seed = 0x17C41Bull; // core::Random seed of the spring embedder
    double edgeMargin_um =
        800.0; // outermost footprint → chip edge (bond pads, Purcell filters, routing)
    double bondPadPitch_um = 200.0; // bond pads on all four edges
    double bondPadSize_um = 90.0;
    double bondPadInset_um = 150.0;   // pad centre from the chip edge
    double airbridgePitch_um = 200.0; // along every CPW (spec 17 §6.3)
    double cpwWidth_um = 10.0, cpwGap_um = 6.0;
    double armWidth_um = 24.0, padGap_um = 24.0;
    double couplerScale = 0.6; // coupler_tunable Xmon scale (descriptor)
    double meanderPitch_um = 50.0, meanderAmplitude_um = 150.0, meanderLead_um = 50.0;
    double clearance_um = 30.0;
    int maxPerFeedline = 8;      // used when the device does not list its feedlines
    double routeGrid_um = 100.0; // maze-router cell size for drive/flux/feed lines
    double epsEff = 6.45;        // (1 + ε_r)/2 for Si (spec 17 §6)
};

// Oriented rectangle footprint in the chip plane.
struct ChipRect {
    glm::dvec2 center{0.0};
    glm::dvec2 halfSize{0.0}; // along (axis, perpendicular)
    double angle_rad = 0.0;   // axis direction
    bool overlaps(const ChipRect& o, double clearance = 0.0) const;
    bool overlapsCircle(glm::dvec2 c, double r) const;
    bool contains(glm::dvec2 p, double inflate = 0.0) const;
};

struct QubitSite {
    std::uint32_t index = 0;
    bool coupler = false; // tunable-coupler element (hw::QubitKind::Coupler)
    bool tunable = false; // SQUID + flux line
    glm::dvec2 pos_um{0.0};
    double armLength_um = 150.0; // centre to arm tip (after any coupler scale)
    double armWidth_um = 24.0;
    double gap_um = 24.0;
    double radius_um() const { return armLength_um + gap_um; }
};

struct ResonatorSite {
    std::uint32_t qubit = 0;
    int feedline = -1;
    double frequency_Hz = 0.0;
    double length_um = 0.0;      // L = c / (4 f sqrt(eps_eff))
    double pitch_um = 50.0;      // meander parameters that reproduce `centerline_um`
    double amplitude_um = 150.0; // (requested amplitude; the generator widens it to hit L exactly)
    double lead_um = 50.0;
    glm::dvec2 origin_um{0.0}; // open (qubit-coupled) end
    double angle_rad = 0.0;    // meander axis
    ChipRect footprint;
    glm::dvec2 tap_um{0.0}; // shorted end, coupled to the feedline
    std::vector<glm::dvec2> centerline_um;
};

struct CpwRoute {
    std::vector<glm::dvec2> path_um;
    double length_um() const;
};

struct FeedlineSite {
    int id = 0;
    std::vector<std::uint32_t> qubits;
    CpwRoute route;   // input bond pad → taps → output bond pad
    CpwRoute purcell; // λ/4 Purcell filter at the input (spec 17 §6.3)
    int padIn = -1, padOut = -1;
};

struct CouplerSite {
    std::size_t edge = 0;
    std::uint32_t a = 0, b = 0;
    std::optional<std::uint32_t> couplerQubit;
    std::vector<CpwRoute>
        stubs; // fixed: one pad-to-pad segment; tunable: coupler pad to each qubit
};

struct ControlLine {
    std::uint32_t target = 0; // qubit (or coupler qubit) index
    bool flux = false;
    CpwRoute route; // bond pad → terminal next to the pad
    int bondPad = -1;
    bool routed = true; // false: the maze router found no path, drawn straight
};

struct AirbridgeSite {
    glm::dvec2 pos_um{0.0};
    double angle_rad = 0.0; // bridge span direction (perpendicular to the CPW)
    bool crossing = false;
};

struct BondPadSite {
    glm::dvec2 pos_um{0.0};
    int edge = 0; // 0 bottom, 1 right, 2 top, 3 left
    glm::dvec2 outward{0.0, -1.0};
    int signal = -1; // −1 ground; otherwise index into ChipLayout::signalNames
};

struct ChipLayout {
    std::string device;
    ChipTopology topology = ChipTopology::SpringEmbedded;
    glm::dvec2 size_um{0.0};
    std::vector<QubitSite> qubits; // indexed like hw::Device::qubits
    std::vector<ResonatorSite> resonators;
    std::vector<FeedlineSite> feedlines;
    std::vector<CouplerSite> couplers;
    std::vector<ControlLine> driveLines, fluxLines;
    std::vector<AirbridgeSite> airbridges;
    std::vector<BondPadSite> bondPads;
    std::vector<std::string> signalNames;
    std::vector<std::string> diagnostics;

    // Footprint conflicts: pad–pad, resonator–pad, resonator–resonator (empty when clean).
    std::vector<std::string> overlaps(double clearance_um = 0.0) const;
    // Chip-centred position (µm) used by the scene: origin at the chip centre.
    glm::dvec2 centred(glm::dvec2 p) const { return p - 0.5 * size_um; }
};

ChipTopology classifyTopology(const hw::Device& dev);

// Arm length (µm) of an Xmon whose capacitance C_Σ = e²/(2 E_C) with E_C/h ≈ |α| (transmon limit),
// from the coplanar-strip conformal-mapping estimate C' = 4 ε0 ε_eff K(k)/K(k'), k = w/(w + 2g),
// summed over four arms (Model).
double xmonArmLength_um(double anharmonicity_Hz, double armWidth_um, double gap_um, double epsEff);

Result<ChipLayout> layoutChip(const hw::Device& dev, const hw::Calibration* cal,
                              const ChipLayoutOptions& opt = {});

} // namespace qlab::lab
