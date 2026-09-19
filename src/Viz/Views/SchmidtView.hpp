#pragma once
// Spec 21 §3.9 — Schmidt spectrum `schmidt` (Simulator-only, ImPlot). For a user-chosen
// bipartition A | Ā: bars of the Schmidt coefficients λ_k in descending order, with
// S_A = −Σ λ_k² log₂ λ_k² and the Schmidt rank printed. The SVD of the 2^|A| × 2^(n−|A|) reshaped
// state is the run's work (|A| ≤ 14): the view asks for it through `wants()` and shows the previous
// spectrum with the stale badge until the new one arrives.
#include "Viz/Math/Reduced.hpp"
#include "Viz/StateView.hpp"

namespace qlab::viz {

class SchmidtView final : public StateView {
  public:
    std::string_view id() const override { return "schmidt"; }
    std::string_view title() const override { return "Schmidt spectrum"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::ImPlot; }
    std::string_view theoryAnchor() const override { return "T01 §6"; }
    ReductionRequest wants(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;
    std::string statusLine() const override;

    // The bipartition A (empty = qubit 0 alone, the default cut). Same as `setQubitSubset`.
    void setPartition(std::span<const QubitIndex> a) { setQubitSubset(a); }
    std::span<const QubitIndex> partition() const { return partition_; }
    const std::optional<math::SchmidtSpectrum>& spectrum() const { return spectrum_; }
    const std::string& note() const { return note_; }
    Rect plotRect() const { return plot_; }
    // Bars are drawn on a log axis when the spectrum spans more than this many decades.
    void setLogScale(bool on);
    bool logScale() const { return log_; }

  protected:
    void rebuild(const ViewInput& in) override;
    void drawBody(DrawContext& ctx) override;

  private:
    std::vector<QubitIndex> defaultPartition(std::uint32_t n) const;
    std::vector<QubitIndex> partition_;
    std::optional<math::SchmidtSpectrum> spectrum_;
    std::vector<double> x_, y_;
    std::string note_;
    bool log_ = false;
    Rect plot_;
};

} // namespace qlab::viz
