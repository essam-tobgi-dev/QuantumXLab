// Spec 14 §5, 25 §4 — optimization never changes the unitary (up to global phase), strictly
// reduces the gate count on crafted inputs, never crosses a blocker, and is idempotent.
#include "CompilerTestUtil.hpp"
#include <cmath>

using namespace ctest;
using compiler::Basis1q;

namespace {
// Physical single-qubit pulses: every single-qubit gate that is not a frame change (spec 14 §5.4).
std::size_t pulses(const ir::Circuit& c) {
    std::size_t p = 0;
    for (const auto* g : gatesOf(c))
        if (g->targets.size() == 1 && g->controls.empty() && !compiler::isFrameChange(g->name)) ++p;
    return p;
}
// Optimizes a copy, checks the unitary (tolerance 1e-9) and the verifier, returns the result.
ir::Circuit optimized(const ir::Circuit& source, Basis1q basis, const compiler::OptimizeOptions& o = {}) {
    ir::Circuit c = source;
    auto st = compiler::optimize(c, basis, o);
    INFO((st ? std::string() : st.error().format()));
    REQUIRE(st.has_value());
    REQUIRE(ir::verify(c).has_value());
    if (source.isPureUnitary()) REQUIRE(num::equalUpToGlobalPhase(unitary(source), unitary(c, source.qubitCount()), 1e-9));
    // The cost order of §5.2 is (pulses, gates): the gate count may grow only when a pulse is saved.
    CHECK(pulses(c) <= pulses(source));
    CHECK((pulses(c) < pulses(source) || st->gatesAfter <= st->gatesBefore));
    CHECK(c.twoQubitCount() <= source.twoQubitCount());
    return c;
}
std::size_t gates(const ir::Circuit& c) { return gatesOf(c).size(); }
} // namespace

TEST_CASE("§5.1 adjacent inverse pairs vanish, nested pairs included") {
    const std::vector<std::vector<ir::Gate>> vanish = {
        {G("x", {0}), G("x", {0})},
        {G("h", {1}), G("h", {1})},
        {G("s", {0}), G("sdg", {0})},
        {G("t", {0}), G("tdg", {0})},
        {G("sx", {0}), G("sxdg", {0})},
        {G("rz", {0}, {0.7}), G("rz", {0}, {-0.7})},
        {G("cx", {0, 1}), G("cx", {0, 1})},
        {G("cz", {0, 1}), G("cz", {1, 0})},                       // cz is operand symmetric
        {G("swap", {0, 1}), G("swap", {1, 0})},
        {G("ecr", {0, 1}), G("ecr", {0, 1})},
        {G("ccx", {0, 1, 2}), G("ccx", {0, 1, 2})},
        {G("U", {0}, {0.3, 0.2, 0.1}), G("U", {0}, {-0.3, -0.1, -0.2})},
        {G("h", {0}), G("cx", {0, 1}), G("t", {1}), G("tdg", {1}), G("cx", {0, 1}), G("h", {0})},   // to fixpoint
        {controlled(G("ry", {0}, {0.4}), {1}, {1}), controlled(G("ry", {0}, {-0.4}), {1}, {1})},
    };
    for (const auto& seq : vanish) {
        INFO(seq.front().name << " … " << seq.back().name);
        CHECK(gates(optimized(circuit(3, seq), Basis1q::ZSX)) == 0);
    }
    // Not inverses: different operand order, different polarity, a blocker in between.
    CHECK(gates(optimized(circuit(2, {G("cx", {0, 1}), G("cx", {1, 0})}), Basis1q::ZSX)) == 2);
    CHECK(gates(optimized(circuit(2, {controlled(G("ry", {0}, {0.4}), {1}, {1}), controlled(G("ry", {0}, {-0.4}), {1})}), Basis1q::ZSX)) == 2);
    CHECK(gates(optimized(circuit(2, {G("cx", {0, 1}), G("h", {0}), G("cx", {0, 1})}), Basis1q::ZSX)) >= 3);
}

