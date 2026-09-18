#pragma once
// Spec 07 §2 — state-vector backend.
#include "Core/Aligned.hpp"
#include "QSim/Backend.hpp"
#include "Numerics/Types.hpp"
#include <functional>

namespace qlab::qsim {

class StateVectorBackend final : public IBackend {
public:
    Capabilities capabilities() const override;
    Kind kind() const override { return Kind::StateVector; }
    std::uint32_t nQubits() const override { return n_; }

    Status allocate(std::uint32_t nQubits, std::uint32_t levelsPerSite = 2) override;
    Status reset(std::span<const QubitIndex> qubits, core::Random& rng) override;
    Status applyGate(const Matrix& u, std::span<const QubitIndex> targets) override;
    Status applyControlled(const Matrix& u, std::span<const QubitIndex> controls,
                           std::span<const QubitIndex> targets) override;
    Status apply(const GateOp& op) override;
    Status applyChannel(const Kraus& kraus, std::span<const QubitIndex> targets) override;
    Result<Outcome> measure(std::span<const QubitIndex> qubits, core::Random& rng) override;
    Result<double> expectation(const PauliString& p) const override;
    Result<Probabilities> probabilities(std::span<const QubitIndex> qubits) const override;
    Result<Snapshot> snapshot(const SnapshotRequest& req) const override;
    Result<Counts> sample(std::span<const QubitIndex> qubits, std::uint64_t shots, core::Random& rng) const override;
    std::size_t bytesAllocated() const override { return psi_.size() * sizeof(Complex); }
    double stateNorm() const override;
    std::unique_ptr<IBackend> clone() const override;

    // Direct access for probes and tests (Simulator-only).
    std::span<const Complex> amplitudes() const { return psi_; }
    std::span<Complex> amplitudesMut() { return psi_; }
    Status setAmplitudes(std::span<const Complex> psi);
    Result<Matrix> reducedDensityMatrix(std::span<const QubitIndex> keep) const;
    // Stochastic Kraus unravelling for one shot (spec 08 §7): pick K_k with prob ‖K_k ψ‖², renormalize.
    Status applyChannelStochastic(const Kraus& kraus, std::span<const QubitIndex> targets, core::Random& rng);

    static std::uint32_t maxQubits();

    // Kernels (public for the density-matrix backend, which reuses them column/row-wise).
    static void kernel1(std::span<Complex> psi, std::uint32_t n, std::uint32_t t, const num::Mat2& u, std::uint64_t ctrlMask);
    static void kernel2(std::span<Complex> psi, std::uint32_t n, std::uint32_t t0, std::uint32_t t1, const num::Mat4& u, std::uint64_t ctrlMask);
    static void kernelK(std::span<Complex> psi, std::uint32_t n, std::span<const std::uint32_t> targets, const Matrix& u, std::uint64_t ctrlMask);
    static void kernelDiag1(std::span<Complex> psi, std::uint32_t t, Complex d0, Complex d1, std::uint64_t ctrlMask);
    static void kernelX(std::span<Complex> psi, std::uint32_t t, std::uint64_t ctrlMask);
    static void kernelZ(std::span<Complex> psi, std::uint32_t t, std::uint64_t ctrlMask);
    static void kernelSwap(std::span<Complex> psi, std::uint32_t t0, std::uint32_t t1, std::uint64_t ctrlMask);
    static void kernelCnot(std::span<Complex> psi, std::uint32_t c, std::uint32_t t, std::uint64_t ctrlMask);
    static void kernelCz(std::span<Complex> psi, std::uint32_t a, std::uint32_t b, std::uint64_t ctrlMask);
    static void kernelDiag2(std::span<Complex> psi, std::uint32_t t0, std::uint32_t t1, const num::Mat4& u, std::uint64_t ctrlMask);

private:
    Status applyImpl(const Matrix& u, std::span<const QubitIndex> targets, std::uint64_t ctrlMask, GateClass cls);
    double probOne(std::uint32_t q) const;
    void collapse(std::uint32_t q, bool one, double p);
    std::uint32_t n_ = 0;
    num::Vector psi_;
    bool allocated_ = false;
};

// Range helper: parallelize over [0, count) with the JobSystem when large enough.
void forRange(std::size_t count, const std::function<void(std::size_t, std::size_t)>& fn);

} // namespace qlab::qsim
