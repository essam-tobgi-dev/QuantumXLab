// Regression (found by the Compiler module): `syncCounts` dereferenced `Branch::elseBody` on an
// `if` with no `else`. `SubCircuit::operator*()` creates the body on first dereference, so an
// empty virtual body appeared inside a physical program and `ir::verify` rejected every
// `pragma qlab.layout physical` program containing an else-less `if`. Nested bodies must also
// inherit the parent's physical flag, and `verify` must skip absent bodies.
#include "IR/IR.hpp"
#include "Lang/Lang.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace qlab;

namespace {
ir::Circuit build(const std::string& src) {
    auto p = lang::parseProgram(src, "branch_body_test.qasm");
    REQUIRE(p.has_value());
    for (const auto& d : p->diagnostics) INFO(d.error.format());
    REQUIRE_FALSE(lang::hasErrors(p->diagnostics));
    auto c = ir::buildCircuit(*p);
    if (!c) UNSCOPED_INFO(c.error().format());
    REQUIRE(c.has_value());
    return std::move(*c);
}
const ir::Branch& onlyBranch(const ir::Circuit& c) {
    for (ir::NodeId id : c.topologicalOrder())
        if (const auto* b = std::get_if<ir::Branch>(&c.node(id))) return *b;
    FAIL("no branch node");
    std::abort();
}
} // namespace

TEST_CASE("an else-less if leaves the else body absent and verifies") {
    const ir::Circuit c = build(R"(OPENQASM 3.0;
include "stdgates.inc";
bit[1] c;
qubit[2] q;
c[0] = measure q[0];
if (c[0] == 1) { x q[1]; }
)");
    const ir::Branch& br = onlyBranch(c);
    REQUIRE(br.thenBody.present());
    REQUIRE_FALSE(br.elseBody.present()); // never materialised
    REQUIRE(ir::verify(c).has_value());
}

TEST_CASE("a physical program with an else-less if verifies (the reported defect)") {
    const ir::Circuit c = build(R"(OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.layout physical
bit[1] c;
c[0] = measure $0;
if (c[0] == 1) { x $1; }
)");
    REQUIRE(c.isPhysical());
    const ir::Branch& br = onlyBranch(c);
    REQUIRE_FALSE(br.elseBody.present());
    REQUIRE(br.thenBody->isPhysical()); // nested bodies inherit the parent's flag
    REQUIRE(br.thenBody->qubitCount() == c.qubitCount());
    REQUIRE(br.thenBody->clbitCount() == c.clbitCount());
    auto st = ir::verify(c);
    if (!st) UNSCOPED_INFO(st.error().format());
    REQUIRE(st.has_value());
}

TEST_CASE("if/else and nested control bodies all inherit the physical shape") {
    const ir::Circuit c = build(R"(OPENQASM 3.0;
include "stdgates.inc";
pragma qlab.layout physical
bit[2] c;
c[0] = measure $0;
if (c[0] == 1) { x $1; } else { h $1; if (c[0] == 0) { z $1; } }
for int i in [0:1] { box { y $0; } }
)");
    const ir::Branch& br = onlyBranch(c);
    REQUIRE(br.thenBody.present());
    REQUIRE(br.elseBody.present());
    for (const ir::Circuit* body : {&*br.thenBody, &*br.elseBody}) {
        REQUIRE(body->isPhysical());
        REQUIRE(body->qubitCount() == c.qubitCount());
        REQUIRE(body->clbitCount() == c.clbitCount());
    }
    REQUIRE(ir::verify(c).has_value());
}

TEST_CASE("verify names the physical-flag mismatch rather than printing equal shapes") {
    ir::Circuit c = build(R"(OPENQASM 3.0;
include "stdgates.inc";
bit[1] c;
qubit[2] q;
c[0] = measure q[0];
if (c[0] == 1) { x q[1]; }
)");
    // Corrupt only the nested body's flag: shapes stay equal, so the message must say "physical".
    for (ir::NodeId id : c.topologicalOrder())
        if (auto* b = std::get_if<ir::Branch>(&c.node(id))) b->thenBody->setPhysical(true);
    auto st = ir::verify(c);
    REQUIRE_FALSE(st.has_value());
    const std::string msg = st.error().message;
    INFO(msg);
    REQUIRE(msg.find("physical") != std::string::npos);
    REQUIRE(msg.find("virtual") != std::string::npos);
}