TEST_CASE("§5.3 cancellation and merging look through commuting gates") {
    // rz on the control and x on the target pass through cx; cx sharing a control or a target commute.
    ir::Circuit a = optimized(circuit(2, {G("cx", {0, 1}), G("rz", {0}, {0.3}), G("cx", {0, 1})}), Basis1q::ZSX);
    CHECK(gates(a) == 1);
    CHECK(gatesOf(a)[0]->name == "rz");
    ir::Circuit b = optimized(circuit(2, {G("cx", {0, 1}), G("x", {1}), G("sx", {1}), G("cx", {0, 1})}), Basis1q::ZSX);
    CHECK(countGates(b, "cx") == 0);
    CHECK(countGates(optimized(circuit(3, {G("cx", {0, 1}), G("cx", {0, 2}), G("cx", {0, 1})}), Basis1q::ZSX), "cx") == 1);
    CHECK(countGates(optimized(circuit(3, {G("cx", {0, 2}), G("cx", {1, 2}), G("cx", {0, 2})}), Basis1q::ZSX), "cx") == 1);
    CHECK(countGates(optimized(circuit(3, {G("cx", {0, 1}), G("cz", {0, 2}), G("rz", {0}, {1.0}), G("cx", {0, 1})}), Basis1q::ZSX), "cx") == 0);
    // A target of one cx that is the control of another does not commute: nothing cancels.
    CHECK(countGates(optimized(circuit(3, {G("cx", {0, 1}), G("cx", {1, 2}), G("cx", {0, 1})}), Basis1q::ZSX), "cx") == 3);
    // z on the target, x on the control: blocked.
    CHECK(countGates(optimized(circuit(2, {G("cx", {0, 1}), G("rz", {1}, {0.3}), G("cx", {0, 1})}), Basis1q::ZSX), "cx") == 2);
    CHECK(countGates(optimized(circuit(2, {G("cx", {0, 1}), G("sx", {0}), G("cx", {0, 1})}), Basis1q::ZSX), "cx") == 2);
    // With commutation off only adjacent pairs cancel.
    compiler::OptimizeOptions adjacentOnly;
    adjacentOnly.commute = false;
    CHECK(countGates(optimized(circuit(2, {G("cx", {0, 1}), G("rz", {0}, {0.3}), G("cx", {0, 1})}), Basis1q::ZSX, adjacentOnly), "cx") == 2);
    // A diagonal gate moves left across the control and merges into the run before it.
    ir::Circuit m = optimized(circuit(2, {G("rz", {0}, {0.2}), G("sx", {0}), G("rz", {0}, {0.4}), G("cx", {0, 1}), G("rz", {0}, {0.5}), G("sx", {0})}), Basis1q::ZSX);
    CHECK(gates(m) == 5);
    CHECK(std::abs(gatesOf(m)[2]->params[0] - 0.9) < 1e-12);
}

TEST_CASE("§5.6 diagonal merging adds angles and drops full turns") {
    ir::Circuit a = optimized(circuit(2, {G("rz", {0}, {0.25}), G("cz", {0, 1}), G("rz", {0}, {0.5}), G("t", {0})}), Basis1q::ZSX);
    REQUIRE(gates(a) == 2);
    CHECK(gatesOf(a)[0]->name == "rz");
    CHECK(std::abs(gatesOf(a)[0]->params[0] - (0.75 + kPi / 4)) < 1e-12);
    ir::Circuit b = optimized(circuit(2, {G("cp", {0, 1}, {0.3}), G("cp", {1, 0}, {0.4}), G("rzz", {0, 1}, {1.0}), G("rzz", {1, 0}, {-0.25})}), Basis1q::ZSX);
    REQUIRE(gates(b) == 2);
    CHECK(std::abs(gatesOf(b)[0]->params[0] - 0.7) < 1e-12);
    CHECK(std::abs(gatesOf(b)[1]->params[0] - 0.75) < 1e-12);
    CHECK(gates(optimized(circuit(2, {G("cp", {0, 1}, {kPi}), G("cp", {0, 1}, {kPi})}), Basis1q::ZSX)) == 0);
    CHECK(gates(optimized(circuit(2, {G("rxx", {0, 1}, {kPi}), G("rxx", {0, 1}, {kPi})}), Basis1q::ZYZ)) == 0);   // −I: a global phase
    // crz(2π) = Z on the control is NOT the identity: the period of a controlled rotation is 4π.
    ir::Circuit c = optimized(circuit(2, {G("crz", {0, 1}, {kPi}), G("crz", {0, 1}, {kPi})}), Basis1q::ZSX);
    CHECK(gates(c) == 1);
    CHECK(gates(optimized(circuit(2, {G("crz", {0, 1}, {2 * kPi}), G("crz", {0, 1}, {2 * kPi})}), Basis1q::ZSX)) == 0);
}

