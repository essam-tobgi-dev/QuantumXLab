#pragma once
// Spec 21 §3.17 — Wigner function `wigner` (Simulator-only, ImPlot heatmap). W(α) of an oscillator
// mode (readout resonator, cavity, ion motional mode) whose ρ is given in a Fock basis truncated at
// N_max ≤ 60, on a grid ≤ 201 × 201, drawn with the diverging map so that W = 0 is the neutral
// colour; the negativity ∫|W| d²α − 1 is printed and the photon-number distribution P(n) = ρ_nn is
// plotted beside it. The grid is the run's work: the view asks through `wants()` and computes it
// inline only when the product N² · n_x · n_y is small enough for the UI thread (spec 21 §2.3).
#include "Data/Fidelity.hpp"
#include "Viz/Math/Wigner.hpp"
#include "Viz/StateView.hpp"
#include <memory>

namespace qlab::viz {

class WignerView final : public StateView {
  public:
    // Inline budget: ρ elements × grid points. 60² × 101² ≈ 3.7 × 10⁷ is far too much for a frame;
    // this bound keeps the inline path near a millisecond.
    static constexpr std::size_t kInlineWork = 400000;

    std::string_view id() const override { return "wigner"; }
    std::string_view title() const override { return "Wigner function"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::ImPlot; }
    std::string_view theoryAnchor() const override { return "T07 §3"; }
    ReductionRequest wants(const ViewInput& in) const override;
    data::FidelityClass fidelity(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;
    std::string statusLine() const override;

    // The mode the view shows: ρ in the Fock basis, and a label for the header ("readout r0").
    void setMode(std::shared_ptr<const num::Matrix> rho, std::string label = {});
    const std::shared_ptr<const num::Matrix>& mode() const { return mode_; }
    void setAxes(const math::WignerOptions& axes);
    const math::WignerOptions& axes() const { return options_; }

    const std::optional<math::WignerGrid>& grid() const { return grid_; }
    std::span<const double> photonNumbers() const { return photons_; }
    const std::string& note() const { return note_; }
    Rect plotRect() const { return plot_; }

  protected:
    void rebuild(const ViewInput& in) override;
    void drawBody(DrawContext& ctx) override;

  private:
    std::shared_ptr<const num::Matrix> mode_;
    std::string modeLabel_;
    math::WignerOptions options_;
    std::optional<math::WignerGrid> grid_;
    std::vector<double> photons_;
    std::vector<double> heat_; // row-major with iy = 0 at the TOP, as ImPlot::PlotHeatmap wants
    std::string note_;
    Rect plot_;
};

} // namespace qlab::viz
