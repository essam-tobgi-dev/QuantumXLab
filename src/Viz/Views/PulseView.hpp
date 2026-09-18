#pragma once
// Spec 21 §3.14 — pulse viewer `pulses` (Physical, ImPlot). Stacked subplots, one per channel of
// the schedule (drive, control, flux, measure, acquire), sharing the time axis in ns. Each shows
// the baseband envelope Re and Im, frame phase jumps as vertical ticks, frequency changes as
// annotations and acquisition windows as shaded spans, with the playhead synced to the Lindblad
// snapshot time. The zoom level sets the decimation (spec 22 §2) and the qubit selection filters
// the channels. The layout itself is pure and lives in `viz::layout::buildPulseModel`.
#include "Viz/Layout/PulseLayout.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/StateView.hpp"

namespace qlab::viz {

class PulseView final : public StateView {
public:
    std::string_view id() const override { return "pulses"; }
    std::string_view title() const override { return "Pulses"; }
    // The schedule is what the control electronics actually plays (spec 00 §6).
    Observability observability() const override { return Observability::Physical; }
    Backend backend() const override { return Backend::ImPlot; }
    std::string_view theoryAnchor() const override { return "T03 §2"; }
    data::FidelityClass fidelity(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;
    std::string statusLine() const override;
    void frameContent() override;

    // Visible time window in ns; an empty window (t1 ≤ t0) means the whole schedule.
    void setWindow(double t0Ns, double t1Ns);
    double windowStartNs() const { return t0_; }
    double windowEndNs() const { return t1_; }
    // Amplitude and phase instead of Re and Im (spec 21 §3.14).
    void setPolar(bool on);
    bool polar() const { return polar_; }

    const layout::PulseModel& model() const { return model_; }
    std::size_t rowCount() const { return model_.rows.size(); }
    Rect plotRect() const { return plot_; }
    // Body-local y span of row `k` after the last draw.
    Rect rowRect(std::size_t k) const;

protected:
    void rebuild(const ViewInput& in) override;
    void drawBody(DrawContext& ctx) override;

private:
    layout::PulseModel model_;
    // Draw arrays per row (polar mode replaces re/im by |e| and arg e).
    struct RowPlot {
        std::vector<double> a, b;
        std::vector<double> phaseX;     // x of each frame tick
    };
    std::vector<RowPlot> plots_;
    std::string note_;
    double t0_ = 0.0, t1_ = 0.0;
    double playheadNs_ = 0.0;
    bool hasPlayhead_ = false, polar_ = false;
    float rowH_ = 0.0f;
    Rect plot_;
};

} // namespace qlab::viz
