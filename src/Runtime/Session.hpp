#pragma once
// Spec 15 §1 — one session per open project: the selected device and backend, the loaded programs
// with their compile state, asynchronous cancellable compiles and runs on `core::JobSystem`, the
// results history, and the events the UI listens to on `core::EventBus`.
#include "Compiler/Compile.hpp"
#include "Core/JobSystem.hpp"
#include "Hardware/Hardware.hpp"
#include "Runtime/Run.hpp"
#include <atomic>
#include <future>
#include <map>
#include <mutex>
#include <string>

namespace qlab::runtime {

// ---------------------------------------------------------------- events (spec 02 §6, 15 §1)
struct DeviceSelected {
    std::string device, calibrationTimestamp;
};
struct BackendSelected {
    BackendChoice choice;
};
struct ProgramLoaded {
    ProgramId program;
    std::string origin;
};
struct CompileStarted {
    ProgramId program;
    CompileHandle handle;
};
struct CompileFinished {
    ProgramId program;
    CompileHandle handle;
    bool ok = false;
    std::uint32_t errors = 0, warnings = 0;
    std::chrono::milliseconds wallTime{0};
};
struct RunStarted {
    RunHandle handle;
    RunId id;
    std::string device, backendReason;
    qsim::Kind backend = qsim::Kind::StateVector;
    std::uint32_t shots = 0;
    std::uint64_t seed = 0;
};
struct RunProgress {
    RunHandle handle;
    RunId id;
    std::uint64_t done = 0, total = 0;
};
struct RunFinished {
    RunHandle handle;
    RunId id;
    bool ok = false, partial = false;
    std::uint64_t shots = 0;
    std::chrono::nanoseconds wallTime{0};
};

// One execution of one compiled program (spec 15 §3). `source` is needed for the program's
// `pragma qlab.*` overrides and to rebind + recompile each sweep point (§5).
struct RunRequest {
    const compiler::CompiledProgram* program = nullptr;
    const lang::Program* source = nullptr;
    compiler::CompileOptions compileOptions;
    RunOptions options;
    std::stop_token stop;
};

class Session {
  public:
    explicit Session(core::EventBus* bus = nullptr, core::JobSystem* jobs = nullptr);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // ---- device and backend (spec 15 §1: exactly one of each at a time)
    Status selectDevice(std::string_view deviceId); // one of `hw::shippedDeviceIds()`
    void setDevice(hw::LoadedDevice loaded);        // an already-loaded pair
    const hw::Device* device() const { return device_ ? &device_->device : nullptr; }
    const hw::Calibration* calibration() const { return device_ ? &device_->calibration : nullptr; }
    void selectBackend(BackendChoice choice);
    BackendChoice backendChoice() const { return backend_; }

    // ---- programs
    Result<ProgramId> loadProgram(std::string source, std::filesystem::path origin = {});
    const lang::Program* program(ProgramId id) const;
    std::string_view source(ProgramId id) const;

    // ---- compile (async, cancellable)
    Result<CompileHandle> compile(ProgramId id, compiler::CompileOptions options = {});
    bool compileDone(CompileHandle h) const;
    Status waitCompile(CompileHandle h);
    void cancelCompile(CompileHandle h);
    const compiler::CompiledProgram* compiled(CompileHandle h) const; // nullptr until it succeeded
    std::vector<lang::Diagnostic> compileDiagnostics(CompileHandle h) const;

    // ---- run (async, cancellable)
    Result<RunHandle> run(CompileHandle h, RunOptions options = {});
    bool done(RunHandle h) const;
    Status wait(RunHandle h);
    void cancel(RunHandle h);
    const RunResult* result(RunHandle h) const; // nullptr until complete
    std::vector<RunRecord> history() const;

    // Synchronous execution of an already compiled program: what `run` does on the worker, for the
    // report exporter, the conformance tests and headless tools.
    Result<RunResult> runSync(const RunRequest& request);

    // Spec 15 §5: the grid a program's `pragma qlab.sweep` declares, merged with `options.sweep`.
    Result<std::optional<SweepGrid>> sweepGrid(const lang::Program* program,
                                               const RunOptions& options) const;

  private:
    // Held by unique_ptr so a worker's pointer stays valid while the session loads more programs.
    struct ProgramEntry {
        std::string source;
        std::filesystem::path origin;
        lang::Program program;
        std::uint64_t hash = 0;
    };
    struct CompileState;
    struct RunState;
    ProgramEntry* entry(ProgramId id) const;
    CompileState* findCompile(CompileHandle h) const;
    RunState* findRun(RunHandle h) const;
    Result<noise::NoiseModel> noiseFor(const RunOptions& options) const;

    core::EventBus* bus_ = nullptr;
    core::JobSystem* jobs_ = nullptr;
    std::optional<hw::LoadedDevice> device_;
    BackendChoice backend_ = BackendChoice::Auto;

    mutable std::mutex mu_;
    std::vector<std::unique_ptr<ProgramEntry>> programs_;
    std::map<std::uint32_t, std::unique_ptr<CompileState>> compiles_;
    std::map<std::uint32_t, std::unique_ptr<RunState>> runs_;
    std::vector<RunRecord> history_;
    std::uint32_t nextCompile_ = 1, nextRun_ = 1;
    std::uint64_t nextRunId_ = 1;
};

// Spec 15 §3.2: every `input` the program declares must have a value at run time (QL5001).
Status checkInputs(const lang::Program& program, const ir::ParamMap& bound,
                   const std::optional<SweepGrid>& sweep);

} // namespace qlab::runtime
