// Spec 16 §8 — visualization data: roles and coordinates per physical qubit, CNOT-order arrows,
// syndrome / matched pairs / correction of a decode, the space-time syndrome lattice, chip binding.
#include "QEC/QEC.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using namespace qlab;
using namespace qlab::qec;

namespace {
StabilizerCode shipped(const std::string& id) { return loadShippedCode(id).value(); }
} // namespace

TEST_CASE("View: roles, coordinates and gate arrows of the d = 3 surface code") {
    const StabilizerCode code = shipped("surface_rot_3");
    const CodeView view = makeCodeView(code);
    REQUIRE(view.qubits.size() == 17);
    auto count = [&](QubitRole r) { return std::count_if(view.qubits.begin(), view.qubits.end(), [r](const RoleEntry& e) { return e.role == r; }); };
    REQUIRE(count(QubitRole::Data) == 9);
    REQUIRE(count(QubitRole::AncillaX) == 4);
    REQUIRE(count(QubitRole::AncillaZ) == 4);
    REQUIRE(view.qubits[4].coord == Coord2{3, 3});
    REQUIRE(view.qubits[9].role == QubitRole::AncillaZ);      // generator 0 = Z0 Z1 at (0, 2)
    REQUIRE(view.qubits[9].check == 0);
    REQUIRE(view.qubits[9].circuitQubit == 9);
    REQUIRE(view.qubits[10].coord == Coord2{2, 2});
    REQUIRE(roleName(view.qubits[10].role) == "ancilla_x");
    REQUIRE(view.arrows.size() == 24);                         // one per CNOT of a round
    for (const GateArrow& a : view.arrows) {
        const bool xCheck = code.checkType(a.check) == CheckType::X;
        REQUIRE(a.ancillaControls == xCheck);                  // X: ancilla → data, Z: data → ancilla
        REQUIRE((xCheck ? a.from : a.to) == code.ancillas[a.check].coord);
        REQUIRE(a.step < 4);
        REQUIRE(int(a.step) == tomitaSvoreLayer(code.checkType(a.check), code.ancillas[a.check].coord, code.dataLayout[a.dataQubit]));
    }
    REQUIRE(view.logicalX == std::vector<std::vector<std::uint32_t>>{{0, 1, 2}});
    REQUIRE(view.logicalZ == std::vector<std::vector<std::uint32_t>>{{2, 5, 8}});
    // Codes without a lattice number their gates in ancilla order; mixed checks keep their letters.
    const CodeView five = makeCodeView(shipped("five_qubit"));
    REQUIRE(five.qubits.back().role == QubitRole::AncillaMixed);
    REQUIRE(five.arrows[1].step == 1);
    REQUIRE(five.arrows[1].letter == 'Z');                     // XZZXI on qubit 1
}

TEST_CASE("View: a decode shows its syndrome, matched pair and correction") {
    const StabilizerCode code = shipped("surface_rot_3");
    CodeView view = makeCodeView(code);
    auto decoder = makeCodeCapacityDecoder("union_find", code).value();
    const auto syndrome = syndromeOf(code.stabilizers, PauliString::single(9, 4, 'X'));
    const Correction c = decoder->decode(SyndromeLattice::fromSyndrome(syndrome)).value();
    showSyndrome(view, syndrome, c);
    REQUIRE(view.syndrome == std::vector<std::uint8_t>{0, 0, 1, 0, 0, 1, 0, 0});   // Z checks 2 and 5
    REQUIRE(view.matched.size() == 1);
    REQUIRE(view.matched[0].a == 2);
    REQUIRE(view.matched[0].b == 5);
    REQUIRE(view.correction[4] == 'X');
    REQUIRE(std::count(view.correction.begin(), view.correction.end(), 'I') == 8);
    // Chip binding: circuit wire w sits on physical qubit layout[w].
    std::vector<std::uint32_t> layout(17);
    for (std::uint32_t w = 0; w < 17; ++w) layout[w] = 53 - w;
    REQUIRE(bindToChip(view, layout).has_value());
    REQUIRE(view.qubits[0].physical == QubitIndex{53});
    REQUIRE(view.qubits[16].physical == QubitIndex{37});
    REQUIRE(bindToChip(view, std::vector<std::uint32_t>(5, 0)).error().code == err::BadLayout);
}

