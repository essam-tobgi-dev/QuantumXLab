#pragma once
// Spec 21 §3.4 — Q-sphere view `qsphere` (Simulator-only, GL canvas). Basis state |i⟩ sits at the
// latitude of its Hamming weight (z = 1 − 2w/n) and at a longitude spaced uniformly among states
// of equal weight; node area ∝ |a_i|², colour = phase hue, a spoke from the centre for
// |a_i|² > ε. At most 12 qubits in full (4096 nodes); above that only the top-k nodes, and the
// view says so. Click a node → the shared basis focus (amplitude-bar filter).
// Cost per update: O(min(2^n, k)) — one threshold scan plus a partial sort.
#include "Viz/GlCanvas.hpp"
#include "Viz/Math/QSphere.hpp"
#include "Viz/StateView.hpp"

namespace qlab::viz {

class QSphereView final : public StateView {
  public:
    std::string_view id() const override { return "qsphere"; }
    std::string_view title() const override { return "Q-sphere"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::GlCanvas; }
    std::string_view theoryAnchor() const override { return "T01 §3"; }
    ReductionRequest wants(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    void frameContent() override { resetCamera(); }
    void resetCamera() override;
    std::string statusLine() const override;

    const math::QSphereModel& model() const { return model_; }
    void setOptions(const math::QSphereOptions& o);
    void setRotation(double yaw, double pitch);
    // Body-local position (ImGui units) of a quantum-frame point on the unit sphere; `depth` > 0
    // means the front hemisphere.
    glm::vec2 projectPoint(const glm::dvec3& p, double* depth = nullptr) const;
    glm::vec2 centerPx() const { return center_; }
    float radiusPx() const { return radius_; }
    Status renderPng(GlBackend& gl, const VizTheme& theme, int widthPx, int heightPx,
                     const std::filesystem::path& png);

  protected:
    void rebuild(const ViewInput& in) override;
    void layout() override;
    void drawBody(DrawContext& ctx) override;
    void onSelectionChanged(const SelectionModel&) override { canvas_.invalidate(); }

  private:
    glm::dmat3 rotation() const;
    GlCanvas::SceneFn scene(const VizTheme& theme, const SelectionModel* selection,
                            float pxScale) const;
    void aimCamera();
    math::QSphereModel model_;
    math::QSphereOptions options_;
    double yaw_ = -0.45, pitch_ = 0.32;
    glm::vec2 center_{0.0f};
    float radius_ = 0.0f;
    mutable GlCanvas canvas_;
};

} // namespace qlab::viz
