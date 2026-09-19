// Spec 15 §2–§3 — analysis of a compiled circuit: used qubits, measurement map, classical layout,
// execution model, and the schedule quantities the noise model and the estimator consume.
#include "Runtime/Plan.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <unordered_map>

namespace qlab::runtime {
namespace {

// Spec 15 §2 "Clifford + measurement/reset": the stabilizer-admissible gate names, plus the
// rotations whose angle is a multiple of π/2. Conservative — a false negative only costs a
// larger backend.
bool cliffordGate(const ir::Gate& g) {
    static const std::set<std::string, std::less<>> kNames{
        "i",  "id", "x",  "y",    "z",   "h",     "s",  "sdg",  "sx",     "sxdg",
        "cx", "cy", "cz", "swap", "ecr", "iswap", "sy", "sydg", "barrier"};
    if (!g.controls.empty()) { // ctrl @ Clifford is not Clifford in general (ccx is not)
        if (g.controls.size() != 1)
            return false;
        if (g.name != "x" && g.name != "y" && g.name != "z")
            return false;
    }
    if (kNames.contains(g.name))
        return true;
    if (g.params.size() == 1 &&
        (g.name == "rz" || g.name == "rx" || g.name == "ry" || g.name == "p")) {
        const double k = g.params[0] / (std::numbers::pi / 2.0);
        return std::abs(k - std::round(k)) < 1e-9;
    }
    return false;
}

struct Walker {
    ProgramPlan* plan = nullptr;
    void visit(const ir::Circuit& c, bool nested) {
        for (ir::NodeId id : c.topologicalOrder()) {
            const ir::Node& n = c.node(id);
            if (const auto* g = std::get_if<ir::Gate>(&n)) {
                ++plan->gateCount;
                if (!cliffordGate(*g))
                    plan->cliffordOnly = false;
            } else if (const auto* m = std::get_if<ir::Measure>(&n)) {
                if (nested)
                    plan->terminalMeasurementOnly = false;
                (void)m;
            } else if (std::holds_alternative<ir::Reset>(n)) {
                plan->hasReset = true;
                plan->terminalMeasurementOnly = false;
            } else if (std::holds_alternative<ir::ClassicalOp>(n)) {
                plan->terminalMeasurementOnly = false;
            } else if (const auto* b = std::get_if<ir::Branch>(&n)) {
                plan->hasFeedforward = true;
                plan->terminalMeasurementOnly = false;
                ++plan->branchCount;
                if (b->thenBody.present())
                    visit(*b->thenBody, true);
                if (b->elseBody.present())
                    visit(*b->elseBody, true);
            } else if (const auto* l = std::get_if<ir::Loop>(&n)) {
                plan->hasFeedforward = true;
                plan->hasLoop = true;
                plan->terminalMeasurementOnly = false;
                ++plan->branchCount;
                if (l->body.present())
                    visit(*l->body, true);
            } else if (const auto* bx = std::get_if<ir::Box>(&n)) {
                if (bx->body.present())
                    visit(*bx->body, true);
            }
        }
    }
};

// Every node after `at` on `w` is a directive (barrier/delay): the measurement is terminal there.
bool lastOnWire(const ir::Circuit& c, ir::Wire w, ir::NodeId at) {
    bool seen = false;
    for (ir::NodeId id : c.onWire(w)) {
        if (id == at) {
            seen = true;
            continue;
        }
        if (!seen)
            continue;
        const ir::Node& n = c.node(id);
        if (!std::holds_alternative<ir::Barrier>(n) && !std::holds_alternative<ir::Delay>(n))
            return false;
    }
    return true;
}
} // namespace

std::vector<QubitIndex> ProgramPlan::sim(std::span<const ir::Wire> wires) const {
    std::vector<QubitIndex> out;
    out.reserve(wires.size());
    for (ir::Wire w : wires) {
        const std::int32_t s = w.index < toSim.size() ? toSim[w.index] : -1;
        out.push_back(QubitIndex{static_cast<std::uint32_t>(s < 0 ? 0 : s)});
    }
    return out;
}

std::vector<std::uint32_t> usedQubits(const ir::Circuit& c) {
    std::set<std::uint32_t> used;
    const auto collect = [&](const ir::Circuit& body, auto&& self) -> void {
        for (ir::NodeId id : body.topologicalOrder()) {
            const ir::Node& n = body.node(id);
            if (std::holds_alternative<ir::Barrier>(n) || std::holds_alternative<ir::Delay>(n))
                continue;
            for (ir::Wire w : ir::nodeWires(n))
                used.insert(w.index);
            if (const auto* b = std::get_if<ir::Branch>(&n)) {
                if (b->thenBody.present())
                    self(*b->thenBody, self);
                if (b->elseBody.present())
                    self(*b->elseBody, self);
            } else if (const auto* l = std::get_if<ir::Loop>(&n)) {
                if (l->body.present())
                    self(*l->body, self);
            } else if (const auto* bx = std::get_if<ir::Box>(&n)) {
                if (bx->body.present())
                    self(*bx->body, self);
            }
        }
    };
    collect(c, collect);
    return {used.begin(), used.end()};
}

Result<ProgramPlan> planProgram(const ir::Circuit& c, std::span<const std::string> outputs) {
    ProgramPlan plan;
    plan.qubits = usedQubits(c);
    if (plan.qubits.empty() && c.qubitCount() > 0)
        plan.qubits.push_back(0); // a program with no quantum op
    plan.toSim.assign(std::max<std::size_t>(c.qubitCount(), 1), -1);
    for (std::size_t s = 0; s < plan.qubits.size(); ++s) {
        if (plan.qubits[s] >= plan.toSim.size())
            plan.toSim.resize(plan.qubits[s] + 1, -1);
        plan.toSim[plan.qubits[s]] = static_cast<std::int32_t>(s);
    }

    plan.layout.bits = c.clbitCount();
    for (const ir::BitRegister& r : c.bitRegisters()) {
        RegisterInfo info{r.name, r.first, r.size, r.kind, r.scalar, false};
        info.output = std::find(outputs.begin(), outputs.end(), r.name) != outputs.end();
        plan.layout.registers.push_back(std::move(info));
    }
    plan.depth = static_cast<std::uint32_t>(c.depth());

    Walker w{&plan};
    w.visit(c, false);

    // Top-level measurements: order of appearance, and whether each is the last op on its wire.
    std::vector<std::uint8_t> seen(plan.toSim.size(), 0);
    for (ir::NodeId id : c.topologicalOrder()) {
        const auto* m = std::get_if<ir::Measure>(&c.node(id));
        if (!m)
            continue;
        const std::int32_t s = m->qubit.index < plan.toSim.size() ? plan.toSim[m->qubit.index] : -1;
        if (s < 0)
            return fail(err::Unsupported, "measurement on a wire outside the circuit");
        plan.measurements.push_back(
            MeasuredBit{static_cast<std::uint32_t>(s), m->qubit.index, m->bit});
        if (!seen[m->qubit.index]) {
            seen[m->qubit.index] = 1;
            plan.measuredQubits.push_back(static_cast<std::uint32_t>(s));
        }
        if (!lastOnWire(c, m->qubit, id)) {
            plan.terminalMeasurementOnly = false;
            plan.hasMidCircuitMeasure = true;
        }
    }
    if (plan.hasFeedforward)
        plan.hasMidCircuitMeasure = plan.hasMidCircuitMeasure || !plan.measurements.empty();
    return plan;
}

Picoseconds circuitCriticalPath(const ir::Circuit& c, const compiler::ScheduleInfo& timing) {
    const auto order = c.topologicalOrder();
    if (timing.nodes.size() != order.size())
        return timing.duration;
    // T12 (1.2): the latest end over all NON-measurement operations. Mid-circuit measurements stay
    // on the path through the start times of the nodes that follow them.
    std::int64_t end = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (std::holds_alternative<ir::Measure>(c.node(order[i])))
            continue;
        end = std::max(end, timing.nodes[i].end().get());
    }
    return Picoseconds{end};
}

std::vector<IdleGap> idleGaps(const ir::Circuit& c, const compiler::ScheduleInfo& timing) {
    std::vector<IdleGap> gaps;
    const auto order = c.topologicalOrder();
    if (timing.nodes.size() != order.size())
        return gaps;
    std::unordered_map<std::uint32_t, std::size_t> index;
    for (std::size_t i = 0; i < order.size(); ++i)
        index[order[i].get()] = i;
    for (std::uint32_t q = 0; q < c.qubitCount(); ++q) {
        std::int64_t last = -1;
        for (ir::NodeId id : c.onWire(ir::Wire{q})) {
            auto it = index.find(id.get());
            if (it == index.end())
                continue;
            const compiler::TimedNode& t = timing.nodes[it->second];
            if (last >= 0 && t.start.get() > last)
                gaps.push_back(IdleGap{q, it->second, Picoseconds{t.start.get() - last}});
            last = std::max(last, t.end().get());
        }
    }
    std::sort(gaps.begin(), gaps.end(), [](const IdleGap& a, const IdleGap& b) {
        return a.nodeIndex == b.nodeIndex ? a.wire < b.wire : a.nodeIndex < b.nodeIndex;
    });
    return gaps;
}

} // namespace qlab::runtime
