#pragma once
// Spec 21 §3.5 — density-matrix city plot `city` (Simulator-only, GL canvas). A 3D bar chart of
// Re ρ_ij, with Im, |ρ_ij| and arg ρ_ij (as colour) selectable; axes labelled with kets and bras,
// the diagonal highlighted, hover reads ρ_ij. Up to 8 qubits in full (256² bars); above that the
// view shows the reduced ρ of its qubit subset. Cost O(4^k), k ≤ 8.
#include "Viz/GlCanvas.hpp"
#include "Viz/Math/DensityPlot.hpp"
#include "Viz/StateView.hpp"
#include "Viz/Views/StateSource.hpp"

namespace qlab::viz {

class CityView final : public StateView {
  public:
    std::string_view id() const override { return "city"; }
    std::string_view title() const override { return "Density matrix"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::GlCanvas; }
    std::string_view theoryAnchor() const override { return "T01 §5"; }
    ReductionRequest wants(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;
    std::string statusLine() const override;
    void frameContent() override { resetCamera(); }
    void resetCamera() override;

    void setQuantity(math::CityQuantity q);
    math::CityQuantity quantity() const { return quantity_; }
    void setRotation(double yaw, double pitch);

    const math::CityModel& model() const { return model_; }
    const DensitySource& source() const { return source_; }
    // Body-local position of a matrix element's bar top (ImGui units); nullopt when behind the
    // camera.
    std::optional<glm::vec2> projectBar(std::uint32_t row, std::uint32_t col) const;
    Status renderPng(GlBackend& gl, const VizTheme& theme, int widthPx, int heightPx,
                     const std::filesystem::path& png);

  protected:
    void rebuild(const ViewInput& in) override;
    void layout() override;
    void drawBody(DrawContext& ctx) override;

  private:
    GlCanvas::SceneFn scene(const VizTheme& theme, float pxScale) const;
    void aimCamera(glm::vec2 body);
    // Grid coordinates of element (i, j) in the renderer's world frame (y is up, bars grow in +y).
    glm::dvec3 barBase(std::uint32_t row, std::uint32_t col) const;

    DensitySource source_;
    math::CityModel model_;
    math::CityQuantity quantity_ = math::CityQuantity::Real;
    double yaw_ = -0.9, pitch_ = 0.62;
    double cell_ = 1.0, heightScale_ = 1.0;
    mutable GlCanvas canvas_;
};

} // namespace qlab::viz
