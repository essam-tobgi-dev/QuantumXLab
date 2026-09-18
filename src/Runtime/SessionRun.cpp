// Spec 15 §1, §3, §5 — the session's run path: input binding, the noise source, the asynchronous
// cancellable job, the history, and the sweep grid a program declares.
#include "Runtime/Perform.hpp"
#include "Runtime/SessionState.hpp"
#include <format>

namespace qlab::runtime {
namespace {

// Spec 15 §1: a `pragma qlab.*` value applies when the run dialog left the option at its default.
RunOptions resolveOptions(RunOptions options, const lang::Program* program) {
    if (!program) return options;
    const lang::Pragmas& p = program->pragmas;
    static const RunOptions kDefault;
    if (p.shots && options.shots == kDefault.shots) options.shots = static_cast<std::uint32_t>(*p.shots);
    if (!options.seed && p.seed) options.seed = *p.seed;
    if (options.noise == kDefault.noise && p.noise != lang::NoiseChoice::Calibrated)
        options.noise = p.noise == lang::NoiseChoice::Ideal ? NoiseSource::Ideal : NoiseSource::Custom;
    if (options.noiseFile.empty() && !p.noiseFile.empty()) options.noiseFile = p.noiseFile;
    if (!options.backend && p.backend != lang::BackendChoice::Auto)
        options.backend = static_cast<BackendChoice>(p.backend);
    if (options.cadence == kDefault.cadence) {
        if (p.snapshotCadence == "none") options.cadence = SnapshotCadence::None;
        else if (p.snapshotCadence == "gate") options.cadence = SnapshotCadence::Gate;
        else if (p.snapshotCadence == "barrier") options.cadence = SnapshotCadence::Barrier;
        else if (p.snapshotCadence == "end") options.cadence = SnapshotCadence::End;
    }
    return options;
}
} // namespace

Status checkInputs(const lang::Program& program, const ir::ParamMap& bound, const std::optional<SweepGrid>& sweep) {
    for (const lang::InputVar& v : program.inputs) {
        if (v.defaultValue || bound.contains(v.name)) continue;
        bool swept = false;
        if (sweep)
            for (const SweepAxis& a : sweep->axes) swept = swept || a.input == v.name;
        if (swept) continue;
        auto d = diagnostic("QL5001", v.span, v.name);
        return std::unexpected(error(d));
    }
    return {};
}

Result<std::optional<SweepGrid>> Session::sweepGrid(const lang::Program* program, const RunOptions& options) const {
    SweepGrid grid;
    if (options.sweep) grid = *options.sweep;
    else if (program)
        for (const lang::Sweep& s : program->pragmas.sweeps)
            grid.axes.push_back(SweepAxis{s.input, {}, s.grid()});
    if (grid.axes.empty()) return std::optional<SweepGrid>{};
    if (grid.axes.size() > SweepGrid::kMaxAxes || grid.points() > SweepGrid::kMaxPoints) {
        auto d = diagnostic("QL5030", SourceSpan{}, grid.points(), SweepGrid::kMaxPoints);
        return std::unexpected(error(d));
    }
    return std::optional<SweepGrid>{std::move(grid)};
}

Result<noise::NoiseModel> Session::noiseFor(const RunOptions& options) const {
    if (!device_) return fail(err::NoDevice, "the noise model needs a device");
    if (options.noise == NoiseSource::Custom) {
        auto text = core::readTextFile(options.noiseFile);
        if (!text) {
            auto d = diagnostic("QL5050", SourceSpan{}, options.noiseFile.string(), text.error().message);
            return std::unexpected(error(d));
        }
        auto model = noise::NoiseModel::parse(*text);
        if (!model) {
            auto d = diagnostic("QL5050", SourceSpan{}, options.noiseFile.string(), model.error().message);
            return std::unexpected(error(d));
        }
        return model;
    }
    noise::NoiseOptions opts;
    opts.levels = options.levels;
    return noise::NoiseModel::fromCalibration(device_->device, device_->calibration, opts);
}

Result<RunResult> Session::runSync(const RunRequest& request) {
    if (!request.program) return fail(err::NotCompiled, "the run needs a compiled program");
    if (!device_) return fail(err::NoDevice, "select a device before running");
    RunOptions options = resolveOptions(request.options, request.source);
    if (options.probes.empty() && request.source) options.probes = resolveProbes(*request.source, *request.program);
    QXL_TRY_ASSIGN(const std::optional<SweepGrid> grid, sweepGrid(request.source, options));
    if (request.source) QXL_TRY(checkInputs(*request.source, request.compileOptions.inputs, grid));

    std::optional<noise::NoiseModel> model;
    if (options.noise != NoiseSource::Ideal) {
        QXL_TRY_ASSIGN(model, noiseFor(options));
    }

    detail::RunContext ctx;
    ctx.program = request.program;
    ctx.source = request.source;
    ctx.device = &device_->device;
    ctx.calibration = &device_->calibration;
    ctx.noise = model ? &*model : nullptr;
    ctx.choice = options.backend.value_or(backend_);
    ctx.compileOptions = request.compileOptions;
    ctx.options = options;
    ctx.stop = request.stop;
    ctx.bus = bus_; // progress and per-line drive power reach the UI from a synchronous run too
    {
        std::lock_guard lk(mu_);
        ctx.id = RunId{nextRunId_++};
    }
    QXL_TRY_ASSIGN(RunResult out, detail::performRun(ctx));
    if (grid) {
        QXL_TRY_ASSIGN(out.sweep, detail::performSweep(ctx, *grid));
    }
    return out;
}

// ---------------------------------------------------------------- asynchronous run
Session::RunState* Session::findRun(RunHandle h) const {
    std::lock_guard lk(mu_);
    auto it = runs_.find(h.get());
    return it == runs_.end() ? nullptr : it->second.get();
}

Result<RunHandle> Session::run(CompileHandle h, RunOptions options) {
    CompileState* compileState = findCompile(h);
    if (!compileState) return fail(err::UnknownHandle, "no such compile");
    if (!compileState->finished.load(std::memory_order_acquire) || !compileState->output)
        return fail(err::NotCompiled, "the program has not compiled yet");
    if (!device_) return fail(err::NoDevice, "select a device before running");
    ProgramEntry* prog = entry(compileState->program);
    const lang::Program* source = prog ? &prog->program : nullptr;
    options = resolveOptions(std::move(options), source);
    if (options.probes.empty() && source) options.probes = resolveProbes(*source, *compileState->output);
    if (options.shots == 0 || options.shots > RunOptions::kMaxShots) {
        auto d = diagnostic("QL5040", SourceSpan{}, options.shots);
        return std::unexpected(error(d));
    }
    QXL_TRY_ASSIGN(const std::optional<SweepGrid> grid, sweepGrid(source, options));
    if (source) QXL_TRY(checkInputs(*source, compileState->options.inputs, grid));

    auto state = std::make_unique<RunState>();
    RunState* raw = state.get();
    {
        std::lock_guard lk(mu_);
        raw->handle = RunHandle{nextRun_++};
        raw->id = RunId{nextRunId_++};
        raw->program = compileState->program;
        raw->compileHandle = h;
        raw->options = options;
        runs_.emplace(raw->handle.get(), std::move(state));
    }
    auto body = [this, raw, compileState, source, grid](std::stop_token worker) {
        std::stop_callback link(std::move(worker), [raw] { raw->stop.request_stop(); });
        std::optional<noise::NoiseModel> model;
        Result<RunResult> out = fail(err::Unsupported, "run not started");
        Status noiseStatus{};
        if (raw->options.noise != NoiseSource::Ideal) {
            auto m = noiseFor(raw->options);
            if (m) model = std::move(*m);
            else noiseStatus = std::unexpected(m.error());
        }
        if (noiseStatus) {
            detail::RunContext ctx;
            ctx.program = &*compileState->output;
            ctx.source = source;
            ctx.device = &device_->device;
            ctx.calibration = &device_->calibration;
            ctx.noise = model ? &*model : nullptr;
            ctx.choice = raw->options.backend.value_or(backend_);
            ctx.compileOptions = compileState->options;
            ctx.options = raw->options;
            ctx.stop = raw->stop.get_token();
            ctx.id = raw->id;
            ctx.handle = raw->handle;
            ctx.bus = bus_;   // `performRun` posts RunStarted once it knows the backend and the seed
            out = detail::performRun(ctx);
            if (out && grid) {
                auto sweep = detail::performSweep(ctx, *grid);
                if (sweep) out->sweep = std::move(*sweep);
                else out = std::unexpected(sweep.error());
            }
        } else {
            out = std::unexpected(noiseStatus.error());
        }
        if (out) {
            raw->ok = true;
            raw->result = std::move(*out);
        } else {
            raw->error = out.error();
        }
        RunRecord record;
        if (raw->ok) {
            record = RunRecord{raw->result->id,     raw->handle, raw->program,        raw->result->programHash,
                               raw->result->device, std::string(qsim::kindName(raw->result->backend)),
                               raw->options.shots,  raw->result->seed, raw->result->partial, raw->result->wallTime};
        }
        {
            std::lock_guard lk(mu_);
            if (raw->ok) history_.push_back(std::move(record));
        }
        raw->finished.store(true, std::memory_order_release);
        if (bus_)
            bus_->post(RunFinished{raw->handle, raw->id, raw->ok, raw->ok && raw->result->partial,
                                   raw->ok ? raw->result->shotsCompleted : 0,
                                   raw->ok ? raw->result->wallTime : std::chrono::nanoseconds{0}});
    };
    if (jobs_) raw->future = jobs_->submit(std::move(body), core::JobPriority::Batch);
    else body(std::stop_token{});
    return raw->handle;
}

bool Session::done(RunHandle h) const {
    const RunState* s = findRun(h);
    return s && s->finished.load(std::memory_order_acquire);
}

Status Session::wait(RunHandle h) {
    RunState* s = findRun(h);
    if (!s) return fail(err::UnknownHandle, "no such run");
    if (s->future.valid()) s->future.wait();
    if (!s->ok) return std::unexpected(s->error);
    return {};
}

void Session::cancel(RunHandle h) {
    if (RunState* s = findRun(h)) s->stop.request_stop();
}

const RunResult* Session::result(RunHandle h) const {
    const RunState* s = findRun(h);
    if (!s || !s->finished.load(std::memory_order_acquire) || !s->result) return nullptr;
    return &*s->result;
}

} // namespace qlab::runtime
