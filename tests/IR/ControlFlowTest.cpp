// Spec 14 §2 — loop unrolling and its bound (QL4020), def inlining, if → Branch, per-shot classical
// storage, run-time while → Loop, and evaluation of classical expressions over the bit space.
#include "IrTestUtil.hpp"

using namespace qlab;
using namespace irtest;

TEST_CASE("for and statically bounded while loops unroll with break and continue") {
    const auto sets = build(program(
        "qubit[3] q;\nfor int i in {2, 0} { h q[i]; }\nfor int i in [0:2:4] { x q[i / 2]; }\n"));
    REQUIRE(sets.nodeCount() == 5);
    CHECK(indices(gateAt(sets, 0).targets) == std::vector<std::uint32_t>{2});
    CHECK(indices(gateAt(sets, 1).targets) == std::vector<std::uint32_t>{0});
    CHECK(indices(gateAt(sets, 4).targets) == std::vector<std::uint32_t>{2});
    const auto flow = build(program(R"(qubit q;
for int i in [0:9] {
  if (i == 2) { continue; }
  if (i == 4) { break; }
  rz(i) q;
}
int k = 3;
while (k > 0) { x q; k -= 1; }
)"));
    REQUIRE(flow.nodeCount() == 6);
    CHECK(gateAt(flow, 2).params == std::vector<double>{3.0});
    CHECK(gateAt(flow, 5).name == "x");
    // Compound assignment on a float keeps the fraction (spec 13 §3 float arithmetic).
    const auto fl = build(program("qubit q;\nfloat a = 0.25;\na += 0.5;\na *= 2;\nrz(a) q;\n"));
    CHECK(gateAt(fl, 0).params == std::vector<double>{1.5});
}

TEST_CASE("loop unrolling beyond the bound is QL4020 with the expansion and the bound") {
    ir::BuildOptions opts;
    opts.loopUnrollBound = 10;
    const std::string flat = program("qubit q;\nfor int i in [0:10] { x q; }\n");
    auto e = tryBuild(flat, {}, opts);
    REQUIRE_FALSE(e.has_value());
    CHECK(e.error().diagnosticId == "QL4020");
    CHECK(e.error().code == ErrorCode::Compiler_ + 20);
    CHECK(e.error().message == "loop expands to 11 statements; bound is 10");
    REQUIRE(e.error().span.has_value());
    CHECK(e.error().span->line == 4);
    // Each nested loop fits, the total does not: 3 outer × (1 + 3 inner) statements.
    auto nested = tryBuild(
        program("qubit q;\nfor int i in [0:2] { for int j in [0:2] { x q; } }\n"), {}, opts);
    REQUIRE_FALSE(nested.has_value());
    CHECK(nested.error().diagnosticId == "QL4020");
    auto spin = tryBuild(program("qubit q;\nint k = 0;\nwhile (k < 100) { k += 1; }\n"), {}, opts);
    REQUIRE_FALSE(spin.has_value());
    CHECK(spin.error().diagnosticId == "QL4020");
    CHECK(build(flat).nodeCount() == 11); // the default bound (65536) accepts it
}

TEST_CASE(
    "def subroutines inline: qubits by reference, classical arguments by value, returned values") {
    const auto c = build(program(R"(def twice(float x) -> float { return 2 * x; }
def layer(qubit[2] r, int n) -> int {
  for int i in [0:1] { rx(twice(n)) r[i]; }
  return n + 1;
}
def bell(qubit a, qubit b) -> bit {
  h a;
  cx a, b;
  return measure a;
}
qubit[2] q;
bit c;
int next = layer(q, 3);
rz(next) q[0];
c = bell(q[0], q[1]);
)"));
    const auto ns = nodes(c);
    REQUIRE(ns.size() == 7);
    CHECK((gateAt(c, 0).name == "rx" && gateAt(c, 0).params == std::vector<double>{6.0}));
    CHECK(indices(gateAt(c, 1).targets) == std::vector<std::uint32_t>{1});
    CHECK(gateAt(c, 2).params == std::vector<double>{4.0});
    CHECK((gateAt(c, 3).name == "h" && gateAt(c, 4).name == "cx"));
    // `return measure a` lands in fresh bits that the assignment then copies into c.
    const auto& m = std::get<ir::Measure>(*ns[5]);
    CHECK((m.qubit.index == 0 && m.bit.index == 1));
    const auto& copy = std::get<ir::ClassicalOp>(*ns[6]);
    CHECK((copy.dst.reg.first == 0 && copy.expr.op == ir::ClassOp::BitRef &&
           copy.expr.bit.index == 1));
    REQUIRE(c.bitRegisters().size() == 2);
    CHECK(c.bitRegisters()[1].name == "__meas");
    // Inlined nodes keep the call site: `int next = layer(q, 3);` is line 15, `c = bell(…)`
    // line 17.
    CHECK(ir::nodeSpan(*ns[0]).line == 15);
    CHECK(ir::nodeSpan(*ns[2]).line == 16);
    CHECK(ir::nodeSpan(*ns[3]).line == 17);
    CHECK(ir::nodeSpan(*ns[6]).line == 17);
}

TEST_CASE("if on a measured bit becomes a Branch; constant conditions fold") {
    const auto c = build(program(R"(qubit[2] q;
bit[2] m;
m[0] = measure q[0];
if (m[0] == 1) { x q[1]; } else { z q[1]; h q[1]; }
if (2 > 1) { y q[0]; }
if (m == 2) { s q[0]; }
)"));
    const auto ns = nodes(c);
    REQUIRE(ns.size() == 4);
    const auto& br = std::get<ir::Branch>(*ns[1]);
    CHECK(br.cond.op == ir::ClassOp::Eq);
    CHECK((*br.thenBody).nodeCount() == 1);
    CHECK((*br.elseBody).nodeCount() == 2);
    CHECK((*br.thenBody).qubitCount() == 2);
    std::vector<std::uint8_t> bits = {1, 0};
    CHECK(br.cond.eval(bits) == 1);
    bits[0] = 0;
    CHECK(br.cond.eval(bits) == 0);
    CHECK(std::get<ir::Gate>(*ns[2]).name == "y");
    const auto& reg = std::get<ir::Branch>(*ns[3]);
    CHECK((reg.cond.args[0].op == ir::ClassOp::RegRef && reg.cond.args[0].reg.size == 2));
    CHECK(reg.cond.eval(std::vector<std::uint8_t>{0, 1}) == 1); // m = 0b10 little-endian
    CHECK(c.depth() == 3); // measure, then the branch on q1, then the branch reading m after it
    CHECK(c.hasClassicalControl());
    CHECK_FALSE(c.isPureUnitary());
}

TEST_CASE("classical variables reached by a measurement become storage, including under a branch") {
    const auto c = build(program(R"(qubit[2] q;
int k = 0;
bit b;
b = measure q[0];
if (b == 1) { k = 5; }
if (k == 5) { x q[1]; }
)"));
    REQUIRE(c.bitRegisters().size() == 2);
    CHECK((c.bitRegisters()[0].name == "k" && c.bitRegisters()[0].kind == ir::RegKind::Int &&
           c.bitRegisters()[0].size == 32));
    const auto ns = nodes(c);
    REQUIRE(ns.size() == 4);
    CHECK(std::get<ir::ClassicalOp>(*ns[0]).expr.value == 0);
    const auto& set = std::get<ir::Branch>(*ns[2]);
    const auto& inner =
        std::get<ir::ClassicalOp>((*set.thenBody).node((*set.thenBody).topologicalOrder()[0]));
    CHECK((inner.dst.reg.size == 32 && inner.expr.value == 5));
    const auto& use = std::get<ir::Branch>(*ns[3]);
    CHECK((use.cond.args[0].op == ir::ClassOp::RegRef && use.cond.args[0].isSigned));
    CHECK(c.bitRegisters()[1].name == "b");
    // A float changed under a run-time branch has no bit representation: rejected, not miscompiled,
    // and the error points at its declaration.
    auto rejected = tryBuild(program(
        "qubit q;\nbit b;\nfloat a = 0.5;\nb = measure q;\nif (b == 1) { a = 1.5; }\nrz(a) q;\n"));
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().code == ir::err::Unsupported);
    REQUIRE(rejected.error().span.has_value());
    CHECK(rejected.error().span->line == 5);
}

TEST_CASE("a while loop on a measurement is a run-time Loop node") {
    ir::BuildOptions opts;
    opts.loopUnrollBound = 500;
    const auto c = build(program(R"(qubit q;
bit b;
b = measure q;
while (b == 1) {
  x q;
  b = measure q;
}
)"),
                         {}, opts);
    const auto ns = nodes(c);
    REQUIRE(ns.size() == 2);
    const auto& lp = std::get<ir::Loop>(*ns[1]);
    CHECK(lp.maxIterations == 500);
    CHECK((*lp.body).nodeCount() == 2);
    CHECK(lp.cond.reads() == std::vector<ir::ClassicalBit>{ir::ClassicalBit{0}});
    CHECK(c.onClassicalBit(ir::ClassicalBit{0}).size() == 2);
}

TEST_CASE(
    "classical expressions read registers little-endian and int registers as two's complement") {
    using ir::ClassicalExpr;
    using ir::ClassOp;
    const std::vector<std::uint8_t> bits = {1, 0, 1, 1, 1, 1};
    CHECK(ClassicalExpr::regRef(ir::CregRef{0, 3}).eval(bits) == 5);
    CHECK(ClassicalExpr::regRef(ir::CregRef{3, 3}, true).eval(bits) == -1);
    const auto e = ClassicalExpr::binary(
        ClassOp::LogicAnd,
        ClassicalExpr::binary(ClassOp::Eq, ClassicalExpr::regRef(ir::CregRef{0, 2}),
                              ClassicalExpr::constant(1)),
        ClassicalExpr::unary(ClassOp::LogicNot, ClassicalExpr::bitRef(ir::ClassicalBit{1})));
    CHECK(e.eval(bits) == 1);
    CHECK(e.text() == "((c[0:1] == 1) && (!c[1]))");
    std::vector<std::uint8_t> mem(6, 0);
    ir::applyClassicalOp(ir::ClassicalOp{ClassicalExpr::constant(6),
                                         ir::ClassicalTarget{ir::CregRef{1, 2}, std::nullopt},
                                         {}},
                         mem);
    CHECK(mem == std::vector<std::uint8_t>{0, 0, 1, 0, 0, 0}); // 6 truncated to 2 bits = 0b10
}
