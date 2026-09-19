#pragma once
// Spec 21 §3.12, §3.8 — device graph layout (pure; no GL, no ImGui). Nodes sit at the device's
// layout coordinates (`device.json` qubit `pos`, in qubit pitches); nodes are coloured by a
// selectable calibration field and edges by two-qubit error or duration, each with a min/max
// legend (spec 22 §4). The entanglement graph reuses the same node positions with edge width and
// opacity ∝ I(i:j)/2 and node colour = S(ρ_i).
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include "QEC/View.hpp"
#include "Viz/Reductions.hpp"
#include "Viz/Types.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::viz::layout {

enum class NodeField : std::uint8_t {
    None,
    T1,
    T2,
    F01,
    Anharmonicity,
    Error1q,
    ReadoutError,
    QecRole
};
enum class EdgeField : std::uint8_t { None, Error2q, Duration };
std::string_view nodeFieldName(NodeField f);
std::string_view edgeFieldName(EdgeField f);

// Linear min/max colour scale over the values present (spec 22 §4: every coloured graph shows it).
struct ScaleLegend {
    std::string title;              // "T1"
    std::string minLabel, maxLabel; // formatted with units: "48.2 µs"
    double min = 0.0, max = 0.0;
    bool valid = false; // false: no calibration, nothing is colour-coded
    double normalised(double v) const { return valid && max > min ? (v - min) / (max - min) : 0.5; }
};

struct GraphNode {
    QubitIndex qubit{0};
    glm::dvec2 pos{0.0};         // device layout coordinates (qubit pitches), y down the page
    bool coupler = false;        // tunable-coupler element: drawn small, not selectable as a qubit
    std::optional<double> value; // SI value of the selected field
    std::string valueText;       // "48.2 µs"
    glm::vec3 color{0.5f};       // display colour (sequential map, or a role colour)
    std::optional<std::uint32_t>
        virtualQubit;                   // program qubit mapped here (printed inside the node)
    std::optional<qec::QubitRole> role; // QEC overlay (spec 16 §8)
    bool syndromeFired = false;         // ancilla whose current syndrome bit is 1
};

struct GraphEdge {
    QubitIndex a{0}, b{0}; // a < b for undirected edges; control → target when directed
    bool directed = false;
    std::optional<QubitIndex> coupler;
    std::optional<double> value;
    std::string valueText;
    glm::vec3 color{0.5f};
    double weight = 1.0;               // entanglement graph: I(i:j)/2 ∈ [0, 1] (width and opacity)
    std::optional<double> concurrence; // entanglement graph label when I > 0.01
};

struct DeviceGraph {
    std::vector<GraphNode> nodes; // one per device qubit, index = qubit index
    std::vector<GraphEdge> edges;
    Rect extent; // bounding box of the node positions
    ScaleLegend nodeLegend, edgeLegend;
    std::size_t dataNodes = 0;
    // Node nearest to `p` within `radius`, and edge nearest within `tolerance` (layout units).
    std::optional<std::size_t> nodeAt(glm::dvec2 p, double radius) const;
    std::optional<std::size_t> edgeAt(glm::dvec2 p, double tolerance) const;
};

struct CouplingOptions {
    NodeField nodeField = NodeField::T1;
    EdgeField edgeField = EdgeField::Error2q;
    std::span<const std::uint32_t> layout; // virtual → physical
    const qec::CodeView* code =
        nullptr; // role colouring + live syndrome bits when bound to the chip
};
DeviceGraph buildCouplingGraph(const hw::Device& device, const hw::Calibration* calibration,
                               const CouplingOptions& options = {});

// Spec 21 §3.8: the same nodes, edges for every reduced pair, colours from the entropies.
DeviceGraph buildEntanglementGraph(const hw::Device* device, std::uint32_t nQubits,
                                   const Reductions& reductions);

// Index into the theme's colour-blind-safe qubit palette (spec 19 §1) for a QEC role: the layout
// holds no colours of its own, the view resolves this against `VizTheme::qubits`.
constexpr std::size_t paletteIndexOf(qec::QubitRole r) {
    switch (r) {
    case qec::QubitRole::Data:
        return 1; // sky blue
    case qec::QubitRole::AncillaX:
        return 0; // orange
    case qec::QubitRole::AncillaZ:
        return 2; // bluish green
    case qec::QubitRole::AncillaMixed:
        return 6; // reddish purple
    }
    return 7;
}

// Positions for a register without a device: a circle of radius n/π pitches (unit spacing).
std::vector<glm::dvec2> circlePositions(std::uint32_t n);

} // namespace qlab::viz::layout
