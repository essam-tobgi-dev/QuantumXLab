#pragma once
// Spec 21 §3.12 and §3.8 — the two node-link views over the device layout (both GL canvas).
//
//   CouplingView       `coupling` (Physical): nodes at the device's layout coordinates coloured by
//                      a selectable calibration field (T₁, T₂, f₀₁, anharmonicity, 1q error,
//                      readout error), edges by two-qubit error or duration, each with its min/max
//                      legend; overlays for the virtual→physical mapping, the routing SWAPs pulsing
//                      along their edges as the playhead passes them, and the gates active now.
//   EntanglementView   `entanglement` (Simulator-only): the same node positions, one edge per
//                      reduced pair with width and opacity ∝ I(i:j)/2, the concurrence printed for
//                      I > 0.01, node colour by S(ρ_i).
//
// Click a node → select the qubit; click an edge → select the coupler (spec 21 §1.1).
#include "Viz/GlCanvas.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Layout/GraphLayout.hpp"
#include "Viz/StateView.hpp"

namespace qlab::viz {

// Shared geometry, drawing and hit testing of a `layout::DeviceGraph`.
class DeviceGraphView : public StateView {
public:
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;
    void frameContent() override { markLayoutDirty(); }
    void resetCamera() override { markLayoutDirty(); }

    const layout::DeviceGraph& graph() const { return graph_; }
    // Device layout units ↔ body-local ImGui units (the graph is flat: one ortho camera, no depth).
    glm::vec2 toBody(glm::dvec2 layoutPos) const;
    glm::dvec2 toLayout(glm::vec2 body) const;
    double pixelsPerUnit() const { return pxPerUnit_; }
    Status renderPng(GlBackend& gl, const VizTheme& theme, int widthPx, int heightPx, const std::filesystem::path& png);

protected:
    void layout() override;
    void drawBody(DrawContext& ctx) override;
    void onSelectionChanged(const SelectionModel&) override { canvas_.invalidate(); }
    // Filled by the concrete view in `rebuild`.
    layout::DeviceGraph graph_;
    std::string note_;
    // Extra marks the concrete view draws on top of the shared graph.
    virtual void drawOverlay(gfx::Renderer&, GlBackend&, const VizTheme&, float) const {}
    virtual void fillNodeReadout(HitResult&, const layout::GraphNode&) const {}
    virtual void fillEdgeReadout(HitResult&, const layout::GraphEdge&) const {}
    virtual bool showNodeLegend() const { return graph_.nodeLegend.valid; }
    double nodeRadius() const { return nodeRadius_; }

private:
    GlCanvas::SceneFn scene(const VizTheme& theme, const SelectionModel* selection, float pxScale) const;
    void aimCamera(glm::vec2 body);
    glm::dvec2 center_{0.0};
    double pxPerUnit_ = 1.0, nodeRadius_ = 0.32;
    mutable GlCanvas canvas_;
};

class CouplingView final : public DeviceGraphView {
public:
    std::string_view id() const override { return "coupling"; }
    std::string_view title() const override { return "Coupling graph"; }
    Observability observability() const override { return Observability::Physical; }
    Backend backend() const override { return Backend::GlCanvas; }
    std::string_view theoryAnchor() const override { return "T02 §6"; }
    // Calibration is a measured model of the device, never an exact quantity (spec 00 §5).
    data::FidelityClass fidelity(const ViewInput& in) const override;
    std::string statusLine() const override;

    void setNodeField(layout::NodeField f);
    layout::NodeField nodeField() const { return options_.nodeField; }
    void setEdgeField(layout::EdgeField f);
    layout::EdgeField edgeField() const { return options_.edgeField; }
    // Physical qubit pairs of the SWAPs the router inserted, pulsing as the playhead passes them.
    std::span<const std::pair<QubitIndex, QubitIndex>> activeEdges() const { return active_; }

protected:
    void rebuild(const ViewInput& in) override;
    void drawOverlay(gfx::Renderer& r, GlBackend& gl, const VizTheme& theme, float pxScale) const override;
    void fillNodeReadout(HitResult& h, const layout::GraphNode& n) const override;
    void fillEdgeReadout(HitResult& h, const layout::GraphEdge& e) const override;

private:
    layout::CouplingOptions options_;
    std::vector<std::uint32_t> layoutMap_;
    std::vector<std::pair<QubitIndex, QubitIndex>> active_;   // gates at the playhead
    std::vector<std::pair<QubitIndex, QubitIndex>> swaps_;    // routing SWAPs on those edges
};

class EntanglementView final : public DeviceGraphView {
public:
    std::string_view id() const override { return "entanglement"; }
    std::string_view title() const override { return "Entanglement"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::GlCanvas; }
    std::string_view theoryAnchor() const override { return "T01 §6"; }
    ReductionRequest wants(const ViewInput& in) const override;
    std::string statusLine() const override;

protected:
    void rebuild(const ViewInput& in) override;
    void drawOverlay(gfx::Renderer& r, GlBackend& gl, const VizTheme& theme, float pxScale) const override;
    void fillNodeReadout(HitResult& h, const layout::GraphNode& n) const override;
    void fillEdgeReadout(HitResult& h, const layout::GraphEdge& e) const override;
    bool showNodeLegend() const override { return true; }

private:
    std::uint32_t nQubits_ = 0;
    double totalMutualInformation_ = 0.0;
};

} // namespace qlab::viz
