#pragma once
// Spec 21 §3.3 — phase disk `phasedisk` (Simulator-only, ImGui draw list). One disc per basis
// state on a grid: radius ∝ |a_i|, a radial line at arg a_i, fill = phase hue (§2.4). Selection
// rules are those of §3.2 (threshold, top-k, order). Made for QFT and phase estimation, where the
// magnitudes are uniform and only the phases differ. Hover and click as §3.2.
#include "Viz/Math/Amplitudes.hpp"
#include "Viz/StateView.hpp"
#include "Viz/Views/StateSource.hpp"

namespace qlab::viz {

class PhaseDiskView final : public StateView {
public:
    std::string_view id() const override { return "phasedisk"; }
    std::string_view title() const override { return "Phase disks"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::DrawList; }
    std::string_view theoryAnchor() const override { return "T01 §3"; }
    ReductionRequest wants(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;
    std::string statusLine() const override;

    void setFilter(const math::AmplitudeFilter& f);
    const math::AmplitudeFilter& filter() const { return filter_; }

    const math::AmplitudeSelection& selection() const { return selection_; }
    std::size_t columns() const { return columns_; }
    float cellSize() const { return cell_; }
    // Centre of disc `k` in body-local ImGui units (after setBodySize).
    glm::vec2 discCenter(std::size_t k) const;
    float discRadius(std::size_t k) const;

protected:
    void rebuild(const ViewInput& in) override;
    void layout() override;
    void drawBody(DrawContext& ctx) override;

private:
    math::AmplitudeFilter filter_;
    math::AmplitudeSelection selection_;
    std::string note_;
    std::uint32_t nQubits_ = 0;
    std::size_t columns_ = 1, rows_ = 1;
    float cell_ = 0.0f;
    Rect grid_;
};

} // namespace qlab::viz
