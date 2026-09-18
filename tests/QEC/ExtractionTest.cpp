// Spec 16 §2.1, §3; T09 §5.2–§5.3 — syndrome-extraction circuits: ancilla per stabilizer, CNOT
// count per round, the Tomita–Svore order read back from the ir::Circuit, four parallel CNOT
// layers, bit layout, detector definitions, QASM export and the schedule ↔ circuit round trip.
#include "IR/IR.hpp"
#include "Lang/Sema.hpp"
#include "QEC/Extraction.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <map>
#include <set>

using namespace qlab;
using namespace qlab::qec;

namespace {
StabilizerCode shipped(const std::string& id) { return loadShippedCode(id).value(); }

// Compass direction of a data qubit seen from its ancilla, computed from the coordinates only.
std::string direction(Coord2 anc, Coord2 data) {
    return std::string(data.y > anc.y ? "N" : "S") + (data.x < anc.x ? "W" : "E");
}
} // namespace

TEST_CASE("Extraction: surface_rot_3 has 8 ancillas, 24 CNOTs per round and the spec 16 §3 bit layout") {
    const StabilizerCode code = shipped("surface_rot_3");
    for (std::uint32_t rounds : {1u, 3u}) {
        ExtractionOptions opt;
        opt.rounds = rounds;
        const auto circuit = buildMemoryExperiment(code, opt);
        REQUIRE(circuit.has_value());
        REQUIRE(ir::verify(*circuit).has_value());
        REQUIRE(circuit->qubitCount() == 17);                 // 2d² − 1 (spec 16 §2.1)
        REQUIRE(circuit->qubitRegisters().at(1).name == "anc");
        REQUIRE(circuit->qubitRegisters().at(1).size == 8);   // one ancilla per stabilizer
        REQUIRE(circuit->clbitCount() == 8 * rounds + 9);     // bit[n_anc · rounds + n_data]
        const auto counts = circuit->gateCounts();
        // Four weight-4 and four weight-2 checks: 4·4 + 4·2 = 24 CNOTs per round.
        REQUIRE(counts.at("cx") == 24 * rounds);
        REQUIRE(counts.at("h") == 2 * 4 * rounds);            // before and after, X ancillas only
        REQUIRE(counts.at("reset") == 8 * rounds + 9);
        REQUIRE(counts.at("measure") == 8 * rounds + 9);
        REQUIRE(circuit->twoQubitCount() == 24 * rounds);
        REQUIRE(circuit->meta()["interactionHint"]["data"].size() == 9);
        REQUIRE(circuit->meta()["interactionHint"]["ancilla"].size() == 8);
        REQUIRE(circuit->meta()["qec"]["code"] == "surface_rot_3");
    }
    // One round alone: reset, h, four CNOT layers, h, measure (T09 §5.2 "depth 6" plus reset and measure).
    const auto round = buildSyndromeRound(code);
    REQUIRE(round.has_value());
    REQUIRE(round->depth() == 8);
    REQUIRE(round->clbitCount() == 8);
}

TEST_CASE("Extraction: CNOTs follow the Tomita–Svore order on every ancilla wire") {
    for (const char* id : {"surface_rot_3", "surface_rot_5"}) {
        const StabilizerCode code = shipped(id);
        const auto circuit = buildSyndromeRound(code);
        REQUIRE(circuit.has_value());
        for (std::uint32_t j = 0; j < code.checkCount(); ++j) {
            const std::uint32_t wire = code.n + j;
            const bool xType = code.checkType(j) == CheckType::X;
            std::vector<std::string> seen;
            for (ir::NodeId node : circuit->onWire(ir::Wire{wire})) {
                const auto* g = std::get_if<ir::Gate>(&circuit->node(node));
                if (!g || g->name != "cx") continue;
                // X check: the ancilla controls (after h). Z check: the ancilla is the target.
                REQUIRE(g->targets[xType ? 0 : 1].index == wire);
                const std::uint32_t data = g->targets[xType ? 1 : 0].index;
                REQUIRE(data < code.n);
                seen.push_back(direction(code.ancillas[j].coord, code.dataLayout[data]));
            }
            const std::vector<std::string> pattern = xType ? std::vector<std::string>{"NW", "NE", "SW", "SE"}
                                                           : std::vector<std::string>{"NW", "SW", "NE", "SE"};
            // Boundary checks keep the pattern restricted to the neighbours that exist.
            std::vector<std::string> expected;
            for (const auto& dir : pattern)
                if (std::find(seen.begin(), seen.end(), dir) != seen.end()) expected.push_back(dir);
            INFO(id << " ancilla " << j);
            REQUIRE(seen == expected);
            REQUIRE(seen.size() == code.stabilizers[j].weight());
        }
    }
}

