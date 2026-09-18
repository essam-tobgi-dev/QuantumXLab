#pragma once
// Spec 21 §3.13 — circuit diagram layout (pure; no GL, no ImGui). Layout by moments: a node sits
// in the earliest moment after all its predecessors (`ir::Circuit::layers()`), and the nodes of
// one moment share a column — except where two glyphs of the same moment would cross (a CX over
// the wire of another gate), which splits the moment into sub-columns so that no two boxes ever
// overlap. With a scheduled circuit the timed layout places boxes at t_start on a time axis (ns).
// Cost O(V + E) on the DAG; the view caches the result until the circuit changes.
// Units: one wire pitch = 1.0 vertically; columns are sized by their widest label.
#include "Core/Error.hpp"
#include "IR/Circuit.hpp"
#include "Viz/Types.hpp"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::viz::layout {

enum class GlyphKind : std::uint8_t {
    Box,        // named gate box on its target wires (with control dots when it has controls)
    Cx,         // control dot(s) + target circle ⊕
    Cz,         // filled dot pair
    Swap,       // crossed wires
    Measure,    // meter box with a double line down to its classical register
    Reset,      // |0⟩ box
    Barrier,    // dashed vertical line
    Delay,      // hatched box with its duration
    Classical,  // classical assignment on the register rows
    Region      // bracketed block: if / else, while, box — encloses the glyphs of its body
};

struct Glyph {
    GlyphKind kind = GlyphKind::Box;
    ir::NodeId node = ir::kNoNode;
    std::uint32_t topoIndex = 0;          // index in the top-level topological order (the playhead unit)
    std::uint32_t depth = 0;              // nesting level (0 = top level)
    std::string label;                    // "h", "rz", "cp", "measure", header of a region
    std::string params;                   // "π/2" — spec 21 §3.13 angle rule; durations for delays
    std::vector<std::uint32_t> targetRows, controlRows;
    std::vector<std::uint8_t> negControl; // parallel to controlRows, 1 = open dot (active on |0⟩)
    std::optional<std::uint32_t> clbit;   // Measure: flat classical bit index
    std::optional<std::uint32_t> classicalRow; // row of the register the double line ends on
    std::uint32_t moment = 0, column = 0, columnSpan = 1;
    std::optional<std::uint32_t> elseColumn; // Region of a branch with an else body: where that body starts
    std::uint32_t rowMin = 0, rowMax = 0; // rows the glyph occupies (wires it crosses included)
    Rect bounds;                          // layout units: everything the glyph draws
    // The boxes themselves. Moment layout: one, equal to `bounds`. Timed layout: one per wire (a
    // two-qubit gate is two boxes joined by a line, so it never covers a concurrent gate between
    // its wires). `firstOverlap` tests these.
    std::vector<Rect> parts;
    bool routingSwap = false;             // a SWAP inserted by the router: highlighted
    std::optional<double> startNs, durationNs; // scheduled circuits
};

struct WireLabel {
    std::string name;                     // "q[1]" (register name and index) or "c" for a bit register
    std::optional<std::uint32_t> physical; // physical view: the physical qubit index
    std::optional<std::uint32_t> virtualQubit; // physical view: the program qubit that starts here
    bool classical = false;
    std::uint32_t bits = 0;               // classical rows: width of the register
};

struct CircuitLayoutOptions {
    bool timed = false;                   // time-axis layout (needs a scheduled circuit)
    std::span<const std::uint32_t> layout; // virtual → physical (labels of the physical view)
    bool markRoutingSwaps = false;        // the circuit is routed and the pre-routing stage had no swap
};

struct CircuitLayout {
    std::vector<WireLabel> rows;          // qubit wires first, then one row per classical register
    std::uint32_t qubitRows = 0;
    std::vector<Glyph> glyphs;            // in drawing order; regions before their contents
    std::vector<double> columnX, columnWidth; // moment layout: left edge and width per column
    std::size_t moments = 0;              // == circuit.layers().size()
    std::size_t columns = 0;              // drawn columns (≥ moments)
    double width = 0.0, height = 0.0;     // extent in layout units
    bool timed = false;
    double nsPerUnit = 0.0;               // timed layout: nanoseconds per layout unit
    double durationNs = 0.0;
    std::size_t routingSwaps = 0;

    double rowY(std::uint32_t row) const { return static_cast<double>(row) + 0.5; }
    // Glyph whose bounds contain the point (innermost non-region first), or null.
    const Glyph* glyphAt(double x, double y) const;
    // First pair of overlapping non-region glyph boxes, or nullopt: the layout invariant.
    std::optional<std::pair<std::size_t, std::size_t>> firstOverlap() const;
};

Result<CircuitLayout> layoutCircuit(const ir::Circuit& circuit, const CircuitLayoutOptions& options = {});

// Start time and duration per node of `circuit.topologicalOrder()` in ns, read from the
// `meta()["schedule"]` record the scheduler writes (start_ps / length_ps); empty when absent.
struct NodeTiming { double startNs = 0.0, durationNs = 0.0; };
std::vector<NodeTiming> scheduleTimes(const ir::Circuit& circuit);
// Number of `swap` gates (nested bodies included).
std::size_t countSwaps(const ir::Circuit& circuit);
// Label and parameter text of a gate node ("rz", "π/2").
std::string gateParamText(const ir::Gate& g);

} // namespace qlab::viz::layout
