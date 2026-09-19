#pragma once
// Internal: one execution of one compiled program, and the sweep loop that repeats it over a grid.
// Split from `Session` so the report exporter and the tests can drive a run without a session.
#include "Compiler/Compile.hpp"
#include "Runtime/Run.hpp"

namespace qlab::runtime::detail {

struct RunContext {
    const compiler::CompiledProgram* program = nullptr;
    const lang::Program* source = nullptr; // for the program's pragmas and for sweeps
    const hw::Device* device = nullptr;
    const hw::Calibration* calibration = nullptr;
    const noise::NoiseModel* noise = nullptr; // null = ideal run
    BackendChoice choice = BackendChoice::Auto;
    compiler::CompileOptions compileOptions; // used to recompile each sweep point (spec 15 §5)
    RunOptions options;
    std::stop_token stop;
    RunId id{0};
    RunHandle handle{0};
    core::EventBus* bus = nullptr;
};

// Spec 15 §3: plan, select the backend, execute, summarise, estimate.
Result<RunResult> performRun(const RunContext& ctx);
// Spec 15 §5: bind, recompile and run each grid point; the seed is offset by the point index.
Result<SweepResult> performSweep(const RunContext& ctx, const SweepGrid& grid);
// Spec 15 §7 (ii): the noisy-versus-ideal comparison of the output distributions and of the state.
Result<SimulatedFidelity> simulatedFidelity(const RunContext& ctx, const RunResult& noisy);

} // namespace qlab::runtime::detail
