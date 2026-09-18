// Spec 14 §1.1 — the passes of the standard pipeline as `IPass` adaptors over the module's functions.
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/Decompose.hpp"
#include "Compiler/Kak.hpp"
#include "Compiler/LayoutPass.hpp"
#include "Compiler/Optimize.hpp"
#include "Compiler/Pass.hpp"
#include "Compiler/Route.hpp"
#include <format>

namespace qlab::compiler {

PassMetrics PassContext::metrics(const ir::Circuit& c) const {
    PassMetrics m = measureCircuit(c);
    m.swapCount = swapCount;
    m.estimatedDuration = schedule.duration;
    return m;
}

PassMetrics PassContext::unchanged() const {
    PassMetrics m = lastMetrics;
    m.swapCount = swapCount;
    m.estimatedDuration = schedule.duration;
    return m;
}

Result<const CouplingGraph*> PassContext::graph() {
    if (!options.device) return fail(err::NoDevice, "this pass needs a device");
    if (!coupling) {
        QXL_TRY_ASSIGN(CouplingGraph g, CouplingGraph::build(*options.device, options.calibration));
        coupling = std::move(g);
    }
    return &*coupling;
}

namespace {
struct Verify final : IPass {
    std::string_view name() const override { return "Verify"; }
    Result<PassMetrics> run(ir::Circuit& c, PassContext& ctx) override {
        QXL_TRY(ir::verify(c));
        if (!ctx.options.device) QXL_TRY(requireResolvedDurations(c));   // QL4010
        return ctx.unchanged();
    }
};

struct Decompose final : IPass {
    std::string_view name() const override { return "Decompose"; }
    Result<PassMetrics> run(ir::Circuit& c, PassContext& ctx) override {
        QXL_TRY(decompose(c, ctx.target, ctx.stop));
        return ctx.metrics(c);
    }
};

struct Optimize final : IPass {
    std::string_view name() const override { return "Optimize"; }
    Result<PassMetrics> run(ir::Circuit& c, PassContext& ctx) override {
        QXL_TRY(optimize(c, ctx.target, OptimizeOptions{}, ctx.stop));
        if (ctx.options.kak || ctx.options.level == OptimizeLevel::O2) {   // §5.5: level 2 adds two-qubit resynthesis
            QXL_TRY_ASSIGN(const KakStats k, resynthesizeTwoQubitBlocks(c, ctx.target, ctx.stop));
            if (k.replaced > 0) QXL_TRY(optimize(c, ctx.target, OptimizeOptions{}, ctx.stop));   // fuse the new local gates
        }
        return ctx.metrics(c);
    }
};

struct LayoutSelect final : IPass {
    std::string_view name() const override { return "Layout"; }
    Result<PassMetrics> run(ir::Circuit& c, PassContext& ctx) override {
        QXL_TRY_ASSIGN(const CouplingGraph* g, ctx.graph());
        if (c.isPhysical()) {   // `pragma qlab.layout physical`: the program chose its qubits
            ctx.initialLayout = ctx.finalLayout = Layout::identity(c.qubitCount());
            return ctx.unchanged();
        }
        QXL_TRY_ASSIGN(LayoutChoice choice, chooseLayout(c, *g, ctx.options.layout, ctx.options.seed, ctx.options.vf2StateBudget, &ctx.diagnostics));
        ctx.initialLayout = ctx.finalLayout = choice.layout;
        c.setLayout(choice.layout.v2p);
        c.meta()["layout_policy"] = std::string(layoutPolicyName(choice.policy));
        c.meta()["layout_score"] = choice.score;
        return ctx.unchanged();
    }
};

struct Route final : IPass {
    std::string_view name() const override { return "Route"; }
    Result<PassMetrics> run(ir::Circuit& c, PassContext& ctx) override {
        QXL_TRY_ASSIGN(const CouplingGraph* g, ctx.graph());
        Result<RoutingResult> routed = [&]() -> Result<RoutingResult> {
            if (c.isPhysical()) return adoptPhysical(c, *g);   // `qlab.layout physical`: validate only
            if (ctx.options.routing == RoutingPolicy::None) return applyLayout(c, ctx.initialLayout, *g);
            RouteOptions o;
            o.seed = ctx.options.seed;
            o.trials = ctx.options.level == OptimizeLevel::O2 ? 4 : 1;   // spec 14 §8: level 2 tries 4 seeds
            o.noiseAware = ctx.options.layout == LayoutPolicy::NoiseAware && g->hasCalibration();
            return route(c, ctx.initialLayout, *g, o, ctx.stop);
        }();
        if (!routed) return std::unexpected(routed.error());
        ctx.initialLayout = routed->initialLayout;
        ctx.finalLayout = routed->finalLayout;
        ctx.swapCount = routed->swaps;
        c = std::move(routed->circuit);
        c.meta()["device"] = ctx.options.device->id;
        return ctx.metrics(c);
    }
};

struct VirtualZ final : IPass {
    std::string_view name() const override { return "VirtualZ"; }
    Result<PassMetrics> run(ir::Circuit& c, PassContext& ctx) override {
        if (ctx.target.isNative1q("rz")) QXL_TRY(virtualZ(c));
        return ctx.unchanged();
    }
};

struct ScheduleNodes final : IPass {
    std::string_view name() const override { return "Schedule"; }
    Result<PassMetrics> run(ir::Circuit& c, PassContext& ctx) override {
        const CompileContext& o = ctx.options;
        if (!o.device || !o.calibration) return fail(err::NoDevice, "scheduling needs a device and its calibration");
        const bool calibrated = c.meta().contains("calibrations") && !c.meta()["calibrations"].empty();
        if (o.pulseLevel && !o.pulses) return fail(err::NoDevice, "a pulse-level compile needs the device's pulse library (pulses.json)");
        if (o.pulseLevel || calibrated) {   // block durations come from the pulses themselves
            QXL_TRY_ASSIGN(PulseSource source, PulseSource::create(*o.device, o.pulseLevel ? o.pulses : nullptr, c));
            ctx.pulseSource = std::move(source);
        }
        ScheduleOptions so;
        so.policy = o.schedule;
        QXL_TRY_ASSIGN(ctx.schedule, schedule(c, *o.device, *o.calibration, so, ctx.pulseSource ? &*ctx.pulseSource : nullptr));
        return ctx.unchanged();
    }
};

struct PulseLower final : IPass {
    std::string_view name() const override { return "PulseLower"; }
    Result<PassMetrics> run(ir::Circuit& c, PassContext& ctx) override {
        const CompileContext& o = ctx.options;
        if (!ctx.pulseSource || !o.device) return fail(err::NoDevice, "pulse lowering runs after Schedule on a device with a pulse library");
        if (o.enforcePulseQubitCap) {   // spec 15 §2: the Lindblad backend takes 5 transmons or 6 ions
            const std::uint32_t cap = o.pulseQubitCap ? o.pulseQubitCap : (o.device->technology == hw::Technology::IonChain ? 6u : 5u);
            if (usedWires(c).size() > cap) return fail(lang::Diagnostics::make("QL4040", SourceSpan{}, cap).error);
        }
        QXL_TRY_ASSIGN(PulseProgram program, lowerToPulses(c, ctx.schedule, *ctx.pulseSource));
        ctx.pulseProgram = std::move(program);   // non-fatal verifier findings (W_DEAD_PHASE) travel inside it
        return ctx.unchanged();
    }
};

struct Equivalence final : IPass {
    std::string_view name() const override { return "Equivalence"; }
    Result<PassMetrics> run(ir::Circuit& c, PassContext& ctx) override {
        if (!ctx.options.verifyEquivalence || !ctx.reference) return ctx.unchanged();
        EquivalenceReport skipped;
        if (ctx.reference->qubitCount() > 10) {   // spec 14 §1.1: automatic for n ≤ 10, on demand above
            skipped.detail = "more than 10 program qubits: run checkEquivalence on demand";
            ctx.equivalence = skipped;
            return ctx.unchanged();
        }
        EquivalenceOptions eo;
        eo.seed = ctx.options.seed;
        eo.workLimit = ctx.options.equivalenceWorkLimit;
        QXL_TRY_ASSIGN(const EquivalenceReport report, checkEquivalence(*ctx.reference, c, eo));
        ctx.equivalence = report;
        if (!report.equivalent && report.method != EquivalenceMethod::Skipped)
            return fail(err::NotEquivalent, std::format("internal error: the compiled circuit is not equivalent to the source ({}: {})",
                                                        equivalenceMethodName(report.method), report.detail));
        return ctx.unchanged();
    }
};
} // namespace

std::unique_ptr<IPass> makeVerifyPass() { return std::make_unique<Verify>(); }
std::unique_ptr<IPass> makeDecomposePass() { return std::make_unique<Decompose>(); }
std::unique_ptr<IPass> makeOptimizePass() { return std::make_unique<Optimize>(); }
std::unique_ptr<IPass> makeLayoutPass() { return std::make_unique<LayoutSelect>(); }
std::unique_ptr<IPass> makeRoutePass() { return std::make_unique<Route>(); }
std::unique_ptr<IPass> makeVirtualZPass() { return std::make_unique<VirtualZ>(); }
std::unique_ptr<IPass> makeSchedulePass() { return std::make_unique<ScheduleNodes>(); }
std::unique_ptr<IPass> makePulseLowerPass() { return std::make_unique<PulseLower>(); }
std::unique_ptr<IPass> makeEquivalencePass() { return std::make_unique<Equivalence>(); }

} // namespace qlab::compiler
