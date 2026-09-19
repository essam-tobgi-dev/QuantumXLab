// Spec 15 §5 — sweeps: `input` variables × grid → a tensor of per-point summaries. Each point binds
// its coordinates, recompiles the program (the structure may depend on the inputs) and runs with
// the base seed offset by the point index, so the whole sweep is reproducible from one seed.
#include "Runtime/Perform.hpp"
#include <cmath>
#include <format>

namespace qlab::runtime::detail {
namespace {

// The calibration fits of spec 22 §4 read y = P(first measured bit = 1) at each point.
void firstBitProbability(const RunResult& r, SweepPoint& point) {
    if (r.marginals.empty())
        return;
    const Marginal* first = nullptr;
    for (const Marginal& m : r.marginals)
        if (m.qubit != Marginal::kNoQubit) {
            first = &m;
            break;
        }
    if (!first)
        first = &r.marginals.front();
    point.p1 = first->p1;
    point.p1Stderr = first->stderr_;
}
} // namespace

Result<SweepResult> performSweep(const RunContext& ctx, const SweepGrid& grid) {
    if (!ctx.source)
        return fail(err::Unsupported, "a sweep needs the analysed program to rebind its inputs");
    if (grid.axes.size() > SweepGrid::kMaxAxes || grid.points() > SweepGrid::kMaxPoints) {
        auto d = diagnostic("QL5030", SourceSpan{}, grid.points(), SweepGrid::kMaxPoints);
        return std::unexpected(error(d));
    }
    if (grid.points() == 0)
        return fail(err::Unsupported, "the sweep grid is empty");

    SweepResult out;
    out.axes = grid.axes;
    out.points.reserve(grid.points());
    const std::uint64_t baseSeed =
        ctx.options.seed.value_or(ctx.program ? ctx.program->programHash : 0);
    for (std::size_t p = 0; p < grid.points(); ++p) {
        if (ctx.stop.stop_requested())
            break;
        const std::vector<double> coords = grid.at(p);
        compiler::CompileOptions options = ctx.compileOptions;
        for (std::size_t a = 0; a < grid.axes.size(); ++a)
            options.inputs[grid.axes[a].input] = coords[a];
        QXL_TRY_ASSIGN(
            compiler::CompiledProgram compiled,
            compiler::compile(*ctx.source, *ctx.device, *ctx.calibration, options, ctx.stop));
        compiled.programHash = ctx.program ? ctx.program->programHash : 0;

        RunContext point = ctx;
        point.program = &compiled;
        point.compileOptions = options;
        point.options.sweep.reset();
        point.options.computeEstimate = false; // the estimate is the run's, not the point's
        point.options.accurateFidelity = false;
        point.options.cadence = SnapshotCadence::None;
        point.options.seed = baseSeed + p; // spec 15 §5: the same seed offset by the point index
        QXL_TRY_ASSIGN(const RunResult r, performRun(point));

        SweepPoint sp;
        sp.coords = coords;
        sp.counts = r.counts;
        sp.expectations = r.expectations;
        firstBitProbability(r, sp);
        std::size_t k = 0;
        for (const RegisterInfo& reg : r.layout.registers) {
            if (!reg.output)
                continue;
            double sum = 0.0;
            std::size_t n = 0;
            for (const ShotRecord& s : r.memory)
                if (k < s.outputs.size()) {
                    sum += s.outputs[k];
                    ++n;
                }
            sp.outputs.push_back(n ? sum / static_cast<double>(n) : 0.0);
            ++k;
        }
        out.points.push_back(std::move(sp));
    }
    return out;
}

} // namespace qlab::runtime::detail
