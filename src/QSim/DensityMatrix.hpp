#pragma once
// Spec 07 §3 — density-matrix backend with mixed-radix sites (d ∈ {2,3}).
#include "QSim/Backend.hpp"

namespace qlab::qsim {

class DensityMatrixBackend final : public IBackend {
  public:
    Capabilities capabilities() const override;
    Kind kind() const override { return Kind::DensityMatrix; }
    std::uint32_t nQubits() const override { return n_; }
    std::uint32_t levels() const override { return levels_; }

    Status allocate(std::uint32_t nQubits, std::uint32_t levelsPerSite = 2) override;
    Status allocateMixed(std::span<const std::uint32_t> dims); // per-site dimensions
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
    std::size_t bytesAllocated() const override { return rho_.size() * sizeof(Complex) * 2; }
    double stateNorm() const override; // trace
    std::unique_ptr<IBackend> clone() const override;

    const Matrix& rho() const { return rho_; }
    Status setRho(const Matrix& rho);
    Status setPure(std::span<const Complex> psi);
    std::size_t dim() const { return D_; }
    std::span<const std::uint32_t> dims() const { return dims_; }
    double purity() const;
    Result<Matrix> reducedDensityMatrix(std::span<const QubitIndex> keep) const;
    // Population of level `level` on site q (diagonal marginal).
    double population(QubitIndex q, std::uint32_t level) const;

    static std::uint32_t maxQubits();

    // Apply a full-dimension operator O (dim m over the given sites, little-endian) as ρ ← O ρ
    // (left) or ρ ← ρ O† (right). Public for the Lindblad backend.
    static void applyLeft(Matrix& rho, std::span<const std::uint32_t> dims,
                          std::span<const std::uint32_t> sites, const Matrix& op,
                          std::span<const std::size_t> ctrlSites);
    static void applyRightAdjoint(Matrix& rho, std::span<const std::uint32_t> dims,
                                  std::span<const std::uint32_t> sites, const Matrix& op,
                                  std::span<const std::size_t> ctrlSites);
    // Expand a qubit-subspace operator (2^k) to the sites' full dimensions (identity on levels ≥
    // 2).
    static Matrix expandToSites(const Matrix& u, std::span<const std::uint32_t> siteDims);

  private:
    Status applyOp(const Matrix& u, std::span<const QubitIndex> targets,
                   std::span<const QubitIndex> controls);
    void checkInvariants();
    std::uint32_t n_ = 0, levels_ = 2;
    std::vector<std::uint32_t> dims_;
    std::vector<std::size_t> strides_;
    std::size_t D_ = 0;
    Matrix rho_;
    Matrix scratch_;
    bool allocated_ = false;
};

} // namespace qlab::qsim