TEST_CASE("an empty else body equals no else body") {
    // The emitter drops `else { }`, so a round trip turns a present-but-empty body into an absent
    // one; the two circuits are the same program and must compare equal.
    const ir::Circuit withEmpty = build(R"(OPENQASM 3.0;
include "stdgates.inc";
bit[1] c;
qubit[2] q;
c[0] = measure q[0];
if (c[0] == 1) { x q[1]; } else { }
)");
    const ir::Circuit without = build(R"(OPENQASM 3.0;
include "stdgates.inc";
bit[1] c;
qubit[2] q;
c[0] = measure q[0];
if (c[0] == 1) { x q[1]; }
)");
    REQUIRE(onlyBranch(withEmpty).elseBody.present());
    REQUIRE(onlyBranch(withEmpty).elseBody->nodeCount() == 0);
    REQUIRE_FALSE(onlyBranch(without).elseBody.present());
    REQUIRE(withEmpty.structurallyEqual(without, 0.0));
    REQUIRE(without.structurallyEqual(withEmpty, 0.0)); // symmetric
    // A non-empty else is still different.
    const ir::Circuit withElse = build(R"(OPENQASM 3.0;
include "stdgates.inc";
bit[1] c;
qubit[2] q;
c[0] = measure q[0];
if (c[0] == 1) { x q[1]; } else { z q[1]; }
)");
    REQUIRE_FALSE(withElse.structurallyEqual(without, 0.0));
}


TEST_CASE("the ratio of two durations folds to a number (OpenQASM 3 §3)") {
    // Reported by the calibration-program author: `rz(2*pi*f*t / 1s)` failed with "unsupported
    // arithmetic on two durations", so the one idiom that turns a swept delay into a phase did
    // not build.
    const ir::Circuit c = build(R"(OPENQASM 3.0;
include "stdgates.inc";
qubit[1] q;
duration t = 500ns;
rz(2 * pi * 0.5 * (t / 1us)) q[0];
delay[t] q[0];
)");
    REQUIRE(ir::verify(c).has_value());
    // 500 ns / 1 us = 0.5, so the angle is 2*pi*0.5*0.5 = pi/2.
    bool found = false;
    for (ir::NodeId id : c.topologicalOrder())
        if (const auto* g = std::get_if<ir::Gate>(&c.node(id)); g && g->name == "rz") {
            REQUIRE(g->params.size() == 1);
            REQUIRE(g->params[0] == Catch::Approx(3.14159265358979323846 / 2.0));
            found = true;
        }
    REQUIRE(found);
}

TEST_CASE("durations compare, and a dt ratio without a device is refused") {
    const ir::Circuit c = build(R"(OPENQASM 3.0;
include "stdgates.inc";
qubit[1] q;
duration a = 100ns;
duration b = 200ns;
if (a < b) { x q[0]; }
)");
    REQUIRE(ir::verify(c).has_value());
    // The comparison folds at build time, so the gate is emitted unconditionally, not as a branch.
    bool branch = false, gate = false;
    for (ir::NodeId id : c.topologicalOrder()) {
        branch = branch || std::holds_alternative<ir::Branch>(c.node(id));
        gate = gate || std::holds_alternative<ir::Gate>(c.node(id));
    }
    REQUIRE(gate);
    REQUIRE_FALSE(branch);

    // Mixing a `dt` duration with a wall-clock one needs the device, so it is refused here.
    auto p = lang::parseProgram(R"(OPENQASM 3.0;
include "stdgates.inc";
qubit[1] q;
duration t = 10dt;
rz(t / 1us) q[0];
)", "dt_ratio.qasm");
    REQUIRE(p.has_value());
    REQUIRE_FALSE(lang::hasErrors(p->diagnostics));
    auto built = ir::buildCircuit(*p);
    REQUIRE_FALSE(built.has_value());
    INFO(built.error().format());
    REQUIRE(built.error().message.find("device") != std::string::npos);
}
