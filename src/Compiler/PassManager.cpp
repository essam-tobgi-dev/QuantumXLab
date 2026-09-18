// Spec 14 §1 — the pass manager: fixed pipeline, per-pass metrics and wall time, cancellation.
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/Pass.hpp"
#include "Core/Timer.hpp"
#include "Hardware/Calibration.hpp"
#include <chrono>

namespace qlab::compiler {
namespace {
lang::Diagnostic diagnosticOf(const Error& e) {
    lang::Diagnostic d;
    d.severity = lang::Severity::Error;
    d.error = e;
    return d;
}
std::chrono::microseconds elapsed(const core::Timer& t) {
    return std::chrono::microseconds(static_cast<std::int64_t>(t.seconds() * 1e6));
}
} // namespace

PassManager PassManager::standard(const CompileContext& cc) {
    PassManager pm(cc);
    const bool optimizing = cc.level != OptimizeLevel::O0;
    pm.add(makeVerifyPass());                          // 2
    pm.add(makeDecomposePass());                       // 3
    if (optimizing) pm.add(makeOptimizePass());        // 4
    if (cc.device) {
        pm.add(makeLayoutPass());                      // 5
        pm.add(makeRoutePass());                       // 6
        pm.add(makeDecomposePass());                   // 7: swaps, directed couplers
        if (optimizing) pm.add(makeOptimizePass());
        if (optimizing) pm.add(makeVirtualZPass());    // 8
        pm.add(makeSchedulePass());                    // 9
        if (cc.pulseLevel) pm.add(makePulseLowerPass());   // 10
    }
    pm.add(makeVerifyPass());                          // 11
    pm.add(makeEquivalencePass());
    return pm;
}

std::vector<std::string> PassManager::passNames() const {
    std::vector<std::string> names;
    for (const auto& p : passes_) names.emplace_back(p->name());
    return names;
}

std::size_t PassManager::frontEnd() const {
    for (std::size_t i = 0; i < passes_.size(); ++i)
        if (passes_[i]->name() == "Layout") return i;
    return passes_.size();
}

Result<CompileOutput> PassManager::run(ir::Circuit circuit, std::stop_token stop) {
    trace_.clear();
    normalizeBodies(circuit);
    auto reference = std::make_shared<const ir::Circuit>(circuit);
    return runRange(std::move(circuit), std::move(reference), 0, passes_.size(), std::move(stop));
}

Result<CompileOutput> PassManager::run(const lang::Program& program, const ir::ParamMap& inputs, std::stop_token stop) {
    trace_.clear();
    QXL_TRY_ASSIGN(ir::Circuit built, build(program, inputs));
    auto reference = std::make_shared<const ir::Circuit>(built);
    return runRange(std::move(built), std::move(reference), 0, passes_.size(), std::move(stop));
}

Result<ir::Circuit> PassManager::build(const lang::Program& program, const ir::ParamMap& inputs) {
    // Pass 1, Build (spec 14 §2): AST → IR.
    const core::Timer timer;
    ir::BuildOptions bo;
    bo.loopUnrollBound = context_.loopUnrollBound;
    auto built = ir::buildCircuit(program, inputs, bo);
    PassResult build;
    build.pass = "Build";
    build.wallTime = elapsed(timer);
    if (!built) {
        build.diagnostics.push_back(diagnosticOf(built.error()));
        trace_.push_back(std::move(build));
        return std::unexpected(built.error());
    }
    normalizeBodies(*built);   // an absent `else` arrives as an empty virtual body (see CircuitUtil.hpp)
    build.after = measureCircuit(*built);
    trace_.push_back(std::move(build));
    return built;
}

Result<CompileOutput> PassManager::runRange(ir::Circuit circuit, std::shared_ptr<const ir::Circuit> reference, std::size_t first,
                                            std::size_t last, std::stop_token stop) {
    PassContext ctx(context_, stop);
    ctx.reference = std::move(reference);
    if (context_.device) {
        QXL_TRY_ASSIGN(ctx.target, Target::forDevice(*context_.device, context_.twoQubitBasis));
    }
    // A circuit resumed after routing carries its layouts in its metadata.
    if (circuit.isPhysical() && first > 0) {
        ctx.initialLayout.v2p = metaIndices(circuit, "layout").value_or(std::vector<std::uint32_t>{});
        ctx.finalLayout.v2p = metaIndices(circuit, "final_layout").value_or(ctx.initialLayout.v2p);
        const auto& meta = circuit.meta();
        if (meta.contains("swap_count") && meta["swap_count"].is_number_unsigned()) ctx.swapCount = meta["swap_count"].get<std::uint32_t>();
    }
    CompileOutput out;
    PassMetrics current = ctx.metrics(circuit);   // each pass starts from its predecessor's metrics
    for (std::size_t i = first; i < last && i < passes_.size(); ++i) {
        IPass& pass = *passes_[i];
        PassResult result;
        result.pass = std::string(pass.name());
        if (stop.stop_requested()) {
            const Error cancelled(ErrorCode::Cancelled, "compile cancelled");
            result.diagnostics.push_back(diagnosticOf(cancelled));
            trace_.push_back(std::move(result));
            return std::unexpected(cancelled);
        }
        result.before = ctx.lastMetrics = current;
        const core::Timer timer;
        auto after = pass.run(circuit, ctx);
        result.wallTime = elapsed(timer);
        result.diagnostics = std::move(ctx.diagnostics);
        ctx.diagnostics.clear();
        if (!after) {
            result.diagnostics.push_back(diagnosticOf(after.error()));
            trace_.push_back(std::move(result));
            return std::unexpected(after.error());
        }
        result.after = current = *after;
        for (const auto& d : result.diagnostics) out.diagnostics.push_back(d);
        trace_.push_back(std::move(result));
    }
    out.metrics = current;
    out.circuit = std::move(circuit);
    if (ctx.reference) out.source = *ctx.reference;
    out.timing = std::move(ctx.schedule);
    out.pulses = std::move(ctx.pulseProgram);
    out.initialLayout = std::move(ctx.initialLayout);
    out.finalLayout = std::move(ctx.finalLayout);
    out.equivalence = std::move(ctx.equivalence);
    out.trace = trace_;
    if (context_.device) out.deviceId = context_.device->id;
    if (context_.calibration) out.calibrationTimestamp = context_.calibration->timestamp;
    return out;
}

} // namespace qlab::compiler