TEST_CASE("Extraction: the four CNOT layers are parallel and hook pairs are perpendicular to the logicals") {
    const StabilizerCode code = shipped("surface_rot_5");
    const auto ex = planMemoryExperiment(code, {});
    REQUIRE(ex.has_value());
    // Moments between markers: a CNOT moment must not touch a qubit twice, and a round has four.
    std::vector<std::vector<Op>> cnotMoments(1);
    for (const Op& op : ex->schedule.ops) {
        if (op.marker()) { if (!cnotMoments.back().empty()) cnotMoments.emplace_back(); continue; }
        if (op.kind == OpKind::CX) cnotMoments.back().push_back(op);
    }
    if (cnotMoments.back().empty()) cnotMoments.pop_back();
    REQUIRE(cnotMoments.size() == 4);
    std::size_t total = 0;
    for (const auto& moment : cnotMoments) {
        std::set<std::uint32_t> touched;
        for (const Op& op : moment) {
            REQUIRE(touched.insert(op.a).second);
            REQUIRE(touched.insert(op.b).second);
        }
        total += moment.size();
    }
    REQUIRE(total == 16 * 4 + 8 * 2);   // d = 5: sixteen weight-4 and eight weight-2 checks
    // Hook errors (T09 §5.2): the last two data qubits of an X check share a row (same y), so the
    // X pair is perpendicular to X̄ (a column); the last two of a Z check share a column (same x),
    // perpendicular to Z̄ (a row).
    const auto xbar = code.logicalX[0].support(), zbar = code.logicalZ[0].support();
    for (std::size_t i = 1; i < xbar.size(); ++i) REQUIRE(code.dataLayout[xbar[i]].x == code.dataLayout[xbar[0]].x);
    for (std::size_t i = 1; i < zbar.size(); ++i) REQUIRE(code.dataLayout[zbar[i]].y == code.dataLayout[zbar[0]].y);
    for (std::uint32_t j = 0; j < code.checkCount(); ++j) {
        const auto& order = code.ancillas[j].order;
        if (order.size() != 4) continue;
        const Coord2 a = code.dataLayout[order[2]], b = code.dataLayout[order[3]];
        if (code.checkType(j) == CheckType::X) REQUIRE(a.y == b.y);
        else REQUIRE(a.x == b.x);
    }
}

