#pragma once
// Spec 21 §3.7 — probability histogram `histogram` (Physical, ImPlot). Bars of p̂_i = c_i/N with
// the Wilson 68.3 % interval (§2.5), the exact Born probabilities as a dashed overlay when the run
// had a state-vector or density-matrix backend, sort and cumulative toggles, a marginal over
// chosen bits, and the Hellinger and total-variation distances printed with the shot count.
// Register labels are little-endian with "bit 0 is rightmost" in the axis title. Cost
// O(#outcomes); at most 4096 are shown, the rest aggregated as "other".
#include "Viz/Math/Statistics.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/StateView.hpp"

namespace qlab::viz {

class HistogramView final : public StateView {
public:
    std::string_view id() const override { return "histogram"; }
    std::string_view title() const override { return "Probabilities"; }
    // The bars are shot counts: a physical machine produces exactly this (spec 00 §6).
    Observability observability() const override { return Observability::Physical; }
    Backend backend() const override { return Backend::ImPlot; }
    std::string_view theoryAnchor() const override { return "T09 §2"; }
    data::FidelityClass fidelity(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;
    std::string statusLine() const override;

    // ---- options (spec 21 §3.7)
    void setOrder(math::HistogramOrder order);
    math::HistogramOrder order() const { return options_.order; }
    void setCumulative(bool on);
    bool cumulative() const { return cumulative_; }
    void setMarginalBits(std::vector<std::size_t> bits);   // bit 0 is qubit 0 (rightmost in the label)
    std::span<const std::size_t> marginalBits() const { return options_.marginalBits; }

    const math::HistogramModel& model() const { return model_; }
    const std::string& note() const { return note_; }
    Rect plotRect() const { return plot_; }

protected:
    void rebuild(const ViewInput& in) override;
    void drawBody(DrawContext& ctx) override;

private:
    math::HistogramOptions options_;
    math::HistogramModel model_;
    std::string note_;
    bool cumulative_ = false;
    // Draw arrays, parallel to `model_.bars`.
    std::vector<double> x_, y_, neg_, pos_, ideal_, idealX_;
    Rect plot_;
};

} // namespace qlab::viz
