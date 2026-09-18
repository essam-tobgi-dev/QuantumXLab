#pragma once
// Spec 21 §1 — shared vocabulary of qlab::viz: observability, drawing backend, hit results and
// plain geometry. No GL, no ImGui: everything here is usable from headless tests.
#include "Core/StrongType.hpp"
#include "Data/Fidelity.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qlab::viz {

using data::FidelityClass;

// Spec 00 §6 / 21 §1: a Simulator-only view shows something a physical machine cannot give
// without tomography; it carries the badge and is hidden by the "Physical lab" preset.
enum class Observability : std::uint8_t { Physical, SimulatorOnly };
constexpr std::string_view observabilityName(Observability o) {
    return o == Observability::Physical ? "Physical" : "Simulator-only";
}

// Spec 21 §1.2 drawing backends.
enum class Backend : std::uint8_t { ImPlot, GlCanvas, DrawList, Table };
constexpr std::string_view backendName(Backend b) {
    switch (b) {
    case Backend::ImPlot: return "ImPlot";
    case Backend::GlCanvas: return "GL canvas";
    case Backend::DrawList: return "ImGui draw list";
    case Backend::Table: return "ImGui table";
    }
    return "?";
}

// Axis-aligned rectangle in view-local units (pixels for draw-list views, layout units for the
// circuit diagram). Half-open on neither side: touching rectangles do not overlap.
struct Rect {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    constexpr double width() const { return x1 - x0; }
    constexpr double height() const { return y1 - y0; }
    constexpr double cx() const { return 0.5 * (x0 + x1); }
    constexpr double cy() const { return 0.5 * (y0 + y1); }
    constexpr bool contains(double x, double y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    constexpr bool overlaps(const Rect& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
    constexpr Rect inset(double d) const { return {x0 + d, y0 + d, x1 - d, y1 - d}; }
    constexpr Rect unite(const Rect& o) const {
        return {std::min(x0, o.x0), std::min(y0, o.y0), std::max(x1, o.x1), std::max(y1, o.y1)};
    }
};

// One line of a hover readout card (spec 21 §4): values are already formatted to 4 significant
// figures with their unit; `cls` is the fidelity class of that line.
struct ReadoutRow {
    std::string label;
    std::string value;
    FidelityClass cls = FidelityClass::Exact;
};

enum class HitKind : std::uint8_t {
    None, Qubit, Edge, BasisState, MatrixElement, Gate, Channel, Sample, Row
};

// What lies under a view-local position (spec 21 §1 `hitTest`). Only the fields that apply to
// `kind` are set. The readout is what the hover card shows (spec 21 §4).
struct HitResult {
    HitKind kind = HitKind::None;
    std::optional<QubitIndex> qubit;                          // Qubit, Sample
    std::optional<std::pair<QubitIndex, QubitIndex>> edge;    // Edge (a < b)
    std::optional<std::uint64_t> basisIndex;                  // BasisState (little-endian index)
    std::optional<std::pair<std::uint32_t, std::uint32_t>> element; // MatrixElement (row, column)
    std::optional<std::uint32_t> gate;                        // Gate: index in topological order
    std::optional<std::uint32_t> row;                         // Channel / Row / Sample series
    std::string title;
    std::vector<ReadoutRow> readout;
    explicit operator bool() const { return kind != HitKind::None; }
};

// Error codes: spec 04 §2 gives Viz no block of its own, so the generic codes are used
// (InvalidArgument, OutOfRange, Unsupported, NotFound).

} // namespace qlab::viz
