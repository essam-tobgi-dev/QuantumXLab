#pragma once
// Spec 15 §3–§5 — run options, the execution of a compiled program on a backend, and `RunResult`.
#include "Compiler/Pass.hpp"
#include "Data/Fidelity.hpp"
#include "Core/EventBus.hpp"
#include "Cryo/HeatLoads.hpp"
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include "Noise/Noise.hpp"
#include "Pulse/Schedule.hpp"
#include "QSim/Lindblad.hpp"
#include "Runtime/Estimate.hpp"
#include "Runtime/Plan.hpp"
#include "Runtime/Types.hpp"
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace qlab::runtime {

// ---------------------------------------------------------------- sweeps (spec 15 §5)
struct SweepAxis {
    std::string input;                 // `input` variable name
    std::string unit;
    std::vector<double> values;
};
struct SweepGrid {
    std::vector<SweepAxis> axes;       // at most 4 (QL5030)
    std::size_t points() const;        // Cartesian product size
    std::vector<double> at(std::size_t index) const;      // coordinates of grid point `index`
    static constexpr std::size_t kMaxAxes = 4;
    static constexpr std::size_t kMaxPoints = 100000;
};
struct SweepPoint {
    std::vector<double> coords;
    data::Histogram counts;
    std::vector<Expectation> expectations;
    std::vector<double> outputs;       // mean of each `output` variable over the shots
    double p1 = 0.0;                   // P(first measured bit = 1), the calibration fits' y (22 §4)
    double p1Stderr = 0.0;
};
// A tensor over the grid, row-major in axis order (axis 0 slowest).
struct SweepResult {
    std::vector<SweepAxis> axes;
    std::vector<SweepPoint> points;
    std::vector<std::size_t> shape() const;
};

// ---------------------------------------------------------------- options (spec 15 §1)
struct RunOptions {
    std::uint32_t shots = 1024;
    std::optional<std::uint64_t> seed;          // unset: taken from the program/session, recorded
    NoiseSource noise = NoiseSource::Calibrated;
    std::filesystem::path noiseFile;            // NoiseSource::Custom (QL5050 when invalid)
    SnapshotCadence cadence = SnapshotCadence::Layer;
    std::optional<SweepGrid> sweep;
    bool computeEstimate = true;
    bool computeExpectations = true;
    bool accurateFidelity = false;              // §7 (ii): extra ideal + noisy evolution
    bool compareDevices = false;                // §9: recompile for the whole catalog (one compile each)
    bool keepMemory = true;                     // per-shot records (off for very large runs)
    std::uint32_t maxSnapshots = 64;
    std::optional<BackendChoice> backend;       // overrides the session's choice for this run
    ir::ParamMap inputs;                        // `input` bindings (QL5001 when one is missing)
    std::uint32_t levels = 2;                   // simulated levels per site (pulse-level: 3)
    // Ions, pulse-level: Fock levels of the shared motional mode. The Lindblad cap is on the total
    // dimension (D ≤ 243), so two ions admit a cutoff of 8: 2·2·8 = 32. Truncation at 5 leaves a
    // residual ~2e-3 in the mode after a Mølmer–Sørensen gate, so 8 is the default (spec 09 §5.4).
    std::uint32_t fockCutoff = 8;
    bool pulseDecoherence = true;               // pulse-level: collapse operators from the device
    double pulseStepS = 0.0;                    // Lindblad integrator step; 0 = from the model's fastest phase
    bool twirlNonPauli = false;                 // stabilizer + non-Pauli channel (spec 08 §7.3)
    std::uint32_t maxLoopIterations = 1000;     // guard when a `while` declares none (QL5020)
    std::vector<ProbeRequest> probes;           // spec 12 §8; the session fills these from the pragmas

    static constexpr std::uint32_t kMaxShots = 10'000'000;
};

// ---------------------------------------------------------------- result (spec 15 §4)
struct RunResult {
    RunId id{0};
    std::uint64_t programHash = 0;
    std::string device, calibrationTimestamp, backendReason;
    qsim::Kind backend = qsim::Kind::StateVector;
    data::FidelityClass backendClass = data::FidelityClass::Exact;
    RunOptions options;
    std::uint64_t seed = 0;
    std::chrono::nanoseconds wallTime{0};       // simulation wall time, not the estimate
    bool partial = false;

    ClassicalLayout layout;
    std::vector<ShotRecord> memory;
    data::Histogram counts;                     // bitstring → count over every classical bit
    std::optional<std::vector<double>> exact;   // Born probabilities over the classical bits
    data::FidelityClass exactClass = data::FidelityClass::Exact;
    std::vector<Marginal> marginals;
    std::vector<Expectation> expectations;
    std::vector<RunSnapshot> snapshots;
    std::optional<SweepResult> sweep;
    Estimate estimate;
    compiler::PassMetrics metrics;
    std::vector<lang::Diagnostic> diagnostics;

    // What the App hands to the instruments and the views (spec 12 §11, 21 §1).
    std::vector<std::uint32_t> qubits;          // simulator index → device qubit
    std::uint32_t levels = 2;
    std::vector<MeasuredBit> measurements;
    std::shared_ptr<const ir::Circuit> circuit, source;   // compiled (physical, timed) and post-Build
    compiler::Layout initialLayout, finalLayout;          // program qubit → physical, before / after routing
    std::shared_ptr<const pulse::Schedule> schedule;      // pulse-level runs
    std::vector<qsim::TimeSample> lindblad;               // populations over time (model (c))
    std::shared_ptr<const qsim::Snapshot> finalState;
    std::uint64_t shotsCompleted = 0;

    double probability(const std::string& key) const;     // from `counts`
    const Expectation* expectation(std::string_view name) const;
};

// One entry of the session history (spec 15 §1).
struct RunRecord {
    RunId id{0};
    RunHandle handle{0};
    ProgramId program{0};
    std::uint64_t programHash = 0;
    std::string device, backend;
    std::uint32_t shots = 0;
    std::uint64_t seed = 0;
    bool partial = false;
    std::chrono::nanoseconds wallTime{0};
};

// ---------------------------------------------------------------- execution
// Everything one execution needs. `noise` null = ideal run.
struct ExecutionInput {
    const compiler::CompileOutput* program = nullptr;
    const ProgramPlan* plan = nullptr;
    const hw::Device* device = nullptr;
    const hw::Calibration* calibration = nullptr;
    const noise::NoiseModel* noise = nullptr;
    qsim::Kind backend = qsim::Kind::StateVector;
    bool stochasticUnravelling = false;
    std::uint32_t shots = 1024;
    std::uint64_t seed = 0;
    const RunOptions* options = nullptr;
    std::stop_token stop;
    std::function<void(std::uint64_t done, std::uint64_t total)> progress;
    core::EventBus* events = nullptr;   // pulse-level runs post `LinePowerEvent` here at 10 Hz
    RunId id{0};
};

struct ExecutionOutput {
    std::vector<ShotRecord> memory;
    std::optional<std::vector<double>> exact;   // Born distribution over the classical bits
    data::FidelityClass exactClass = data::FidelityClass::Exact;
    std::vector<RunSnapshot> snapshots;
    std::vector<qsim::TimeSample> lindblad;
    std::shared_ptr<const pulse::Schedule> schedule;
    std::shared_ptr<const qsim::Snapshot> finalState;
    data::FidelityClass cls = data::FidelityClass::Exact;
    std::vector<std::string> twirled;
    std::vector<lang::Diagnostic> diagnostics;
    bool partial = false;
    std::uint64_t shotsCompleted = 0;
    double branchesPerShot = 0.0;
};

// Spec 15 §3.5: model (a) one evolution + Born sampling, (b) per-shot execution, (c) Lindblad.
Result<ExecutionOutput> execute(const ExecutionInput& in);
// Model (c) alone (pulse-level), used by `execute` and by the pulse tests.
Result<ExecutionOutput> executePulse(const ExecutionInput& in);

// ---------------------------------------------------------------- pulse level (spec 15 §3.5 (c))
// Levels per qubit site of a pulse-level run: 3 for transmons (the leakage level of spec 07 §5),
// 2 for ions. `options.levels` raises but never lowers it.
std::uint32_t pulseSiteLevels(const hw::Device& device, const RunOptions& options);

// Spec 11 §5: the average power the schedule puts on each fridge line over one repetition period,
//   P̄ = (1/T_rep) Σ_plays ∫ (A·V_fs·|e|)²/(2 Z₀) dt,   Z₀ = 50 Ω,
// keyed by the wiring line id when `device.directory/wiring.json` exists, else by channel name.
Result<cryo::LinePowers> schedulePower(const pulse::Schedule& schedule, const hw::Device& device, double repetitionS);

// Posted on `core::EventBus` at 10 Hz while a pulse-level run is active (spec 15 §3.5 (c)); `cryo`
// re-solves its dissipations from it.
struct LinePowerEvent {
    RunId run{0};
    std::string device;
    double wallTimeS = 0.0;     // since the run started
    double progress = 0.0;      // fraction of the schedule played, 0…1
    cryo::LinePowers powers;    // W at the room-temperature bulkhead
};

// ---------------------------------------------------------------- probes (spec 12 §8)
// Probe values at the current state of `backend`, which holds the plan's simulated qubits. Probes
// a backend cannot answer (a tableau has no reduced density matrix) are skipped, not faked.
std::vector<ProbeValue> evaluateProbes(const qsim::IBackend& backend, const ProgramPlan& plan,
                                       std::span<const ProbeRequest> probes);
// The same from an explicit density matrix over `nQubits` computational qubits (pulse-level runs).
std::vector<ProbeValue> evaluateProbes(const num::Matrix& rho, std::uint32_t nQubits, const ProgramPlan& plan,
                                       std::span<const ProbeRequest> probes);
// Spec 13 §7: `pragma qlab.probe` operands (`q[0]`, `$3`, a whole register) as device qubits.
std::vector<ProbeRequest> resolveProbes(const lang::Program& program, const compiler::CompiledProgram& compiled);

// Fills counts, marginals and expectations of `out` from its memory and exact distribution.
void summarize(RunResult& out, const ProgramPlan& plan, const hw::Device* device, bool expectations);
// ⟨Z_i⟩ per measured qubit, ⟨Z_iZ_j⟩ on the device edges among them, and the mean of every
// `output` variable (spec 15 §4). Exact values are added where the run holds the distribution.
std::vector<Expectation> computeExpectations(const RunResult& r, const ProgramPlan& plan, const hw::Device* device);

} // namespace qlab::runtime
