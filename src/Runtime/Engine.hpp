#pragma once
// Internal: the gate-level execution engine shared by the terminal-measurement and per-shot models
// (spec 15 §3.5 (a) and (b)). It holds one backend, binds the noise model's channels to the
// SIMULATOR indices (the backend holds only the qubits the circuit uses) and publishes snapshots.
#include "Runtime/Run.hpp"
#include <map>
#include <memory>
#include <vector>

namespace qlab::runtime::detail {

// Classical state of one shot (spec 15 §3.5 (b)).
struct ShotContext {
    std::vector<std::uint8_t> bits;    // flat classical memory
    std::vector<std::int8_t> measured; // per simulated qubit, −1 = not measured yet
    core::Random* rng = nullptr;
    bool truncated = false;     // QL5020
    std::uint32_t branches = 0; // executed Branch / Loop iterations (T12 (1.1) T_ff)
    std::uint32_t shot = 0;
};

class Engine {
  public:
    Engine(const ExecutionInput& in, const ProgramPlan& plan);

    Status allocate(); // backend + memory budget (QL5012)
    // |0…0⟩ with the state-preparation channels of the calibration (T04 §10.3).
    Status prepare(ShotContext& ctx);
    // Walks the compiled circuit for one shot; `topLevel` applies idle intervals and snapshots.
    Status runCircuit(const ir::Circuit& c, ShotContext& ctx, bool topLevel);
    // Terminal Born sampling of `shots` shots (model (a)); fills `memory` and `exact`.
    Status sampleTerminal(std::uint32_t shots, core::Random& rng, std::vector<ShotRecord>& memory,
                          std::optional<std::vector<double>>& exact);

    qsim::IBackend& backend() { return *backend_; }
    const noise::ApplyReport& report() const { return report_; }
    std::vector<RunSnapshot>& snapshots() { return snapshots_; }
    std::shared_ptr<const qsim::Snapshot> capture(double timeS = 0.0, std::uint64_t gateIndex = 0);
    std::size_t stateBytes() const;
    void setCollectSnapshots(bool on) { collect_ = on; }
    // Model (a): the terminal Measure nodes only carry their readout dephasing; the outcomes come
    // from `sampleTerminal`. Model (b) projects (spec 15 §3.5).
    void setProjectMeasurements(bool on) { project_ = on; }

    // Classical bit each measured qubit ends up in, −1 when its outcome is discarded.
    const std::vector<std::int64_t>& classicalBitOfMeasured() const { return cbitOfMeasured_; }

  private:
    Status applyGate(const ir::Gate& g, ShotContext& ctx);
    Status applyMeasure(const ir::Measure& m, ShotContext& ctx);
    Status applyReset(const ir::Reset& r, ShotContext& ctx);
    Status applyIdle(std::size_t nodeIndex, ShotContext& ctx);
    Status applyChannels(std::span<const noise::AttachedChannel> channels, ShotContext& ctx);
    std::vector<noise::AttachedChannel> remap(std::vector<noise::AttachedChannel> v) const;
    Result<const noise::ReadoutModel*> readoutFor(std::uint32_t simQubit);
    void maybeSnapshot(std::size_t nodeIndex, const ir::Node& n, ShotContext& ctx);

    const ExecutionInput& in_;
    const ProgramPlan& plan_;
    const ir::Circuit& circuit_;
    const RunOptions& options_;
    std::unique_ptr<qsim::IBackend> backend_;
    noise::ApplyOptions apply_;
    noise::ApplyReport report_;
    std::vector<double> detuning_; // per simulated qubit, this shot
    std::vector<IdleGap> idle_;
    std::size_t idleCursor_ = 0;
    std::vector<std::uint32_t> layerOf_;       // per node index: ASAP layer
    std::vector<std::int64_t> cbitOfMeasured_; // parallel to plan.measuredQubits
    std::map<std::uint32_t, noise::ReadoutModel> readout1q_;
    std::vector<RunSnapshot> snapshots_;
    SnapshotCadence cadence_ = SnapshotCadence::None;
    std::uint32_t lastLayer_ = 0xFFFFFFFFu;
    std::uint64_t gateIndex_ = 0;
    bool collect_ = true;
    bool project_ = true;
    bool perShotDrift_ = false;

  public:
    // Terminal readout over `plan.measuredQubits` in simulator indices (ideal when noise is off).
    Result<noise::ReadoutModel> terminalReadout() const;
};

// Bitstring of one shot's measured qubits (MSB-first over `qubits`) written into classical memory.
void writeMeasuredBits(const std::string& key, std::span<const std::int64_t> cbits,
                       std::span<std::uint8_t> bits);
// Classical bit each measured qubit ends up in (the LAST measurement of a qubit wins), −1 when the
// outcome is discarded. Parallel to `plan.measuredQubits`.
std::vector<std::int64_t> classicalBitOfMeasured(const ProgramPlan& plan);
// Assignment map of `plan.measuredQubits` in SIMULATOR indices; ideal when the run has no noise.
Result<noise::ReadoutModel> terminalReadoutFor(const ExecutionInput& in, const ProgramPlan& plan);

} // namespace qlab::runtime::detail
