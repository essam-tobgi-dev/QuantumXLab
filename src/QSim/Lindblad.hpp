#pragma once
// Spec 07 §5 — pulse-level Lindblad backend.
//   dρ/dt = −i[H(t), ρ] + Σ_k (L_k ρ L_k† − ½{L_k†L_k, ρ})     (ħ = 1; H in rad/s)
// H(t) = H0 + Σ_j (Re Ω_j(t) A_j + Im Ω_j(t) B_j) with the envelopes of a pulse::Schedule.
#include "QSim/Backend.hpp"
#include "QSim/SystemModel.hpp"
#include <vector>

namespace qlab::qsim {

enum class LindbladIntegrator { Rk4, Dopri5, Magnus2 };

// Every integrator advances segment by segment on the `sampleS` grid (the AWG sample spacing), so
// recorded samples and piecewise-constant envelopes coincide with step boundaries.
struct LindbladSettings {
    LindbladIntegrator integrator =
        LindbladIntegrator::Rk4; // spec 07 §5 default; Dopri5 MAY be selected
    double stepS = 10e-12;       // RK4 / Magnus2 maximum step (spec 07 §5: 10 ps)
    double sampleS = 1e-9;       // segment grid; ρ is recorded at each segment end (spec 12 §3)
    double rtol = 1e-9, atol = 1e-12; // Dopri5 only
    bool recordTrajectory = true;
    // With no enveloped drive (H constant) and a stretch of at least this many steps, the evolution
    // uses the exact propagator e^{L Δt} from num::expm (dense superoperator; system dimension ≤
    // 16, larger systems keep the selected integrator, spec 06 §6).
    std::size_t constantPropagatorSteps = 100;
};

// One recorded sample of the evolution (spec 07 §5 outputs).
struct TimeSample {
    double timeS = 0.0;
    std::vector<double> populations; // per site, per level: site-major, [site][level] flattened
    double leakage = 0.0;            // total population outside the computational subspace
    double trace = 1.0;
};

class LindbladBackend final : public IBackend {
  public:
    static constexpr std::uint32_t kMaxSites = 5;
    static constexpr std::size_t kMaxDim = 243; // 3^5

    Capabilities capabilities() const override;
    Kind kind() const override { return Kind::Lindblad; }
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
    Result<Counts> sample(std::span<const QubitIndex> qubits, std::uint64_t shots,
                          core::Random& rng) const override;
    std::size_t bytesAllocated() const override { return rho_.data.size() * sizeof(Complex); }
    double stateNorm() const override; // trace
    std::unique_ptr<IBackend> clone() const override;

    // Install the time-domain model; allocates ρ = |0…0⟩⟨0…0| with the model's site dimensions.
    Status setModel(SystemModel model);
    const SystemModel& model() const { return model_; }
    void setSettings(const LindbladSettings& s) { settings_ = s; }
    const LindbladSettings& settings() const { return settings_; }

    // Integrate the master equation from the current time for `durationS`.
    Status evolve(double durationS);
    // Integrate the whole schedule (model.durationS).
    Status run();

    double timeS() const { return t_; }
    const Matrix& rho() const { return rho_; }
    Status setRho(const Matrix& rho);
    Status setPure(std::span<const Complex> psi);
    std::span<const TimeSample> trajectory() const { return samples_; }
    double population(std::uint32_t site, std::uint32_t level) const;
    double leakage() const;
    // ρ restricted to the computational (two-level) subspace, renormalized; logs P_leak.
    Result<Matrix> computationalSubspace() const;

  private:
    void derivative(double t, const Matrix& rho, Matrix& out);
    void vecDerivative(double t, std::span<const Complex> y, std::span<Complex> dy);
    void hamiltonianAt(double t, Matrix& h) const;
    bool hasActiveDrives() const;
    void recordSample();
    void enterSegment(double a, double b);
    Status evolveExact(double tEnd);
    Status evolveStepped(double tEnd);
    Status evolveAdaptive(double tEnd);
    void rk4Step(double t, double h);
    void magnus2Step(double t, double h);
    std::uint32_t n_ = 0, levels_ = 2;
    std::size_t D_ = 0;
    SystemModel model_;
    LindbladSettings settings_;
    Matrix rho_;
    Matrix hCache_, k1_, k2_, k3_, k4_, tmp_, work1_, work2_;
    std::vector<Matrix> lAdj_, lDagL_;
    Matrix lDagLSum_;                  // Σ_k L_k†L_k
    double segLo_ = 0.0, segHi_ = 0.0; // envelope sampling window of the current grid segment
    std::vector<TimeSample> samples_;
    double t_ = 0.0;
    bool allocated_ = false;
};

} // namespace qlab::qsim
