#pragma once
// Spec 12 §11 — what the instruments read. Layering decision (SPEC_DEVIATIONS.md): Instruments does
// not depend on Runtime, so the `RunSnapshot`/`ThermalSnapshot` subscriptions of the §11 sketch are
// replaced by two narrow value types the App layer fills each frame from the runtime and the cryo
// model — `RunView` (the live run) and `Environment` (device, calibration, fridge state) —
// published through an `InputHub` as immutable snapshots that worker-thread acquisitions can hold.
#include "Cryo/Cryo.hpp"
#include "Hardware/Hardware.hpp"
#include "Noise/Noise.hpp"
#include "Pulse/Pulse.hpp"
#include "QSim/QSim.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace qlab::instr {

// One collapse of the current Monte-Carlo trajectory (spec 12 §12 `probe_trajectory`).
struct JumpRecord {
    double timeS = 0.0;
    std::uint32_t op = 0; // index into the model's collapse operators
    std::string name;     // "T1 q0"
};

// Level populations at one instant of a time-domain run (Lindblad `TimeSample`, Trajectories
// `AveragedSample`): site-major, populations[site * levels + level].
struct LevelSample {
    double timeS = 0.0;
    std::vector<double> populations;
};

// A gate whose realised process `probe_fidelity` compares with its ideal matrix.
struct GateComparison {
    std::string name;
    num::Matrix ideal;              // d × d unitary
    std::vector<num::Matrix> kraus; // the realised process; a single unitary is one element
};

// The live run as the probes, the digitizer, the AWG and the controller see it.
struct RunView {
    qsim::Kind backend = qsim::Kind::StateVector;
    std::uint32_t nQubits = 0;
    std::uint32_t levels = 2;
    bool running = false;
    std::uint64_t seed = 0;
    std::uint64_t shot = 0, shots = 0;               // current shot index / requested shots
    std::uint64_t gateCursor = 0;                    // index of the instruction being executed
    double wallTimeS = 0.0;                          // hardware wall-time estimate so far (spec 15)
    double playheadS = 0.0;                          // position inside `schedule`
    std::shared_ptr<const pulse::Schedule> schedule; // the compiled pulse schedule of one shot
    std::shared_ptr<const qsim::Snapshot> state;     // amplitudes / density matrix / reduced states
    std::shared_ptr<const qsim::Snapshot> ideal; // noiseless reference run at the same gate index
    std::vector<std::int8_t> measuredBits; // per qubit: outcome of the current shot, −1 = none
    std::vector<LevelSample> populations;  // time-domain record for `probe_leakage`
    std::vector<JumpRecord> jumps;         // `probe_trajectory`
    std::optional<GateComparison> gate;    // `probe_fidelity` process channel
};

// cryo::NoiseBudget only references its coax catalogue; this owns both so an Environment can
// share them across threads. Not copyable or movable (the budget points into `coax`).
struct CryoCatalogs {
    cryo::CoaxCatalog coax;
    cryo::NoiseBudget budget{coax};
    CryoCatalogs() = default;
    CryoCatalogs(const CryoCatalogs&) = delete;
    CryoCatalogs& operator=(const CryoCatalogs&) = delete;
};

// Output chain of one readout line evaluated at a resonator frequency (spec 11 §6, T07 §8).
struct OutputChain {
    std::string lineId;
    double tSysK = 0.0;      // T_q + Friis sum: system noise referred to the chip, vacuum included
    double tQuantumK = 0.0;  // T_q = hf / 2k_B
    double efficiency = 0.0; // η = T_q / T_sys  (T07 (8.2): ½ for an ideal phase-preserving chain)
    double gainDb = 0.0;     // chip → digitizer
    bool hasPreamp = false;
};

struct Environment {
    std::shared_ptr<const hw::Device> device;
    std::shared_ptr<const hw::Calibration> calibration;
    std::shared_ptr<const noise::NoiseModel> noiseModel; // optional
    std::shared_ptr<const cryo::Wiring> wiring;   // optional: without it the standard chains apply
    std::shared_ptr<const CryoCatalogs> catalogs; // optional: a shared default is used without it
    cryo::ThermalSnapshot thermal;                // latest stage temperatures (truth)
    cryo::GhsSnapshot ghs;                        // pressures and ³He flow
    double labTimeS = 0.0;                        // lab clock, stamps every trace
    std::uint64_t seed = 0x5EEDC0FFEE17ull;       // instrument noise seed when no run is active
    double feedlineCableLengthM = 13.6;           // rack → fridge → rack (layout routing.json)
    double cableVelocityFactor = 0.7;             // PTFE coax

    // Stage temperatures of `thermal`, with the nominal value wherever the snapshot holds none.
    cryo::StageArray stageTemperatures() const;
    // First wiring line of `kind` whose channel list covers `qubit` ("a[0..4]", "d[3]").
    const cryo::WiringLine* lineFor(cryo::LineKind kind, std::uint32_t qubit) const;
    // Output chain seen by `qubit`'s resonator at `frequencyHz`; the standard `readout_out_std`
    // chain without a preamp when no wiring is bound.
    OutputChain outputChain(std::uint32_t qubit, double frequencyHz) const;
    // Total attenuation (dB, cable loss included) of the readout input line feeding `qubit`.
    double inputAttenuationDb(std::uint32_t qubit, double frequencyHz) const;
    // Electrical delay of the feedline measurement path, τ = L / (v_f c)  (spec 12 §6).
    double electricalDelayS() const;
};

// The reference condition of spec 12 §15: `readout_out_std`, no preamp, nominal temperatures.
OutputChain referenceOutputChain(double frequencyHz);
// True when a wiring channel list such as "a[0..4]", "m[0..6]" or "d[3]" covers `index`.
bool channelListCovers(std::string_view channel, std::uint32_t index);

// Latest immutable inputs. The App publishes; instruments take a snapshot per acquisition.
class InputHub {
  public:
    void publish(RunView v);
    void publish(Environment e);
    std::shared_ptr<const RunView> run() const;
    std::shared_ptr<const Environment> environment() const;

  private:
    mutable std::mutex mu_;
    std::shared_ptr<const RunView> run_;
    std::shared_ptr<const Environment> env_;
};

} // namespace qlab::instr
