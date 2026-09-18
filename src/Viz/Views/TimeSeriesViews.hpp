#pragma once
// Spec 21 §3.15–3.16 — the two time-domain views (both Simulator-only, ImPlot).
//
//   PopulationsView  P_j(t) = ⟨j|ρ_k(t)|j⟩ for j = 0, 1, 2 of one qubit from a Lindblad snapshot
//                    series (the j = 2 curve is leakage), the drive envelope faintly underneath,
//                    and |r|(t) and Tr ρ²(t) on a second axis.
//   TrajectoryView   individual Monte-Carlo trajectories' ⟨Z_k⟩(t) as faint lines with their
//                    quantum-jump times as markers, and the ensemble mean as a bold line inside a
//                    standard-error band. At most 256 trajectories are drawn; the rest contribute
//                    to the mean only.
#include "Viz/Layout/PulseLayout.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/StateView.hpp"

namespace qlab::viz {

class PopulationsView final : public StateView {
public:
    std::string_view id() const override { return "populations"; }
    std::string_view title() const override { return "Populations"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::ImPlot; }
    std::string_view theoryAnchor() const override { return "T05 §2"; }
    data::FidelityClass fidelity(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;
    std::string statusLine() const override;

    std::uint32_t site() const { return site_; }
    void setSite(std::uint32_t site);
    std::span<const double> timeNs() const { return t_; }
    std::span<const double> level(std::size_t j) const;
    std::size_t levels() const { return levels_.size(); }
    Rect plotRect() const { return plot_; }

protected:
    void rebuild(const ViewInput& in) override;
    void drawBody(DrawContext& ctx) override;

private:
    std::vector<double> t_;
    std::vector<std::vector<double>> levels_;  // [level][sample]
    std::vector<double> purity_, blochNorm_;
    std::vector<double> driveT_, driveA_;      // drive envelope drawn faintly under the curves
    std::string note_;
    std::uint32_t site_ = 0;
    double playheadNs_ = 0.0;
    bool hasPlayhead_ = false;
    Rect plot_;
};

class TrajectoryView final : public StateView {
public:
    static constexpr std::size_t kMaxDrawn = 256;   // spec 21 §3.16

    std::string_view id() const override { return "trajectories"; }
    std::string_view title() const override { return "Trajectories"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::ImPlot; }
    std::string_view theoryAnchor() const override { return "T05 §5"; }
    data::FidelityClass fidelity(const ViewInput&) const override { return data::FidelityClass::Statistical; }
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;
    std::string statusLine() const override;

    std::size_t drawnTraces() const { return drawn_; }
    std::uint64_t trajectories() const { return trajectories_; }
    std::span<const double> meanZ() const { return meanZ_; }
    std::span<const double> timeNs() const { return t_; }
    Rect plotRect() const { return plot_; }

protected:
    void rebuild(const ViewInput& in) override;
    void drawBody(DrawContext& ctx) override;

private:
    struct Trace {
        std::vector<double> t, z;
        std::vector<double> jumpT, jumpZ;
    };
    std::vector<Trace> traces_;
    std::vector<double> t_, meanZ_, lo_, hi_;
    std::string note_;
    std::size_t drawn_ = 0;
    std::uint64_t trajectories_ = 0;
    std::uint32_t qubit_ = 0;
    Rect plot_;
};

} // namespace qlab::viz