TEST_CASE("§5.2 single-qubit fusion resynthesises runs when that is cheaper") {
    // Seven native gates with three pulses → at most five gates with two pulses.
    ir::Circuit a = optimized(circuit(1, {G("rz", {0}, {0.1}), G("sx", {0}), G("rz", {0}, {0.2}), G("sx", {0}), G("rz", {0}, {0.3}),
                                          G("sx", {0}), G("rz", {0}, {0.4})}), Basis1q::ZSX);
    CHECK(gates(a) <= 5);
    CHECK(countGates(a, "sx") <= 2);
    // x · rz(a) · x = rz(−a): two pulses become none.
    ir::Circuit b = optimized(circuit(1, {G("x", {0}), G("rz", {0}, {0.6}), G("x", {0})}), Basis1q::ZSX);
    REQUIRE(gates(b) == 1);
    CHECK(std::abs(gatesOf(b)[0]->params[0] + 0.6) < 1e-12);
    ir::Circuit c = optimized(circuit(1, {G("sx", {0}), G("sx", {0})}), Basis1q::ZSX);
    REQUIRE(gates(c) == 1);
    CHECK(gatesOf(c)[0]->name == "x");
    // Identity results are deleted (spec: within 1e-12).
    CHECK(gates(optimized(circuit(1, {G("h", {0}), G("z", {0}), G("h", {0}), G("x", {0})}), Basis1q::ZSX)) == 0);
    // No device: a run is one U; ions: one rz·ry·rz.
    CHECK(gates(optimized(circuit(1, {G("h", {0}), G("t", {0}), G("h", {0})}), Basis1q::U)) == 1);
    CHECK(gates(optimized(circuit(1, {G("rx", {0}, {0.3}), G("ry", {0}, {0.4}), G("rx", {0}, {0.5}), G("rz", {0}, {0.1})}), Basis1q::ZYZ)) <= 3);
    // A run already in shortest form is left byte-identical.
    const ir::Circuit canonical = circuit(1, {G("rz", {0}, {0.5}), G("sx", {0}), G("rz", {0}, {1.5})});
    CHECK(optimized(canonical, Basis1q::ZSX).structurallyEqual(canonical, 0.0));
}

TEST_CASE("measure, reset, barrier, delay and control nodes block every rewrite; bodies are optimized") {
    ir::Circuit c = build(program(R"(qubit[2] q; bit[2] m;
h q[0]; barrier q[0]; h q[0];
x q[1]; m[1] = measure q[1]; x q[1];
s q[0]; delay[20ns] q[0]; sdg q[0];
z q[1]; reset q[1]; z q[1];
if (m[1] == 1) { x q[0]; x q[0]; h q[0]; }
)"));
    const std::size_t before = gates(c);
    ir::Circuit o = optimized(c, Basis1q::ZSX);
    CHECK(before == 8);
    CHECK(gates(o) == 8);                                 // nothing cancels across a blocker
    const auto* branch = std::get_if<ir::Branch>(&o.node(o.topologicalOrder().back()));
    REQUIRE(branch != nullptr);
    CHECK((*branch->thenBody).nodeCount() == 1);          // x·x vanished inside the body, h stays
}
