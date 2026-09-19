// Spec 15 §1–§3 — compile + run of one program with the pragmas resolved (see RunProgram.hpp).
#include "App/RunProgram.hpp"
#include "Core/Json.hpp"
#include "Core/Timer.hpp"
#include <format>

namespace qlab::app {
namespace {

runtime::BackendChoice fromPragma(lang::BackendChoice c) {
    switch (c) {
    case lang::BackendChoice::StateVector:
        return runtime::BackendChoice::StateVector;
    case lang::BackendChoice::DensityMatrix:
        return runtime::BackendChoice::DensityMatrix;
    case lang::BackendChoice::Stabilizer:
        return runtime::BackendChoice::Stabilizer;
    case lang::BackendChoice::Lindblad:
        return runtime::BackendChoice::Lindblad;
    case lang::BackendChoice::Auto:
        break;
    }
    return runtime::BackendChoice::Auto;
}

runtime::NoiseSource fromPragma(lang::NoiseChoice c) {
    switch (c) {
    case lang::NoiseChoice::Ideal:
        return runtime::NoiseSource::Ideal;
    case lang::NoiseChoice::Custom:
        return runtime::NoiseSource::Custom;
    case lang::NoiseChoice::Calibrated:
        break;
    }
    return runtime::NoiseSource::Calibrated;
}

runtime::SnapshotCadence cadenceFromName(std::string_view name) {
    if (name == "none")
        return runtime::SnapshotCadence::None;
    if (name == "layer")
        return runtime::SnapshotCadence::Layer;
    if (name == "barrier")
        return runtime::SnapshotCadence::Barrier;
    if (name == "end")
        return runtime::SnapshotCadence::End;
    return runtime::SnapshotCadence::Gate;
}

const lang::Diagnostic* firstError(const std::vector<lang::Diagnostic>& diagnostics) {
    for (const lang::Diagnostic& d : diagnostics)
        if (d.severity == lang::Severity::Error)
            return &d;
    return nullptr;
}

} // namespace

runtime::RunOptions runOptionsFor(const lang::Program& program, const Options& o) {
    runtime::RunOptions options;
    const lang::Pragmas& p = program.pragmas;
    options.shots =
        o.shotsGiven ? o.shots : (p.shots ? static_cast<std::uint32_t>(*p.shots) : o.shots);
    options.seed = o.seedGiven ? std::optional(o.seed) : (p.seed ? p.seed : std::optional(o.seed));
    options.noise = o.ideal ? runtime::NoiseSource::Ideal : fromPragma(p.noise);
    options.noiseFile = p.noiseFile;
    options.cadence = cadenceFromName(p.snapshotCadence);
    const runtime::BackendChoice pragmaBackend = fromPragma(p.backend);
    if (o.backendGiven && o.backend != runtime::BackendChoice::Auto)
        options.backend = o.backend;
    else if (pragmaBackend != runtime::BackendChoice::Auto)
        options.backend = pragmaBackend;
    options.computeEstimate = true;
    options.computeExpectations = true;
    return options;
}

compiler::CompileOptions compileOptionsFor(const Options& o) {
    compiler::CompileOptions options;
    // Only a pinned level overrides `pragma qlab.optimize`; `resolveContext` applies the pragma.
    if (o.optimizeGiven)
        options.level = o.optimize == 0   ? compiler::OptimizeLevel::O0
                        : o.optimize == 2 ? compiler::OptimizeLevel::O2
                                          : compiler::OptimizeLevel::O1;
    return options;
}

Result<ProgramRun> compileAndRun(runtime::Session& session, std::string source,
                                 std::filesystem::path origin, const Options& o) {
    if (session.device() == nullptr)
        return fail(ErrorCode::NotFound, "no device is selected");
    QXL_TRY_ASSIGN(const runtime::ProgramId id,
                   session.loadProgram(std::move(source), std::move(origin)));
    const lang::Program* program = session.program(id);
    if (program == nullptr)
        return fail(ErrorCode::Internal, "the session lost the program it just loaded");

    ProgramRun out;
    out.program = id;
    core::Timer timer;
    QXL_TRY_ASSIGN(const runtime::CompileHandle handle, session.compile(id, compileOptionsFor(o)));
    QXL_TRY(session.waitCompile(handle));
    out.compile = handle;
    out.compileTime = std::chrono::milliseconds(static_cast<std::int64_t>(timer.ms()));
    out.diagnostics = session.compileDiagnostics(handle);
    if (session.compiled(handle) == nullptr) {
        const lang::Diagnostic* e = firstError(out.diagnostics);
        return fail(e != nullptr ? runtime::error(*e)
                                 : Error(ErrorCode::Internal, "the program did not compile"));
    }

    timer.reset();
    QXL_TRY_ASSIGN(const runtime::RunHandle run, session.run(handle, runOptionsFor(*program, o)));
    QXL_TRY(session.wait(run));
    out.runTime = std::chrono::milliseconds(static_cast<std::int64_t>(timer.ms()));
    const runtime::RunResult* result = session.result(run);
    if (result == nullptr)
        return fail(ErrorCode::Internal, "the run produced no result");
    out.result = *result;
    for (const lang::Diagnostic& d : out.result.diagnostics)
        out.diagnostics.push_back(d);
    return out;
}

Result<ProgramRun> compileAndRunFile(runtime::Session& session, const std::filesystem::path& file,
                                     const Options& o) {
    QXL_TRY_ASSIGN(std::string text, core::readTextFile(file));
    return compileAndRun(session, std::move(text), file, o);
}

} // namespace qlab::app
