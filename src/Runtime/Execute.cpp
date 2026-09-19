// Spec 15 §3.5 — the two gate-level execution models and the cancellation contract.
//   (a) terminal measurement only: one evolution + Born sampling of N shots;
//   (b) mid-circuit measurement, feedforward, reset or trajectory noise: one evolution per shot on
//       the seeded stream `Random::stream(seed, s)`.
#include "Data/Fidelity.hpp"
#include "Runtime/Engine.hpp"
#include <format>

namespace qlab::runtime {
namespace {

std::vector<double> outputsOf(const ClassicalLayout& layout, std::span<const std::uint8_t> bits) {
    std::vector<double> out;
    for (const RegisterInfo& r : layout.registers) {
        if (!r.output)
            continue;
        const std::uint64_t v = layout.value(bits, r);
        if (r.kind == ir::RegKind::Int && r.size > 0 && r.size <= 64 && (v >> (r.size - 1)) & 1) {
            const std::uint64_t span = r.size >= 64 ? 0 : (std::uint64_t{1} << r.size);
            out.push_back(static_cast<double>(v) - static_cast<double>(span));
        } else {
            out.push_back(static_cast<double>(v));
        }
    }
    return out;
}

bool cancelled(const Error& e) {
    return e.code == ErrorCode::Cancelled;
}
} // namespace

Result<ExecutionOutput> execute(const ExecutionInput& in) {
    if (!in.program || !in.plan)
        return fail(err::NotCompiled, "execute needs a compiled program and its plan");
    if (in.backend == qsim::Kind::Lindblad)
        return executePulse(in); // spec 15 §3.5 (c)
    if (in.backend == qsim::Kind::Trajectories)
        return fail(err::Unsupported,
                    "the Trajectories backend is not a run backend (spec 15 §2 selects lindblad)");
    static const RunOptions kDefault;
    const RunOptions& options = in.options ? *in.options : kDefault;
    const ProgramPlan& plan = *in.plan;

    ExecutionOutput out;
    detail::Engine engine(in, plan);
    QXL_TRY(engine.allocate());
    core::Random rng(in.seed);

    // Trajectory noise collapses model (a) into model (b) (spec 15 §3.5).
    const bool perShot = !plan.terminalMeasurementOnly || in.stochasticUnravelling;
    if (!perShot) {
        detail::ShotContext ctx;
        ctx.rng = &rng;
        engine.setProjectMeasurements(
            false); // the N shots are sampled from the final Born distribution
        QXL_TRY(engine.prepare(ctx));
        if (auto st = engine.runCircuit(in.program->circuit, ctx, true); !st) {
            if (!cancelled(st.error()))
                return std::unexpected(st.error());
            out.partial = true;
            out.diagnostics.push_back(diagnostic("QL5060", SourceSpan{}));
            return out;
        }
        out.finalState = engine.capture();
        QXL_TRY(engine.sampleTerminal(in.shots, rng, out.memory, out.exact));
        for (ShotRecord& r : out.memory)
            r.outputs = outputsOf(plan.layout, r.bits);
        out.shotsCompleted = in.shots;
        if (in.progress)
            in.progress(in.shots, in.shots);
    } else {
        out.memory.reserve(in.shots);
        std::uint64_t branches = 0;
        for (std::uint32_t s = 0; s < in.shots; ++s) {
            if (in.stop.stop_requested()) {
                out.partial = true;
                break;
            }
            core::Random shotRng = rng.stream(s); // spec 15 §3.5 (b), spec 04 §6
            detail::ShotContext ctx;
            ctx.rng = &shotRng;
            ctx.shot = s;
            engine.setCollectSnapshots(s == 0); // the state of later shots differs per trajectory
            QXL_TRY(engine.prepare(ctx));
            if (auto st = engine.runCircuit(in.program->circuit, ctx, true); !st) {
                if (!cancelled(st.error()))
                    return std::unexpected(st.error());
                out.partial = true;
                break;
            }
            if (s == 0)
                out.finalState = engine.capture();
            ShotRecord rec;
            rec.outputs = outputsOf(plan.layout, ctx.bits);
            rec.bits = std::move(ctx.bits);
            rec.truncated = ctx.truncated;
            branches += ctx.branches;
            if (rec.truncated && out.diagnostics.empty())
                out.diagnostics.push_back(
                    diagnostic("QL5020", SourceSpan{}, options.maxLoopIterations));
            out.memory.push_back(std::move(rec));
            ++out.shotsCompleted;
            if (in.progress && (s % 64 == 0 || s + 1 == in.shots))
                in.progress(out.shotsCompleted, in.shots);
        }
        out.branchesPerShot = out.shotsCompleted ? static_cast<double>(branches) /
                                                       static_cast<double>(out.shotsCompleted)
                                                 : 0.0;
    }
    if (out.partial)
        out.diagnostics.push_back(diagnostic("QL5060", SourceSpan{}));
    out.snapshots = std::move(engine.snapshots());
    out.cls = data::weakest(engine.report().cls, in.stochasticUnravelling
                                                     ? data::FidelityClass::Statistical
                                                     : data::FidelityClass::Exact);
    if (in.backend == qsim::Kind::DensityMatrix)
        out.exactClass = data::FidelityClass::Exact;
    out.twirled = engine.report().twirled;
    return out;
}

void summarize(RunResult& out, const ProgramPlan& plan, const hw::Device* device,
               bool expectations) {
    const ClassicalLayout& layout = out.layout;
    out.counts = data::Histogram(layout.bits);
    for (const ShotRecord& r : out.memory)
        out.counts.add(layout.key(r.bits));
    const std::uint64_t n = out.counts.total();
    if (out.exact) { // Born probabilities parallel to `counts.all()` (spec 22 §2)
        std::vector<double> theory;
        for (const auto& [label, count] : out.counts.all()) {
            const std::uint64_t index = data::Histogram::indexFromLabel(label);
            theory.push_back(index < out.exact->size() ? (*out.exact)[index] : 0.0);
        }
        out.counts.theory = std::move(theory);
    }

    // Marginals per register bit, with the qubit that was measured into it (spec 15 §4).
    std::map<std::uint32_t, std::uint32_t> qubitOfBit;
    for (const MeasuredBit& m : plan.measurements)
        if (m.bit != ir::kNoBit)
            qubitOfBit[m.bit.index] = m.physical;
    out.marginals.clear();
    for (const RegisterInfo& reg : layout.registers) {
        for (std::uint32_t i = 0; i < reg.size; ++i) {
            const std::uint32_t flat = reg.first + i;
            std::uint64_t ones = 0;
            for (const ShotRecord& r : out.memory)
                if (flat < r.bits.size() && r.bits[flat])
                    ++ones;
            Marginal m;
            m.register_ = reg.name;
            m.bit = i;
            auto q = qubitOfBit.find(flat);
            m.qubit = q == qubitOfBit.end() ? Marginal::kNoQubit : q->second;
            m.p1 = n ? static_cast<double>(ones) / static_cast<double>(n) : 0.0;
            m.stderr_ =
                n ? std::sqrt(std::max(0.0, m.p1 * (1.0 - m.p1)) / static_cast<double>(n)) : 0.0;
            m.interval = data::wilson(ones, n);
            out.marginals.push_back(std::move(m));
        }
    }
    if (!expectations)
        return;
    out.expectations = computeExpectations(out, plan, device);
}

} // namespace qlab::runtime
