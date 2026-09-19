#pragma once
// Spec 12 §12 — simulator-only probes. They read the RunView / Environment and show what no
// physical instrument can (spec 00 §6): every channel and every trace is flagged Simulator-only,
// and the probes are absent from the Physical-lab workspace. No visualization reads a backend
// directly; the state views of spec 21 take their data from here.
//
//   probe_state          amplitudes, probabilities, reduced density matrix of selected qubits,
//                        Bloch vectors r = Tr(ρ σ), purities Tr ρ²          class of the snapshot
//   probe_entanglement   S(ρ_A) = −Tr ρ_A log₂ ρ_A, pair concurrence, mutual information graph
//   probe_fidelity       F = ⟨ψ_ideal|ρ|ψ_ideal⟩ per snapshot; process fidelity of a selected gate
//   probe_trajectory     jump record of the current Monte-Carlo trajectory     Statistical
//   probe_thermal_truth  true stage temperatures beside the thermometer readings Numerical
//   probe_leakage        population outside the computational subspace vs time   Numerical
#include "Instruments/InstrumentBase.hpp"
#include "Instruments/Thermometer.hpp"
#include <deque>

namespace qlab::instr {

class ProbeBase : public InstrumentBase {
  public:
    bool simulatorOnly() const final { return true; }

  protected:
    ProbeBase(InstrumentId id, SettingSchema schema)
        : InstrumentBase(std::move(id), std::move(schema)) {}
    // Channels of a probe: all Simulator-only.
    void setProbeChannels(std::vector<ChannelDesc> channels);
    bool triggerSourceSet(const SettingValues&) const override { return false; }
    // The run snapshot, or NotBound.
    static Result<std::shared_ptr<const qsim::Snapshot>> stateOf(const AcquireContext& ctx,
                                                                 const InstrumentId& id);
};

// Parses "0,2,5" into qubit indices; empty text → every qubit below `nQubits`.
Result<std::vector<std::uint32_t>> parseQubitList(std::string_view text, std::uint32_t nQubits);

class StateProbe final : public ProbeBase {
  public:
    explicit StateProbe(std::uint32_t index = 0);
    static SettingSchema makeSchema();
    std::optional<double>
    query(std::string_view path) const override; // qubit[i].bloch[k], qubit[i].purity, purity

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
};

class EntanglementProbe final : public ProbeBase {
  public:
    static constexpr std::uint32_t kMaxPairQubits =
        20; // spec 21 §3.8: all-pairs reductions only up to here
    explicit EntanglementProbe(std::uint32_t index = 0);
    static SettingSchema makeSchema();

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
};

class FidelityProbe final : public ProbeBase {
  public:
    explicit FidelityProbe(std::uint32_t index = 0);
    static SettingSchema makeSchema();
    // F of a state against the ideal reference; both snapshots must describe the same register.
    static Result<double> stateFidelity(const qsim::Snapshot& state, const qsim::Snapshot& ideal);
    // Process (entanglement) fidelity Σ_k |Tr(V† K_k)|² / d² of a Kraus set against a unitary V
    // (T10 §1.2); the average gate fidelity is (d F_pro + 1)/(d + 1) (T10 §1.3).
    static Result<double> processFidelity(const GateComparison& gate);

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;

  private:
    std::deque<std::pair<std::uint64_t, double>>
        history_; // (gate index, F); touched only while Acquiring
};

class TrajectoryProbe final : public ProbeBase {
  public:
    explicit TrajectoryProbe(std::uint32_t index = 0);
    static SettingSchema makeSchema();

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
};

class ThermalTruthProbe final : public ProbeBase {
  public:
    explicit ThermalTruthProbe(std::uint32_t index = 0);
    static SettingSchema makeSchema();
    // Thermometers whose latest readings are listed beside the truth (the registry attaches all of
    // them).
    void attach(std::vector<const Thermometer*> thermometers) {
        thermometers_ = std::move(thermometers);
    }
    std::optional<double> query(std::string_view path) const override; // stage.<NAME>.T

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;

  private:
    std::vector<const Thermometer*> thermometers_;
};

class LeakageProbe final : public ProbeBase {
  public:
    explicit LeakageProbe(std::uint32_t index = 0);
    static SettingSchema makeSchema();

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
};

} // namespace qlab::instr
