#pragma once
// Spec 21 §3.13 — circuit diagram `circuit` (Physical, GL canvas). Shows the `ir::Circuit` of any
// compile stage (Source / Decomposed / Routed / Scheduled); the physical stages label their wires
// with the layout map and highlight the SWAPs the router inserted. Layout is by moments, or — for
// a scheduled circuit — on a time axis so that gate durations are visible; both come from the pure
// `viz::layout::layoutCircuit`, cached until the circuit changes (O(V + E) on the DAG). The
// playhead dims executed gates and outlines the current one; zoom and pan are free, and a minimap
// appears above 200 columns. Export is PNG (the canvas) or SVG (the layout).
#include "Viz/GlCanvas.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Layout/CircuitLayout.hpp"
#include "Viz/StateView.hpp"

namespace qlab::viz {

class CircuitView final : public StateView {
public:
    static constexpr std::size_t kMinimapColumns = 200;  // spec 21 §3.13
    static constexpr double kMinZoom = 6.0, kMaxZoom = 160.0;

    std::string_view id() const override { return "circuit"; }
    std::string_view title() const override { return "Circuit"; }
    Observability observability() const override { return Observability::Physical; }
    Backend backend() const override { return Backend::GlCanvas; }
    std::string_view theoryAnchor() const override { return "T01 §7"; }
    data::FidelityClass fidelity(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;
    std::string statusLine() const override;
    void frameContent() override;
    void resetCamera() override { frameContent(); }

    // ---- stage and layout mode
    void setStage(CircuitStage s);
    CircuitStage stage() const { return stage_; }
    CircuitStage shownStage() const { return shown_; }   // the stage actually available
    void setTimed(bool on);                              // time-axis layout (needs a scheduled circuit)
    bool timed() const { return timed_; }

    // ---- camera
    void setZoom(double pixelsPerUnit);
    double zoom() const { return zoom_; }
    void setPan(glm::dvec2 layoutUnits);
    glm::dvec2 pan() const { return pan_; }

    const layout::CircuitLayout& circuitLayout() const { return layout_; }
    std::optional<std::uint64_t> playhead() const { return hasPlayhead_ ? std::optional{playhead_} : std::nullopt; }
    // Layout units ↔ body-local ImGui units.
    glm::vec2 toBody(glm::dvec2 p) const;
    glm::dvec2 toLayout(glm::vec2 p) const;
    bool minimap() const { return layout_.columns > kMinimapColumns; }

    // Spec 23 §8: the diagram as a standalone SVG (wires, boxes, dots, labels) — pure, no GL.
    std::string exportSvg() const;
    Status renderPng(GlBackend& gl, const VizTheme& theme, int widthPx, int heightPx, const std::filesystem::path& png);

protected:
    void rebuild(const ViewInput& in) override;
    void layout() override;
    void drawBody(DrawContext& ctx) override;

private:
    GlCanvas::SceneFn scene(const VizTheme& theme, float pxScale) const;
    void aimCamera(glm::vec2 body);
    // Dimming of spec 21 §3.13: gates before the playhead are faded, the current one is outlined.
    float alphaOf(const layout::Glyph& g) const;
    bool isCurrent(const layout::Glyph& g) const;

    layout::CircuitLayout layout_;
    std::shared_ptr<const ir::Circuit> circuit_;
    std::vector<std::uint32_t> layoutMap_;
    std::string note_;
    CircuitStage stage_ = CircuitStage::Routed, shown_ = CircuitStage::Source;
    std::uint64_t playhead_ = 0;
    bool hasPlayhead_ = false, timed_ = false, fitPending_ = true;
    double zoom_ = 34.0;
    glm::dvec2 pan_{0.0, 0.0};   // layout units of the point drawn at the body's top-left
    mutable GlCanvas canvas_;
};

} // namespace qlab::viz
