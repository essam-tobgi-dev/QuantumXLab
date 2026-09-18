#pragma once
// Spec 07 §5.1, T04 §7 — Monte-Carlo wave function (quantum trajectories).
// Evolves |ψ⟩ under H_eff = H − (i/2) Σ_k L_k†L_k with jumps when the norm crosses a per-trajectory
// random threshold; observables are trajectory averages with a reported standard error.
#include "QSim/Backend.hpp"
#include "QSim/SystemModel.hpp"

namespace qlab::qsim {

struct TrajectorySettings {
    std::uint64_t trajectories = 1000;
    double stepS = 10e-12;   // maximum RK4 step
    double sampleS = 1e-9;   // segment grid (AWG sample spacing); averages recorded at each segment end
    bool recordAverages = true; // false: only t = 0 and the end of the run are recorded
};

// Averaged observable with its Monte-Carlo standard error σ/√N (class Statistical).
struct AveragedSample {
    double timeS = 0.0;
    std::vector<double> populations;   // site-major [site][level]
    std::vector<double> stderrs;       // same layout
    double leakage = 0.0;
};

class TrajectoriesBackend final : public IBackend {
public:
    static constexpr std::uint32_t kMaxSites = 8;
    static constexpr std::size_t kMaxDim = 256; // 2^8 (spec 07 §5.1: d = 2, n ≤ 8)

    Capabilities capabilities() const override;
    Kind kind() const override { return Kind::Trajectories; }
    std::uint32_t nQubits() const override { return n_; }
    std::uint32_t levels() const override { return levels_; }

    Status allocate(std::uint32_t nQubits, std::uint32_t levelsPerSite = 2) override;
    Status reset(std::span<const QubitIndex> qubits, core::Random& rng) override;
    Status applyGate(const Matrix& u, std::span<const QubitIndex> targets) override;
    Status applyControlled(const Matrix& u, std::span<const QubitIndex> controls,
                           std::span<const QubitIndex> targets) override;
    Status applyChannel(const Kraus& kraus, std::span<const QubitIndex> targets) override;
    Result<Outcome> measure(std::span<const QubitIndex> qubits, core::Random& rng) override;
    Result<double> expectation(const PauliString& p) const override;
    Result<Probabilities> probabilities(std::span<const QubitIndex> qubits) const override;
    Result<Snapshot> snapshot(const SnapshotRequest& req) const override;
    Result<Counts> sample(std::span<const QubitIndex> qubits, std::uint64_t shots, core::Random& rng) const override;
    std::size_t bytesAllocated() const override { return psi_.size() * sizeof(Complex); }
    double stateNorm() const override;
    std::unique_ptr<IBackend> clone() const override;

    Status setModel(SystemModel model);
    const SystemModel& model() const { return model_; }
    void setSettings(const TrajectorySettings& s) { settings_ = s; }
    const TrajectorySettings& settings() const { return settings_; }

    // Run `settings().trajectories` independent trajectories from |0…0⟩ over `durationS`, trajectory i
    // drawing from rng.stream(i). The stored state is the last trajectory's; averages live in
    // samples(), whose last entry is always the end of the run.
    Status runEnsemble(double durationS, core::Random& rng);
    std::span<const AveragedSample> samples() const { return samples_; }
    // Ensemble-averaged populations at the end of the run.
    double population(std::uint32_t site, std::uint32_t level) const;
    double populationStdErr(std::uint32_t site, std::uint32_t level) const;

private:
    Status oneTrajectory(std::span<const double> segmentEnds, const std::vector<bool>& recordAt, core::Random& rng,
                         std::span<double> pops);
    void applyHeff(double t, std::span<const Complex> in, std::span<Complex> out);
    void hamiltonianAt(double t, Matrix& h) const;
    void writePopulations(std::span<double> row) const;
    std::uint32_t n_ = 0, levels_ = 2;
    std::size_t D_ = 0;
    SystemModel model_;
    TrajectorySettings settings_;
    num::Vector psi_;
    Matrix heffCache_;
    Matrix lDagLSum_;                  // Σ_k L_k†L_k
    double segLo_ = 0.0, segHi_ = 0.0; // envelope sampling window of the current grid segment
    std::vector<AveragedSample> samples_;
    bool allocated_ = false;
};

} // namespace qlab::qsim
