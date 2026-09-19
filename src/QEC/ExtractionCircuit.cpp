// Spec 16 §3 — lowering a schedule to an ordinary `ir::Circuit` on virtual qubits (registers, bit
// layout, interaction hint) and reading a Clifford `ir::Circuit` back into a schedule.
#include "QEC/Extraction.hpp"
#include <format>

namespace qlab::qec {
namespace {
using core::Json;

Status appendOps(ir::Circuit& c, const Schedule& s) {
    for (const Op& op : s.ops) {
        switch (op.kind) {
        case OpKind::Tick:
        case OpKind::RoundStart:
            c.add(ir::Barrier{{}, {}});
            break;
        case OpKind::Reset:
            c.add(ir::Reset{ir::Wire{op.a}, std::nullopt, {}});
            break;
        case OpKind::Measure:
            c.add(ir::Measure{ir::Wire{op.a},
                              op.bit == kNoIndex ? ir::kNoBit : ir::ClassicalBit{op.bit},
                              std::nullopt,
                              {}});
            break;
        default: {
            std::vector<ir::Wire> wires = {ir::Wire{op.a}};
            if (op.twoQubit())
                wires.push_back(ir::Wire{op.b});
            QXL_TRY_ASSIGN(ir::Gate g, ir::makeGate(opName(op.kind), std::move(wires)));
            c.add(std::move(g));
        }
        }
    }
    return {};
}

Json coordArray(std::span<const Coord2> coords) {
    Json a = Json::array();
    for (const Coord2& xy : coords)
        a.push_back(Json::array({xy.x, xy.y}));
    return a;
}

std::unexpected<Error> notClifford(std::string_view what) {
    return fail(
        err::NotClifford,
        std::format("{} is outside the QEC gate set (id, x, y, z, h, s, sdg, cx, cy, cz, swap, "
                    "measure, reset, barrier)",
                    what));
}

Result<OpKind> kindOf(const ir::Gate& g) {
    static constexpr std::pair<std::string_view, OpKind> table[] = {
        {"h", OpKind::H},   {"s", OpKind::S},   {"sdg", OpKind::Sdg},  {"x", OpKind::X},
        {"y", OpKind::Y},   {"z", OpKind::Z},   {"cx", OpKind::CX},    {"CX", OpKind::CX},
        {"cy", OpKind::CY}, {"cz", OpKind::CZ}, {"swap", OpKind::Swap}};
    if (!g.controls.empty() || g.adjoint || g.opaque || g.custom)
        return notClifford(std::format("modified gate '{}'", g.name));
    for (const auto& [name, kind] : table)
        if (g.name == name)
            return kind;
    return notClifford(std::format("gate '{}'", g.name));
}

} // namespace

Result<ir::Circuit> toCircuit(const MemoryExperiment& ex) {
    ir::Circuit c;
    c.setQubitCount(ex.schedule.qubits);
    c.setClbitCount(ex.schedule.bits);
    c.addQubitRegister(ir::QubitRegister{"data", 0, ex.nData, false});
    c.addQubitRegister(ir::QubitRegister{"anc", ex.nData, ex.nAncilla, false});
    c.addBitRegister(ir::BitRegister{"c", 0, ex.schedule.bits, ir::RegKind::Bit, false});
    QXL_TRY(appendOps(c, ex.schedule));

    const std::span<const Coord2> coords(ex.qubitCoords);
    const std::size_t nData = std::min<std::size_t>(ex.nData, coords.size());
    c.meta()["interactionHint"] = {{"data", coordArray(coords.first(nData))},
                                   {"ancilla", coordArray(coords.subspan(nData))}};
    Json detectors = Json::array();
    for (const Detector& d : ex.detectors)
        detectors.push_back({{"check", d.check},
                             {"layer", d.layer},
                             {"type", std::string(checkTypeName(d.type))},
                             {"bits", d.bits}});
    c.meta()["qec"] = {{"code", ex.codeId},
                       {"rounds", ex.options.rounds},
                       {"basis", std::string(basisName(ex.options.basis))},
                       {"n_data", ex.nData},
                       {"n_ancilla", ex.nAncilla},
                       {"syndrome_bit", "round * n_ancilla + check"},
                       {"data_bit", "rounds * n_ancilla + qubit"},
                       {"detectors", std::move(detectors)},
                       {"observables", ex.observables}};
    return c;
}

Result<ir::Circuit> buildMemoryExperiment(const StabilizerCode& code,
                                          const ExtractionOptions& options) {
    QXL_TRY_ASSIGN(const MemoryExperiment ex, planMemoryExperiment(code, options));
    return toCircuit(ex);
}

Result<ir::Circuit> buildSyndromeRound(const StabilizerCode& code) {
    ExtractionOptions o;
    o.rounds = 1;
    o.finalDataMeasurement = false;
    o.includeLogicalPrep = false;
    return buildMemoryExperiment(code, o);
}

Result<Schedule> compileClifford(const ir::Circuit& circuit) {
    Schedule s;
    s.qubits = circuit.qubitCount();
    s.bits = circuit.clbitCount();
    for (ir::NodeId id : circuit.topologicalOrder()) {
        const ir::Node& node = circuit.node(id);
        if (const auto* g = std::get_if<ir::Gate>(&node)) {
            if (g->name == "id")
                continue;
            QXL_TRY_ASSIGN(const OpKind kind, kindOf(*g));
            Op op{kind};
            if (g->targets.size() != (op.twoQubit() ? 2u : 1u))
                return notClifford(
                    std::format("gate '{}' with {} operands", g->name, g->targets.size()));
            op.a = g->targets[0].index;
            if (op.twoQubit())
                op.b = g->targets[1].index;
            s.ops.push_back(op);
        } else if (const auto* m = std::get_if<ir::Measure>(&node)) {
            s.ops.push_back(
                {OpKind::Measure, m->qubit.index, 0, m->discards() ? kNoIndex : m->bit.index});
        } else if (const auto* r = std::get_if<ir::Reset>(&node)) {
            s.ops.push_back({OpKind::Reset, r->qubit.index});
        } else if (const auto* b = std::get_if<ir::Barrier>(&node)) {
            if (b->wires.empty() || b->wires.size() == circuit.qubitCount())
                s.ops.push_back({OpKind::Tick});
        } else if (std::holds_alternative<ir::Delay>(node)) {
            continue; // timing only
        } else {
            return notClifford(std::format("node '{}'", ir::nodeKindName(node)));
        }
    }
    return s;
}

} // namespace qlab::qec
