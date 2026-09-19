// Spec 14 §10 — precompiled simulation of a circuit with planned measurement outcomes.
#include "Compiler/EquivalenceImpl.hpp"
#include <format>
#include <map>

namespace qlab::compiler::detail {
namespace {
constexpr std::uint32_t kUnmapped = 0xFFFFFFFFu;
constexpr std::uint32_t kLoopCap = 64; // iterations simulated of a run-time loop
constexpr double kImpossible = 1e-9;   // an outcome below this probability is not taken

Result<std::uint32_t> mapWire(ir::Wire w, const std::vector<std::uint32_t>& wireMap) {
    if (w.index >= wireMap.size() || wireMap[w.index] == kUnmapped)
        return fail(ErrorCode::Internal,
                    std::format("wire {} is used but was not mapped for simulation", w.index));
    return wireMap[w.index];
}

Status compileInto(const ir::Circuit& c, const std::vector<std::uint32_t>& wireMap,
                   std::vector<SimOp>& out, SimProgram& totals) {
    for (ir::NodeId id : c.topologicalOrder()) {
        const ir::Node& n = c.node(id);
        SimOp op;
        if (const auto* g = std::get_if<ir::Gate>(&n)) {
            QXL_TRY_ASSIGN(op.matrix, ir::baseMatrixOf(*g));
            for (ir::Wire w : g->targets) {
                QXL_TRY_ASSIGN(const std::uint32_t q, mapWire(w, wireMap));
                op.wires.push_back(q);
            }
            for (std::size_t j = 0; j < g->controls.size(); ++j) {
                QXL_TRY_ASSIGN(const std::uint32_t q, mapWire(g->controls[j], wireMap));
                op.controlMask |= std::size_t{1} << q;
                if (!g->isNegControl(j))
                    op.controlPattern |= std::size_t{1} << q;
            }
            ++totals.gates;
        } else if (const auto* m = std::get_if<ir::Measure>(&n)) {
            op.kind = SimOp::Kind::Measure;
            QXL_TRY_ASSIGN(const std::uint32_t q, mapWire(m->qubit, wireMap));
            op.wires = {q};
            op.bit = m->bit;
            totals.measures = true;
        } else if (const auto* r = std::get_if<ir::Reset>(&n)) {
            op.kind = SimOp::Kind::Reset;
            QXL_TRY_ASSIGN(const std::uint32_t q, mapWire(r->qubit, wireMap));
            op.wires = {q};
            totals.measures = true;
        } else if (const auto* co = std::get_if<ir::ClassicalOp>(&n)) {
            op.kind = SimOp::Kind::Classical;
            op.assign = co;
            totals.measures = true;
        } else if (const auto* br = std::get_if<ir::Branch>(&n)) {
            op.kind = SimOp::Kind::Branch;
            op.condition = &br->cond;
            QXL_TRY(compileInto(*br->thenBody, wireMap, op.first, totals));
            QXL_TRY(compileInto(*br->elseBody, wireMap, op.second, totals));
            totals.measures = true;
        } else if (const auto* lp = std::get_if<ir::Loop>(&n)) {
            op.kind = SimOp::Kind::Loop;
            op.condition = &lp->cond;
            op.maxIterations = lp->maxIterations;
            QXL_TRY(compileInto(*lp->body, wireMap, op.first, totals));
            totals.measures = true;
        } else if (const auto* bx = std::get_if<ir::Box>(&n)) {
            op.kind = SimOp::Kind::Block;
            QXL_TRY(compileInto(*bx->body, wireMap, op.first, totals));
        } else {
            continue; // barrier, delay: no action on the state
        }
        out.push_back(std::move(op));
    }
    return {};
}

std::uint64_t mix(std::uint64_t x) { // SplitMix64 finaliser
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

struct Runner {
    StateVec& state;
    SimRun& run;
    std::uint64_t planSeed;
    std::map<std::uint32_t, std::uint32_t> occurrence; // measurements seen per bit

    int take(std::uint32_t wire, int wanted) { // projects onto `wanted` unless it cannot happen
        const double p1 = state.probabilityOfOne(wire);
        if ((wanted ? p1 : 1.0 - p1) < kImpossible)
            wanted ^= 1;
        run.probability *= state.project(wire, wanted);
        return wanted;
    }
    // A discarded outcome is not addressable by a bit: both circuits take the more probable one.
    int likely(std::uint32_t wire) {
        return take(wire, state.probabilityOfOne(wire) > 0.5 + 1e-6 ? 1 : 0);
    }

    Status exec(const std::vector<SimOp>& ops) {
        for (const SimOp& op : ops) {
            switch (op.kind) {
            case SimOp::Kind::Gate:
                state.apply(op.matrix, op.wires, op.controlMask, op.controlPattern);
                break;
            case SimOp::Kind::Measure:
                if (op.bit == ir::kNoBit) {
                    likely(op.wires[0]);
                    break;
                }
                if (op.bit.index >= run.bits.size())
                    return fail(ErrorCode::OutOfRange,
                                "measurement writes a bit outside the circuit");
                run.bits[op.bit.index] = static_cast<std::uint8_t>(take(
                    op.wires[0], static_cast<int>(mix(planSeed ^ mix(op.bit.index + 1) ^
                                                      mix(0x5151 + occurrence[op.bit.index]++)) &
                                                  1u)));
                break;
            case SimOp::Kind::Reset:
                if (likely(op.wires[0]) == 1)
                    state.flip(op.wires[0]);
                break;
            case SimOp::Kind::Classical:
                ir::applyClassicalOp(*op.assign, run.bits);
                break;
            case SimOp::Kind::Branch:
                QXL_TRY(exec(op.condition->eval(run.bits) != 0 ? op.first : op.second));
                break;
            case SimOp::Kind::Loop:
                for (std::uint32_t it = 0;
                     it < std::min(op.maxIterations, kLoopCap) && op.condition->eval(run.bits) != 0;
                     ++it)
                    QXL_TRY(exec(op.first));
                break;
            case SimOp::Kind::Block:
                QXL_TRY(exec(op.first));
                break;
            }
        }
        return {};
    }
};
} // namespace

Result<SimProgram> compileForSimulation(const ir::Circuit& c,
                                        const std::vector<std::uint32_t>& wireMap) {
    SimProgram p;
    QXL_TRY(compileInto(c, wireMap, p.ops, p));
    return p;
}

Result<SimRun> execute(const SimProgram& p, StateVec& state, std::uint32_t clbits,
                       std::uint64_t planSeed) {
    SimRun run;
    run.bits.assign(clbits, 0);
    Runner r{state, run, planSeed, {}};
    QXL_TRY(r.exec(p.ops));
    return run;
}

} // namespace qlab::compiler::detail