TEST_CASE("View: space-time lattice of a decoded shot") {
    const StabilizerCode code = shipped("surface_rot_3");
    ExtractionOptions x;
    x.rounds = 3;
    NoiseParams n;
    n.p = 1e-3;
    const auto runner = ExperimentRunner::create(code, x, n, "union_find").value();
    auto worker = runner.worker();
    core::Random rng(8);
    // A flipped syndrome bit of check 5 in round 1 and an X on data qubit 4 before round 2.
    FaultEvent x4;
    for (std::uint32_t i = 0; i < runner.experiment().schedule.ops.size(); ++i)
        if (runner.experiment().schedule.ops[i].kind == OpKind::RoundStart && runner.experiment().schedule.ops[i].a == 2) x4.afterOp = i;
    x4.qubitA = 4;
    x4.pauliA = 'X';
    const ShotResult shot = worker.shotWithFaults({x4}, rng, SampleEngine::Stabilizer).value();
    REQUIRE_FALSE(shot.failure);
    const SpaceTimeLattice lattice = makeSpaceTimeLattice(code, runner.experiment(), shot);
    REQUIRE(lattice.checks == 8);
    REQUIRE(lattice.layers == 4);
    REQUIRE(lattice.points.size() == runner.experiment().detectors.size());
    REQUIRE(lattice.events == 2);
    REQUIRE(lattice.matched.size() == 1);
    const LatticePoint& a = lattice.points[lattice.matched[0].a];
    const LatticePoint& b = lattice.points[lattice.matched[0].b];
    REQUIRE(a.fired);
    REQUIRE(b.fired);
    REQUIRE(a.layer == 2);
    REQUIRE(b.layer == 2);
    REQUIRE(a.coord == Coord2{2, 4});   // Z checks 2 and 5 around the centre qubit
    REQUIRE(b.coord == Coord2{4, 2});
    REQUIRE(lattice.cls == FidelityClass::Statistical);
    CodeView view = makeCodeView(code);
    showShot(view, runner.experiment(), shot, 2);
    // Both Z checks flip in round 2 relative to round 1 (the raw bits themselves are fixed at 0 here:
    // Z checks of |0̄⟩ read 0 until the error arrives).
    REQUIRE(view.syndrome[2] == 1);
    REQUIRE(view.syndrome[5] == 1);
    REQUIRE(view.matched.size() == 1);
    REQUIRE(view.correction[4] == 'X');
    // A measurement fault is a time-like edge: one check, two layers, no pair on the code lattice.
    FaultEvent flip;
    flip.afterOp = static_cast<std::uint32_t>(runner.experiment().schedule.ops.size() - 1);
    flip.flipBit = runner.experiment().syndromeBit(1, 5);
    const ShotResult timeShot = worker.shotWithFaults({flip}, rng, SampleEngine::Stabilizer).value();
    const SpaceTimeLattice timeLattice = makeSpaceTimeLattice(code, runner.experiment(), timeShot);
    REQUIRE(timeLattice.matched.size() == 1);
    REQUIRE(timeLattice.points[timeLattice.matched[0].a].check == 5);
    REQUIRE(timeLattice.points[timeLattice.matched[0].b].check == 5);
    REQUIRE(timeLattice.points[timeLattice.matched[0].b].layer == timeLattice.points[timeLattice.matched[0].a].layer + 1);
    showShot(view, runner.experiment(), timeShot, 1);
    REQUIRE(view.matched.empty());
    REQUIRE(std::count(view.correction.begin(), view.correction.end(), 'I') == 9);
}
