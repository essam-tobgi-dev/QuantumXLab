// Spec 14 §1.1 `Verify`, §3 — structural checks reject malformed circuits; toUnitary refuses
// non-unitary content; inverse and structural equality on hand-made circuits.
#include "IrTestUtil.hpp"
#include <numbers>

using namespace qlab;
using namespace irtest;

namespace {
ir::Circuit shell(std::uint32_t qubits, std::uint32_t bits = 0) {
    ir::Circuit c;
    c.setQubitCount(qubits);
    c.setClbitCount(bits);
    if (bits > 0) c.addBitRegister(ir::BitRegister{"c", 0, bits, ir::RegKind::Bit, false});
    return c;
}
ir::Gate gate(std::string_view name, std::vector<std::uint32_t> targets, std::vector<double> params = {}) {
    std::vector<ir::Wire> ws;
    for (auto t : targets) ws.push_back(ir::Wire{t});
    auto g = ir::makeGate(name, std::move(ws), std::move(params));
    REQUIRE(g.has_value());
    return std::move(*g);
}
ErrorCode rejectCode(const ir::Circuit& c) {
    auto st = ir::verify(c);
    REQUIRE_FALSE(st.has_value());
    return st.error().code;
}
ErrorCode unitaryError(const ir::Circuit& c) {
    auto u = ir::toUnitary(c);
    REQUIRE_FALSE(u.has_value());
    return u.error().code;
}
} // namespace

