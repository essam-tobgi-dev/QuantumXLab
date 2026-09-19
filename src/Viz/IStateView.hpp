#pragma once
// Spec 21 §1 — the view interface. A view consumes an immutable `ViewInput`, computes
// presentation only (projections, layout, colours) within the §3 cost bound, draws into an ImGui
// child region (ImPlot, a GlCanvas image, or the draw list), and answers hit tests.
// No ImGui type appears here: ImGui and ImPlot are included by this module's .cpp files only.
#include "Data/Fidelity.hpp"
#include "Viz/Reductions.hpp"
#include "Viz/Selection.hpp"
#include "Viz/Theme.hpp"
#include "Viz/Types.hpp"
#include "Viz/ViewInput.hpp"
#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace qlab::viz {

class GlBackend;

// Export button of the view header (spec 21 §4, 23 §8). The view fills the request; the host
// (UI / Report) captures `screenRect` for a PNG or writes `csv` to the path the user picks.
struct ExportRequest {
    enum class Format : std::uint8_t { Png, Csv };
    std::string viewId;
    Format format = Format::Png;
    Rect screenRect; // body of the view in ImGui screen coordinates
    std::string csv; // tabular data when format == Csv
};

// Everything a view needs from its host while drawing. The host owns all of it.
struct DrawContext {
    const VizTheme* theme = nullptr;     // required
    SelectionModel* selection = nullptr; // the one app-level selection (spec 21 §1.1); may be null
    GlBackend* gl = nullptr;             // null without a GL context: GL views draw a placeholder
    float dpiScale = 1.0f;               // framebuffer pixels per ImGui unit (spec 19 §7)
    double timeS = 0.0;                  // UI clock: hover delay, playhead animation
    bool asciiKets = false;              // "|01>" when the UI font has no U+27E9
    bool reducedMotion = false;          // spec 19 §8
    bool showHeader = true;              // badges, status, export, "?" (spec 21 §4)
    std::function<void(std::string_view anchor)> openTheory; // "?" link → Theory Browser
    std::function<void(const ExportRequest&)> requestExport; // export button
};

class IStateView {
  public:
    using HitCallback = std::function<void(const HitResult&)>;
    virtual ~IStateView() = default;

    // ---- identity (spec 21 §1, §3)
    virtual std::string_view id() const = 0;           // "bloch", "qsphere", …
    virtual std::string_view title() const = 0;        // "Bloch sphere"
    virtual Observability observability() const = 0;   // Simulator-only views carry the badge
    virtual Backend backend() const = 0;               // drawing backend of spec 21 §1.2
    virtual std::string_view theoryAnchor() const = 0; // target of the "?" link, e.g. "T01 §5"
    // Weakest class of what the view would show for these inputs (spec 00 §5).
    virtual data::FidelityClass fidelity(const ViewInput& in) const = 0;

    // ---- data flow
    // What the run must reduce for this view (merged by the App, computed by `computeReductions`).
    virtual ReductionRequest wants(const ViewInput& in) const = 0;
    // At most once per UI frame, main thread, within the cost bound of spec 21 §3. Cheap when the
    // input did not change.
    virtual void update(const ViewInput& in) = 0;
    // True while the shown reductions belong to an older snapshot (spec 21 §1 "stale" indicator).
    virtual bool stale() const = 0;

    // ---- drawing and interaction
    virtual void draw(DrawContext& ctx) = 0;
    // Size of the body the view lays itself out in, in ImGui units. `draw` sets it from the region
    // it is given; headless callers set it before `hitTest`.
    virtual void setBodySize(glm::vec2 size) = 0;
    virtual glm::vec2 bodySize() const = 0;
    // What lies under a body-local position (origin top-left, ImGui units), with its readout.
    virtual std::optional<HitResult> hitTest(glm::vec2 local) const = 0;
    virtual void setOnHover(HitCallback fn) = 0;
    virtual void setOnClick(HitCallback fn) = 0;
    // Dispatches a click as `draw` does: selects the qubit / edge in the shared model and calls the
    // click callback. Returns the hit.
    virtual std::optional<HitResult> click(glm::vec2 local, SelectionModel* selection,
                                           bool additive = false) = 0;

    // ---- qubit subset the view is restricted to (empty = all; tiles have a subset selector)
    virtual void setQubitSubset(std::span<const QubitIndex> qubits) = 0;
    virtual std::span<const QubitIndex> qubitSubset() const = 0;

    // ---- header text and export
    virtual std::string statusLine() const = 0;               // "gate 12 · t = 320 ns"
    virtual std::optional<std::string> exportCsv() const = 0; // tabular views only
    // `F` frames the content, `R` resets the camera (GL views; no-ops elsewhere) — spec 21 §4.
    virtual void frameContent() = 0;
    virtual void resetCamera() = 0;
};

} // namespace qlab::viz
