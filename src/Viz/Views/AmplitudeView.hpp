#pragma once
// Spec 21 §3.2 — amplitude bars `amplitudes` (Simulator-only, ImPlot). One bar per basis state,
// height |a_i| (toggle |a_i|²), fill colour = phase hue (§2.4), label |i⟩ in binary little-endian
// (decimal optional). Filters: threshold |a_i|² > ε (default 1e-4), order by index or magnitude,
// restrict to a qubit subset — which is a marginal distribution and therefore has no phase.
// Up to 20 qubits are read here; above that the run supplies the top-k (k = 256) and the view
// prints the probability mass shown. Cost O(min(2^n, k)).
#include "Viz/Math/Amplitudes.hpp"
#include "Viz/StateView.hpp"
#include "Viz/Views/StateSource.hpp"

namespace qlab::viz {

class AmplitudeView final : public StateView {
  public:
    std::string_view id() const override { return "amplitudes"; }
    std::string_view title() const override { return "Amplitudes"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::ImPlot; }
    std::string_view theoryAnchor() const override { return "T01 §3"; }
    ReductionRequest wants(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;
    std::string statusLine() const override;

    // ---- options (spec 21 §3.2)
    void setShowProbability(bool on); // bar height |a_i|² instead of |a_i|
    bool showProbability() const { return probability_; }
    void setFilter(const math::AmplitudeFilter& f);
    const math::AmplitudeFilter& filter() const { return filter_; }
    void setDecimalLabels(bool on);
    bool decimalLabels() const { return decimal_; }

    // ---- model
    const math::AmplitudeSelection& selection() const { return selection_; }
    bool marginal() const { return marginal_; } // a qubit subset is set: probabilities only
    const std::string& note() const { return note_; }
    std::size_t barCount() const { return selection_.entries.size(); }
    // Plot area in body-local ImGui units, filled by the last draw (hit testing uses it).
    Rect plotRect() const { return plot_; }

  protected:
    void rebuild(const ViewInput& in) override;
    void drawBody(DrawContext& ctx) override;

  private:
    std::string label(std::uint64_t index) const;
    math::AmplitudeFilter filter_;
    math::AmplitudeSelection selection_;
    std::vector<double> heights_;   // bar heights in display order
    std::vector<double> positions_; // x of each bar (0, 1, 2, …)
    std::string note_;
    std::uint32_t nQubits_ = 0;
    bool probability_ = false, decimal_ = false, marginal_ = false, fromRun_ = false;
    Rect plot_;
};

} // namespace qlab::viz
