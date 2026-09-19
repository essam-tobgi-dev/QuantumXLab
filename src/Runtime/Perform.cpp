// Spec 15 §3–§4, §6–§9 — one run: plan, backend selection, execution, the summary of §4 and the
// estimate of §6–§9.
#include "Runtime/Perform.hpp"
#include "Data/Fidelity.hpp"
#include "Runtime/Select.hpp"
#include "Runtime/Session.hpp" // the RunProgress event
#include <chrono>
#include <cmath>
#include <format>

namespace qlab::runtime::detail {
namespace {

std::vector<std::string> outputNames(const lang::Program* p) {
    return p ? p->outputs : std::vector<std::string>{};
}

// Spec 15 §1: the seed actually used. An unset option takes the program's `pragma qlab.seed`, else
// a value derived from the program hash — deterministic and recorded, never a wall-clock seed (04
// §6).
std::uint64_t resolveSeed(const RunContext& ctx) {
    if (ctx.options.seed)
        return *ctx.options.seed;
    if (ctx.source && ctx.source->pragmas.seed)
        return *ctx.source->pragmas.seed;
    const std::uint64_t h = ctx.program->programHash;
    return h ? h : 0x51AB5EED51AB5EEDull;
}

data::FidelityClass classOf(qsim::Kind kind, bool stochastic, data::FidelityClass applied) {
    if (kind == qsim::Kind::Lindblad)
        return data::weakest(applied, data::FidelityClass::Numerical);
    if (stochastic)
        return data::weakest(applied, data::FidelityClass::Statistical);
    if (kind == qsim::Kind::DensityMatrix)
        return data::weakest(applied, data::FidelityClass::Numerical);
    return applied;
}
} // namespace

Result<RunResult> performRun(const RunContext& ctx) {
    if (!ctx.program)
        return fail(err::NotCompiled, "the run needs a compiled program");
    if (!ctx.device || !ctx.calibration)
        return fail(err::NoDevice, "the run needs a device and a calibration");
    const RunOptions& options = ctx.options;
    if (options.shots == 0 || options.shots > RunOptions::kMaxShots) {
        auto d = diagnostic("QL5040", SourceSpan{}, options.shots);
        return std::unexpected(error(d));
    }
    QXL_TRY_ASSIGN(const ProgramPlan plan,
                   planProgram(ctx.program->circuit, outputNames(ctx.source)));

    const bool pulseLevel = ctx.program->pulses.has_value();
    BackendRequest req;
    req.plan = &plan;
    req.choice = ctx.choice;
    req.hasNoise = ctx.noise != nullptr;
    req.pauliNoiseOnly = ctx.noise && ctx.noise->isPauliOnly();
    req.pulseLevel = pulseLevel;
    req.levels = pulseLevel ? pulseSiteLevels(*ctx.device, options) : options.levels;
    req.shots = options.shots;
    QXL_TRY_ASSIGN(const qsim::Selection selection, chooseBackend(req));

    ExecutionInput in;
    in.program = ctx.program;
    in.plan = &plan;
    in.device = ctx.device;
    in.calibration = ctx.calibration;
    in.noise = ctx.noise;
    in.backend = selection.kind;
    in.stochasticUnravelling = selection.stochasticUnravelling;
    in.shots = options.shots;
    in.seed = resolveSeed(ctx);
    RunOptions effective = options;
    effective.levels = req.levels;
    in.options = &effective;
    in.stop = ctx.stop;
    in.events = ctx.bus;
    in.id = ctx.id;
    if (ctx.bus) {
        const RunHandle handle = ctx.handle;
        const RunId id = ctx.id;
        core::EventBus* bus = ctx.bus;
        const std::uint64_t total = options.shots;
        in.progress = [bus, handle, id, total](std::uint64_t done, std::uint64_t) {
            bus->post(RunProgress{handle, id, done, total});
        };
        // The run header of spec 15 §2: the backend and why it was chosen, before the first shot.
        ctx.bus->post(RunStarted{handle, id, ctx.device->id, selection.reason, selection.kind,
                                 options.shots, in.seed});
    }

    const auto started = std::chrono::steady_clock::now();
    QXL_TRY_ASSIGN(ExecutionOutput exec, execute(in));
    const auto elapsed = std::chrono::steady_clock::now() - started;

    RunResult out;
    out.id = ctx.id;
    out.programHash = ctx.program->programHash;
    out.device = ctx.device->id;
    out.calibrationTimestamp = ctx.calibration->timestamp;
    out.backend = selection.kind;
    out.backendReason = selection.reason;
    out.backendClass = classOf(selection.kind, selection.stochasticUnravelling, exec.cls);
    out.options = options;
    out.seed = in.seed;
    out.wallTime = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
    out.partial = exec.partial;
    out.layout = plan.layout;
    out.memory = std::move(exec.memory);
    out.exact = std::move(exec.exact);
    out.exactClass = exec.exactClass;
    out.snapshots = std::move(exec.snapshots);
    out.metrics = ctx.program->metrics;
    out.diagnostics = std::move(exec.diagnostics);
    out.qubits = plan.qubits;
    out.levels = effective.levels;
    out.measurements = plan.measurements;
    out.circuit = std::make_shared<const ir::Circuit>(ctx.program->circuit);
    out.source = std::make_shared<const ir::Circuit>(ctx.program->source);
    out.initialLayout = ctx.program->initialLayout;
    out.finalLayout = ctx.program->finalLayout;
    out.schedule = std::move(exec.schedule);
    out.lindblad = std::move(exec.lindblad);
    out.finalState = std::move(exec.finalState);
    out.shotsCompleted = exec.shotsCompleted;
    for (const std::string& id : exec.twirled) // spec 08 §7.3: the twirl changes the model
        out.diagnostics.push_back(
            note(std::format("channel '{}' was applied as its Pauli twirl (class Model)", id),
                 lang::Severity::Warning));
    if (!exec.twirled.empty())
        out.backendClass = data::weakest(out.backendClass, data::FidelityClass::Model);
    summarize(out, plan, ctx.device, options.computeExpectations);
    if (!options.keepMemory)
        out.memory.clear();

    if (options.computeEstimate) {
        EstimateInput est;
        est.device = ctx.device;
        est.calibration = ctx.calibration;
        est.program = ctx.program;
        est.shots = options.shots;
        est.usedQubits = plan.qubits;
        for (std::uint32_t sim : plan.measuredQubits)
            est.measuredQubits.push_back(plan.qubits[sim]);
        est.branchesPerShot = exec.branchesPerShot;
        QXL_TRY_ASSIGN(out.estimate, estimate(est));
        if (options.compareDevices && ctx.source) { // spec 15 §9: one row per device in the catalog
            if (auto rows = compareDevices(*ctx.source, ctx.compileOptions, options.shots,
                                           ctx.device->id, ctx.stop))
                out.estimate.comparison = std::move(*rows);
            else if (rows.error().code != ErrorCode::Cancelled)
                out.diagnostics.push_back(
                    note("the device comparison is unavailable: " + rows.error().message,
                         lang::Severity::Warning));
        }
        if (options.accurateFidelity) {
            if (auto sim = simulatedFidelity(ctx, out))
                out.estimate.fidelity.simulated = std::move(*sim);
            else
                out.diagnostics.push_back(
                    note("the simulated fidelity is unavailable: " + sim.error().message,
                         lang::Severity::Warning));
        }
    }
    return out;
}

} // namespace qlab::runtime::detail
