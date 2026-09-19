// Spec 15 §3.5 — walking a compiled circuit for one shot: gates with their channels, scheduled
// idle intervals, projective measurement with the readout POVM, reset, classical control.
#include "Runtime/Engine.hpp"
#include <algorithm>
#include <format>

namespace qlab::runtime::detail {

Status Engine::applyIdle(std::size_t nodeIndex, ShotContext& ctx) {
    if (!in_.noise)
        return {};
    while (idleCursor_ < idle_.size() && idle_[idleCursor_].nodeIndex < nodeIndex)
        ++idleCursor_;
    for (std::size_t k = idleCursor_; k < idle_.size() && idle_[k].nodeIndex == nodeIndex; ++k) {
        const IdleGap& g = idle_[k];
        const std::int32_t s = g.wire < plan_.toSim.size() ? plan_.toSim[g.wire] : -1;
        if (s < 0)
            continue;
        QXL_TRY(applyChannels(remap(in_.noise->idleChannels(QubitIndex{g.wire}, g.length)), ctx));
    }
    return {};
}

Status Engine::applyGate(const ir::Gate& g, ShotContext& ctx) {
    QXL_TRY_ASSIGN(qsim::GateOp op, ir::toGateOp(g));
    const auto toSim = [&](std::vector<QubitIndex>& v) {
        for (auto& q : v) {
            const std::int32_t s = q.get() < plan_.toSim.size() ? plan_.toSim[q.get()] : -1;
            q = QubitIndex{static_cast<std::uint32_t>(std::max<std::int32_t>(s, 0))};
        }
    };
    toSim(op.targets);
    toSim(op.controls);
    op.opIndex = gateIndex_;
    // Before / During / After: gate-level backends apply During and After behind the ideal gate
    // (first-order splitting, spec 08 §4).
    std::vector<noise::AttachedChannel> before, after;
    if (in_.noise) {
        std::vector<QubitIndex> phys;
        for (ir::Wire w : g.wires())
            phys.push_back(QubitIndex{w.index});
        for (auto& ac : in_.noise->channelsFor(g.name, phys))
            (ac.placement == noise::Placement::Before ? before : after).push_back(std::move(ac));
    }
    if (!before.empty())
        QXL_TRY(applyChannels(remap(std::move(before)), ctx));
    QXL_TRY(backend_->apply(op));
    if (!after.empty())
        QXL_TRY(applyChannels(remap(std::move(after)), ctx));
    return {};
}

Status Engine::applyMeasure(const ir::Measure& m, ShotContext& ctx) {
    const std::int32_t s = m.qubit.index < plan_.toSim.size() ? plan_.toSim[m.qubit.index] : -1;
    if (s < 0)
        return fail(err::Unsupported, "measurement on an unused wire");
    const std::uint32_t sim = static_cast<std::uint32_t>(s);
    if (in_.noise) { // dephasing of the qubits sharing the feedline (spec 08 §3)
        std::vector<QubitIndex> measured{QubitIndex{m.qubit.index}};
        QXL_TRY(applyChannels(remap(in_.noise->measurementChannels(measured)), ctx));
    }
    // Model (a) (spec 15 §3.5): the terminal measurements are replaced by one Born sampling pass of
    // N shots, so the projective collapse must NOT run here — it would pin every shot to one
    // outcome.
    if (!project_)
        return {};
    QXL_TRY_ASSIGN(const noise::ReadoutModel* readout, readoutFor(sim));
    QXL_TRY_ASSIGN(qsim::Outcome outcome, noise::measureWithReadout(*backend_, *readout, *ctx.rng));
    const std::uint8_t bit = outcome.bits.empty() ? 0 : outcome.bits.front();
    ctx.measured[sim] = static_cast<std::int8_t>(bit);
    if (m.bit != ir::kNoBit && m.bit.index < ctx.bits.size())
        ctx.bits[m.bit.index] = bit;
    return {};
}

Status Engine::applyReset(const ir::Reset& r, ShotContext& ctx) {
    const std::int32_t s = r.qubit.index < plan_.toSim.size() ? plan_.toSim[r.qubit.index] : -1;
    if (s < 0)
        return {};
    const QubitIndex sim{static_cast<std::uint32_t>(s)};
    QXL_TRY(backend_->reset({&sim, 1}, *ctx.rng));
    ctx.measured[sim.get()] = -1;
    if (!in_.noise)
        return {};
    std::vector<QubitIndex> phys{QubitIndex{r.qubit.index}};
    return applyChannels(remap(in_.noise->channelsFor("reset", phys)), ctx);
}

void Engine::maybeSnapshot(std::size_t nodeIndex, const ir::Node& n, ShotContext& ctx) {
    if (!collect_ || cadence_ == SnapshotCadence::None || cadence_ == SnapshotCadence::End)
        return;
    if (snapshots_.size() >= options_.maxSnapshots)
        return;
    bool boundary = false;
    switch (cadence_) {
    case SnapshotCadence::Gate:
        boundary = ir::isQuantum(n);
        break;
    case SnapshotCadence::Layer:
        boundary = ir::isQuantum(n) && layerOf_[nodeIndex] != lastLayer_;
        break;
    case SnapshotCadence::Barrier:
        boundary = std::holds_alternative<ir::Barrier>(n);
        break;
    default:
        break;
    }
    lastLayer_ = layerOf_[nodeIndex];
    if (!boundary)
        return;
    RunSnapshot snap;
    snap.gateIndex = nodeIndex;
    snap.layerIndex = layerOf_[nodeIndex];
    snap.node = circuit_.topologicalOrder()[nodeIndex];
    snap.shot = ctx.shot;
    const auto& timing = in_.program->timing;
    snap.timeS = nodeIndex < timing.nodes.size()
                     ? static_cast<double>(timing.nodes[nodeIndex].end().get()) * 1e-12
                     : 0.0;
    snap.state = capture(snap.timeS, nodeIndex);
    snap.measuredBits = ctx.measured;
    if (!options_.probes.empty()) // spec 12 §8: probe values are computed on the worker
        snap.probes = evaluateProbes(*backend_, plan_, options_.probes);
    snapshots_.push_back(std::move(snap));
}

Status Engine::runCircuit(const ir::Circuit& c, ShotContext& ctx, bool topLevel) {
    const auto order = c.topologicalOrder();
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (in_.stop.stop_requested())
            return fail(ErrorCode::Cancelled, "run cancelled");
        const ir::Node& n = c.node(order[i]);
        if (topLevel)
            QXL_TRY(applyIdle(i, ctx));
        if (const auto* g = std::get_if<ir::Gate>(&n)) {
            QXL_TRY(applyGate(*g, ctx));
            ++gateIndex_;
        } else if (const auto* m = std::get_if<ir::Measure>(&n)) {
            QXL_TRY(applyMeasure(*m, ctx));
        } else if (const auto* r = std::get_if<ir::Reset>(&n)) {
            QXL_TRY(applyReset(*r, ctx));
        } else if (const auto* d = std::get_if<ir::Delay>(&n)) {
            if (in_.noise && d->duration.ps.get() > 0) {
                const auto wires = ir::nodeWiresIn(n, c.qubitCount());
                for (ir::Wire w : wires) {
                    const std::int32_t s = w.index < plan_.toSim.size() ? plan_.toSim[w.index] : -1;
                    if (s < 0)
                        continue;
                    QXL_TRY(applyChannels(
                        remap(in_.noise->idleChannels(QubitIndex{w.index}, d->duration.ps)), ctx));
                }
            }
        } else if (const auto* op = std::get_if<ir::ClassicalOp>(&n)) {
            ir::applyClassicalOp(*op, ctx.bits);
        } else if (const auto* b = std::get_if<ir::Branch>(&n)) {
            ++ctx.branches;
            const bool taken = b->cond.eval(ctx.bits) != 0;
            const ir::SubCircuit& body = taken ? b->thenBody : b->elseBody;
            if (body.present())
                QXL_TRY(runCircuit(*body, ctx, false));
        } else if (const auto* l = std::get_if<ir::Loop>(&n)) {
            const std::uint32_t bound =
                l->maxIterations > 0 ? l->maxIterations : options_.maxLoopIterations;
            std::uint32_t iter = 0;
            while (l->cond.eval(ctx.bits) != 0) {
                if (iter >= bound) { // QL5020: this shot is truncated, the run continues
                    ctx.truncated = true;
                    break;
                }
                ++iter;
                ++ctx.branches;
                if (l->body.present())
                    QXL_TRY(runCircuit(*l->body, ctx, false));
            }
        } else if (const auto* bx = std::get_if<ir::Box>(&n)) {
            if (bx->body.present())
                QXL_TRY(runCircuit(*bx->body, ctx, false));
        }
        if (topLevel)
            maybeSnapshot(i, n, ctx);
    }
    return {};
}

} // namespace qlab::runtime::detail
