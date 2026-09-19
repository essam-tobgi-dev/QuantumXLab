#pragma once
// Spec 21 §3.6 — Hinton diagram `hinton` (Simulator-only, ImGui draw list). One square per ρ_ij,
// area ∝ |ρ_ij|, colour = phase hue, on a grid with ket (rows) and bra (columns) labels. Up to 8
// qubits in full; above that the reduced ρ of the view's qubit subset. Cost O(4^k), k ≤ 8.
#include "Viz/Math/DensityPlot.hpp"
#include "Viz/StateView.hpp"
#include "Viz/Views/StateSource.hpp"

namespace qlab::viz {

class HintonView final : public StateView {
  public:
    std::string_view id() const override { return "hinton"; }
    std::string_view title() const override { return "Hinton diagram"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::DrawList; }
    std::string_view theoryAnchor() const override { return "T10 §4"; }
    ReductionRequest wants(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;

    const math::HintonModel& model() const { return model_; }
    const DensitySource& source() const { return source_; }
    // Grid geometry in body-local ImGui units (after setBodySize).
    Rect gridRect() const { return grid_; }
    float cellSize() const { return cell_; }
    Rect cellRect(std::uint32_t row, std::uint32_t col) const;

  protected:
    void rebuild(const ViewInput& in) override;
    void layout() override;
    void drawBody(DrawContext& ctx) override;

  private:
    DensitySource source_;
    math::HintonModel model_;
    Rect grid_;
    float cell_ = 0.0f;
};

} // namespace qlab::viz