TEST_CASE("Extraction: detector lattice and observables") {
    struct Row { const char* id; LogicalBasis basis; std::uint32_t rounds; std::size_t detectors; std::size_t observableBits; };
    const Row rows[] = {
        // Surface d = 3, Z memory: 4 Z checks on layers 0..R, 4 X checks on layers 1..R−1.
        {"surface_rot_3", LogicalBasis::Z, 3, 4 * 4 + 4 * 2, 3},
        {"surface_rot_3", LogicalBasis::X, 3, 4 * 4 + 4 * 2, 3},
        {"surface_rot_3", LogicalBasis::Z, 1, 4 * 2, 3},
        {"repetition_bitflip_3", LogicalBasis::Z, 4, 2 * 5, 1},
        // Shor: Z̄ = X⊗9, so the two X checks are fixed by the |+⟩⊗9 preparation and closed by the
        // X readout; the six Z checks only get round differences.
        {"shor_9", LogicalBasis::Z, 3, 2 * 4 + 6 * 2, 9},
        // Mixed checks: fixed from round 0 by the explicit encoder, never closed by a transversal readout.
        {"five_qubit", LogicalBasis::Z, 3, 4 * 3, 5}};
    for (const Row& r : rows) {
        INFO(r.id << " rounds " << r.rounds);
        ExtractionOptions opt;
        opt.rounds = r.rounds;
        opt.basis = r.basis;
        const auto ex = planMemoryExperiment(shipped(r.id), opt);
        REQUIRE(ex.has_value());
        REQUIRE(ex->detectors.size() == r.detectors);
        REQUIRE(ex->observables.size() == 1);
        REQUIRE(ex->observables[0].size() == r.observableBits);
        REQUIRE(ex->layers() == r.rounds + 1);
        for (const Detector& d : ex->detectors) REQUIRE(ex->detectorAt(d.check, d.layer) != kNoIndex);
    }
    // T09 (5.2): a bulk detector is the XOR of one check in consecutive rounds; the closing layer adds
    // the data readout of the check's support.
    ExtractionOptions opt;
    opt.rounds = 3;
    const StabilizerCode s3 = shipped("surface_rot_3");
    const auto ex = planMemoryExperiment(s3, opt).value();
    const Detector& mid = ex.detectors[ex.detectorAt(2, 1)];   // generator 2 = IZZIZZIII
    REQUIRE(mid.bits == std::vector<std::uint32_t>{ex.syndromeBit(0, 2), ex.syndromeBit(1, 2)});
    REQUIRE(ex.syndromeBit(1, 2) == 8 + 2);
    const Detector& last = ex.detectors[ex.detectorAt(2, 3)];
    REQUIRE(last.bits == std::vector<std::uint32_t>{ex.syndromeBit(2, 2), ex.dataBit(1), ex.dataBit(2), ex.dataBit(4), ex.dataBit(5)});
    REQUIRE(ex.dataBit(0) == 24);
    REQUIRE(ex.detectorAt(1, 0) == kNoIndex);   // X check in a Z memory: random in round 0
    REQUIRE(ex.detectorAt(1, 3) == kNoIndex);
    REQUIRE(ex.observables[0] == std::vector<std::uint32_t>{ex.dataBit(2), ex.dataBit(5), ex.dataBit(8)});
    // Events are parities of the record.
    std::vector<std::uint8_t> bits(ex.schedule.bits, 0);
    bits[ex.syndromeBit(1, 2)] = 1;
    const auto events = ex.detectionEvents(bits);
    REQUIRE(events[ex.detectorAt(2, 1)] == 1);
    REQUIRE(events[ex.detectorAt(2, 2)] == 1);
    REQUIRE(std::count(events.begin(), events.end(), 1) == 2);
    REQUIRE(planMemoryExperiment(s3, ExtractionOptions{.rounds = 0}).error().code == err::BadOptions);
}

TEST_CASE("Extraction: circuits export to OpenQASM 3 and come back as the same schedule") {
    for (const char* id : {"repetition_bitflip_3", "steane_7", "five_qubit", "surface_rot_3"}) {
        INFO(id);
        ExtractionOptions opt;
        opt.rounds = 2;
        const auto ex = planMemoryExperiment(shipped(id), opt).value();
        const auto circuit = toCircuit(ex);
        REQUIRE(circuit.has_value());
        REQUIRE(ir::verify(*circuit).has_value());
        // Schedule → circuit → schedule: identical up to the marker kind.
        const auto back = compileClifford(*circuit);
        REQUIRE(back.has_value());
        std::vector<Op> expected = ex.schedule.ops;
        for (Op& op : expected)
            if (op.kind == OpKind::RoundStart) op = Op{OpKind::Tick};
        REQUIRE(back->ops == expected);
        REQUIRE(back->qubits == ex.schedule.qubits);
        REQUIRE(back->bits == ex.schedule.bits);
        // Real OpenQASM 3 (spec 16 §3): the text parses and rebuilds to the same dump.
        const auto qasm = ir::toQasm(*circuit);
        REQUIRE(qasm.has_value());
        REQUIRE(qasm->find("qubit[") != std::string::npos);
        auto program = lang::parseProgram(*qasm, "memory.qasm");
        REQUIRE(program.has_value());
        const auto rebuilt = ir::buildCircuit(*program);
        REQUIRE(rebuilt.has_value());
        REQUIRE(ir::dump(*rebuilt) == ir::dump(*circuit));
    }
    // A non-Clifford gate is refused by name.
    ir::Circuit t;
    t.setQubitCount(1);
    t.add(*ir::makeGate("t", {ir::Wire{0}}));
    const auto bad = compileClifford(t);
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(bad.error().code == err::NotClifford);
    REQUIRE(bad.error().message.find("'t'") != std::string::npos);
}
