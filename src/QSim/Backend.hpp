#pragma once
// Spec 07 §1 — IBackend interface, memory guard, factory, selection.
#include "Core/Random.hpp"
#include "QSim/Types.hpp"
#include <memory>

namespace qlab::qsim {

class IBackend {
  public:
    virtual ~IBackend() = default;
    virtual Capabilities capabilities() const = 0;
    virtual Kind kind() const = 0;
    virtual std::uint32_t nQubits() const = 0;
    virtual std::uint32_t levels() const { return 2; }

    virtual Status allocate(std::uint32_t nQubits, std::uint32_t levelsPerSite = 2) = 0;
    virtual Status reset(std::span<const QubitIndex> qubits, core::Random& rng) = 0;
    // `targets` little-endian: targets[0] is the least-significant index of `u`.
    virtual Status applyGate(const Matrix& u, std::span<const QubitIndex> targets) = 0;
    virtual Status applyControlled(const Matrix& u, std::span<const QubitIndex> controls,
                                   std::span<const QubitIndex> targets) = 0;
    // Convenience: dispatches on GateOp::cls fast paths where the backend has them.
    virtual Status apply(const GateOp& op);
    virtual Status applyChannel(const Kraus& kraus, std::span<const QubitIndex> targets) = 0;
    virtual Result<Outcome> measure(std::span<const QubitIndex> qubits, core::Random& rng) = 0;
    virtual Result<double> expectation(const PauliString& p) const = 0;
    virtual Result<Probabilities> probabilities(std::span<const QubitIndex> qubits) const = 0;
    virtual Result<Snapshot> snapshot(const SnapshotRequest& req) const = 0;
    virtual Result<Counts> sample(std::span<const QubitIndex> qubits, std::uint64_t shots,
                                  core::Random& rng) const = 0;
    virtual std::size_t bytesAllocated() const = 0;
    virtual double stateNorm() const = 0;
    virtual std::unique_ptr<IBackend> clone() const = 0;

    std::uint64_t opCount() const { return ops_; }

  protected:
    std::uint64_t ops_ = 0;
};

// Physical memory available now (bytes), minus the 512 MiB reserve of spec 07 §2.4.
std::size_t availableMemoryBytes();
// max n such that bytesPerAmplitude · 2^n ≤ 0.8 · available.
std::uint32_t maxQubitsFor(std::size_t bytesPerEntry, bool squared);

std::unique_ptr<IBackend> makeBackend(Kind kind);

struct SelectionRequest {
    std::uint32_t nQubits = 0;
    bool cliffordOnly = false;
    bool hasNoise = false;
    bool pauliNoiseOnly = false;
    bool pulseLevel = false;
    std::uint32_t levels = 2;
    std::optional<Kind> pinned;
};
struct Selection {
    Kind kind;
    FidelityClass cls;
    bool stochasticUnravelling = false;
    std::string reason;
};
Result<Selection> selectBackend(const SelectionRequest& req);

} // namespace qlab::qsim