TEST_CASE("verify accepts well-formed circuits including nested bodies and discarded measurements") {
    const auto c = build(program(R"(qubit[2] q;
bit[2] c;
h q[0];
measure q[1];
c[0] = measure q[0];
if (c[0] == 1) { box { x q[1]; } }
)"));
    CHECK(ir::verify(c).has_value());
    auto custom = shell(2);
    const double r = 1.0 / std::sqrt(2.0);
    auto u = ir::makeUnitary(num::Matrix::fromRows({{r, r}, {r, -r}}), {ir::Wire{1}});
    REQUIRE(u.has_value());
    custom.add(*u);
    CHECK(ir::verify(custom).has_value());
}

TEST_CASE("verify rejects a non-unitary custom matrix") {
    CHECK_FALSE(ir::makeUnitary(num::Matrix::fromRows({{1, 0}, {0, 2}}), {ir::Wire{0}}).has_value());
    auto c = shell(1);
    ir::Gate g;
    g.name = "unitary";
    g.targets = {ir::Wire{0}};
    g.custom = num::Matrix::fromRows({{1, 0}, {0, 1.0 + 1e-9}});   // off by more than kUnitaryTol
    c.add(g);
    CHECK(rejectCode(c) == ir::err::NotUnitary);
    ir::Gate shape = g;
    shape.custom = num::Matrix::identity(4);   // 4x4 on one target
    auto s = shell(1);
    s.add(shape);
    CHECK(rejectCode(s) == ir::err::BadArity);
}

TEST_CASE("verify rejects control == target, duplicated or out-of-range wires, and bad arity") {
    auto same = shell(2);
    ir::Gate g = gate("x", {0});
    g.controls = {ir::Wire{0}};
    same.add(g);
    auto st = ir::verify(same);
    REQUIRE_FALSE(st.has_value());
    CHECK(st.error().code == ir::err::BadWire);
    CHECK(st.error().message.find("disjoint") != std::string::npos);

    auto dup = shell(2);
    ir::Gate cx = gate("cx", {0, 1});
    cx.targets = {ir::Wire{1}, ir::Wire{1}};
    dup.add(cx);
    CHECK(rejectCode(dup) == ir::err::BadWire);

    auto range = shell(2);
    range.add(gate("h", {0}));
    range.add(ir::Barrier{{ir::Wire{2}}, {}});
    CHECK(rejectCode(range) == ir::err::BadWire);

    auto params = shell(1);
    ir::Gate rz = gate("rz", {0}, {0.1});
    rz.params.clear();
    params.add(rz);
    CHECK(rejectCode(params) == ir::err::BadArity);

    auto unknown = shell(1);
    ir::Gate bogus = gate("x", {0});
    bogus.name = "not_a_gate";
    unknown.add(bogus);
    CHECK(rejectCode(unknown) == ir::err::UnknownGate);
}

TEST_CASE("verify rejects measurements and conditions outside the declared registers") {
    auto meas = shell(1, 1);
    meas.setClbitCount(2);   // bit 1 exists in the space but in no register
    meas.add(ir::Measure{ir::Wire{0}, ir::ClassicalBit{1}, std::nullopt, {}});
    CHECK(rejectCode(meas) == ir::err::BadBit);

    auto far = shell(1, 1);
    far.add(ir::Measure{ir::Wire{0}, ir::ClassicalBit{7}, std::nullopt, {}});
    CHECK(rejectCode(far) == ir::err::BadBit);

    auto cond = shell(1, 1);
    ir::Circuit arm = shell(1, 1);
    arm.add(gate("x", {0}));
    ir::Branch br;
    br.cond = ir::ClassicalExpr::binary(ir::ClassOp::Eq, ir::ClassicalExpr::bitRef(ir::ClassicalBit{3}), ir::ClassicalExpr::constant(1));
    br.thenBody = ir::SubCircuit(arm);
    cond.add(br);
    CHECK(rejectCode(cond) == ir::err::BadBit);

    auto shape = shell(1, 1);
    ir::Branch nested;
    nested.cond = ir::ClassicalExpr::bitRef(ir::ClassicalBit{0});
    nested.thenBody = ir::SubCircuit(shell(3, 1));   // body shaped differently from its parent
    shape.add(nested);
    CHECK(rejectCode(shape) == ir::err::BadNode);
}

TEST_CASE("toUnitary refuses measurement, reset, branches, loops, defcal-only gates and > 12 qubits") {
    auto m = shell(1, 1);
    m.add(ir::Measure{ir::Wire{0}, ir::ClassicalBit{0}, std::nullopt, {}});
    CHECK(unitaryError(m) == ir::err::NotPure);
    auto r = shell(1);
    r.add(ir::Reset{ir::Wire{0}, std::nullopt, {}});
    CHECK(unitaryError(r) == ir::err::NotPure);
    const auto branch = build(program("qubit q;\nbit b;\nb = measure q;\nif (b == 1) { x q; }\n"));
    CHECK(unitaryError(branch) == ir::err::NotPure);
    auto loop = shell(1, 1);
    ir::Loop lp;
    lp.cond = ir::ClassicalExpr::bitRef(ir::ClassicalBit{0});
    lp.maxIterations = 4;
    loop.add(lp);
    CHECK(unitaryError(loop) == ir::err::NotPure);
    auto opaque = shell(1);
    ir::Gate o;
    o.name = "xb";
    o.opaque = true;
    o.targets = {ir::Wire{0}};
    opaque.add(o);
    CHECK(ir::verify(opaque).has_value());
    CHECK(unitaryError(opaque) == ir::err::Unsupported);
    CHECK(unitaryError(shell(13)) == ir::err::TooLarge);
    CHECK(ir::toUnitary(shell(8)).has_value());
}

TEST_CASE("inverse undoes a circuit and structural equality tracks node payloads") {
    const auto c = build(program(R"(gate mygate(a) x0, x1 { rz(a) x0; cx x0, x1; }
qubit[3] q;
h q[0];
ctrl @ sx q[1], q[2];
inv @ iswap q[0], q[2];
mygate(0.3) q[2], q[1];
U(0.1, 0.2, 0.3) q[1];
negctrl @ t q[0], q[1];
barrier q;
)"));
    auto inv = c.inverse();
    REQUIRE(inv.has_value());
    CHECK(ir::verify(*inv).has_value());
    CHECK(sameUnitary(num::matmul(unitary(*inv), unitary(c)), num::Matrix::identity(8)));
    CHECK(c.structurallyEqual(c));
    CHECK_FALSE(c.structurallyEqual(*inv));
    auto shifted = c;
    auto& rz = std::get<ir::Gate>(shifted.node(shifted.topologicalOrder()[3]));   // rz(0.3) from mygate
    REQUIRE((rz.name == "rz" && rz.params.size() == 1));
    rz.params[0] += 1e-9;
    CHECK_FALSE(shifted.structurallyEqual(c));
    CHECK(shifted.structurallyEqual(c, 1e-6));
    const auto measured = build(program("qubit q;\nbit b;\nb = measure q;\n"));
    auto notInvertible = measured.inverse();
    REQUIRE_FALSE(notInvertible.has_value());
    CHECK(notInvertible.error().code == ir::err::NotPure);
}

TEST_CASE("toGateOp hands backends positive controls natively and negative controls as full matrices") {
    const auto c = build(program("qubit[3] q;\nctrl @ ctrl @ rz(0.3) q[0], q[1], q[2];\nnegctrl @ x q[0], q[1];\n"));
    auto op = ir::toGateOp(gateAt(c, 0));
    REQUIRE(op.has_value());
    CHECK((op->targets.size() == 1 && op->controls.size() == 2 && op->matrix.rows == 2));
    CHECK(op->cls == ir::GateClass::Diagonal);
    auto neg = ir::toGateOp(gateAt(c, 1));
    REQUIRE(neg.has_value());
    CHECK((neg->controls.empty() && neg->targets.size() == 2 && neg->matrix.rows == 4));
    CHECK(neg->cls == ir::GateClass::Generic);
}
