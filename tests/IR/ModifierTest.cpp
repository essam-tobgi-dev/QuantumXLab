// Spec 13 §3, 14 §2, T02 §3 — modifier expansion: node form and matrix for ctrl, negctrl, inv, pow
// on library gates, global phase, and user gates. Oracles are unitaries of hand-built circuits.
#include "IrTestUtil.hpp"
#include <cmath>
#include <numbers>

using namespace qlab;
using namespace irtest;

namespace {
num::Matrix unitaryOf(std::string_view body) {
    return unitary(build(program(body)));
}
num::Matrix libraryMatrix(std::string_view name, std::vector<double> p = {}) {
    auto m = ir::gates::matrix(name, p);
    REQUIRE(m.has_value());
    return *m;
}
} // namespace

TEST_CASE("ctrl @ x is cx; negctrl @ x is cx with the control conjugated by x") {
    const auto c = build(program("qubit[2] q;\nctrl @ x q[0], q[1];\n"));
    REQUIRE(c.nodeCount() == 1);
    const auto& g = gateAt(c, 0);
    CHECK((g.name == "cx" && g.controls.empty() &&
           indices(g.targets) == std::vector<std::uint32_t>{0, 1}));
    CHECK(g.cls == ir::GateClass::Cnot);
    CHECK(sameUnitary(unitary(c), unitaryOf("qubit[2] q;\ncx q[0], q[1];\n")));

    const auto n = build(program("qubit[2] q;\nnegctrl @ x q[0], q[1];\n"));
    const auto& ng = gateAt(n, 0);
    CHECK((ng.name == "x" && indices(ng.targets) == std::vector<std::uint32_t>{1}));
    CHECK((indices(ng.controls) == std::vector<std::uint32_t>{0} && ng.isNegControl(0)));
    const auto flipped = unitaryOf("qubit[2] q;\nx q[0];\ncx q[0], q[1];\nx q[0];\n");
    CHECK(sameUnitary(unitary(n), flipped));
    CHECK_FALSE(sameUnitary(unitary(n), unitary(c)));
    // Matrix of the node itself: wires() = {target, control}, the control on the high bit.
    auto m = ir::matrixOf(ng);
    REQUIRE(m.has_value());
    CHECK(sameUnitary(
        *m, num::Matrix::fromRows({{0, 1, 0, 0}, {1, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}})));
}

TEST_CASE("inv @ s is sdg, pow(2) @ s is z, and other closed forms stay named") {
    auto single = [](std::string_view stmt) {
        const auto c = build(program("qubit[2] q;\n" + std::string(stmt) + "\n"));
        REQUIRE(c.nodeCount() == 1);
        return gateAt(c, 0);
    };
    const auto sdg = single("inv @ s q[0];");
    CHECK((sdg.name == "sdg" && !sdg.adjoint));
    CHECK(sameUnitary(*ir::matrixOf(sdg), libraryMatrix("sdg")));
    CHECK(single("pow(2) @ s q[0];").name == "z");
    CHECK(single("pow(2) @ t q[0];").name == "s");
    CHECK(single("inv @ pow(2) @ t q[0];").name == "sdg");
    CHECK(single("pow(-1) @ sx q[0];").name == "sxdg");
    CHECK(single("pow(3) @ h q[0];").name == "h");
    const auto rx = single("inv @ rx(0.4) q[0];");
    CHECK((rx.name == "rx" && rx.params == std::vector<double>{-0.4}));
    const auto rz = single("pow(0.5) @ rz(0.8) q[0];");
    CHECK((rz.name == "rz" && std::abs(rz.params[0] - 0.4) < 1e-15));
    const auto u = single("inv @ U(0.3, 0.2, 0.1) q[0];");
    CHECK((u.name == "U" && u.params == std::vector<double>{-0.3, -0.1, -0.2}));
    CHECK(build(program("qubit q;\npow(2) @ x q;\npow(0) @ h q;\n")).nodeCount() == 0);
}

TEST_CASE("inv and pow without a closed form: adjoint flag and repetition") {
    const auto inv = build(program("qubit[2] q;\ninv @ iswap q[0], q[1];\n"));
    REQUIRE(inv.nodeCount() == 1);
    CHECK((gateAt(inv, 0).name == "iswap" && gateAt(inv, 0).adjoint));
    CHECK(sameUnitary(unitary(inv), num::adjoint(unitaryOf("qubit[2] q;\niswap q[0], q[1];\n"))));
    const auto cube = build(program("qubit[2] q;\npow(3) @ iswap q[0], q[1];\n"));
    CHECK(cube.nodeCount() == 3);
    const auto one = unitaryOf("qubit[2] q;\niswap q[0], q[1];\n");
    CHECK(sameUnitary(unitary(cube), num::matmul(one, num::matmul(one, one))));
    const auto back = build(program("qubit[2] q;\npow(-2) @ iswap q[0], q[1];\n"));
    CHECK((back.nodeCount() == 2 && gateAt(back, 1).adjoint));
    CHECK(sameUnitary(num::matmul(unitary(back), num::matmul(one, one)), num::Matrix::identity(4)));
}

TEST_CASE("multiple controls: ccx forms, mixed polarity, controlled rotations and U") {
    for (const char* stmt : {"ctrl(2) @ x q[0], q[1], q[2];", "ctrl @ ctrl @ x q[0], q[1], q[2];",
                             "ctrl @ cx q[0], q[1], q[2];"}) {
        const auto c = build(program("qubit[3] q;\n" + std::string(stmt) + "\n"));
        INFO(stmt);
        REQUIRE(c.nodeCount() == 1);
        CHECK((gateAt(c, 0).name == "ccx" &&
               indices(gateAt(c, 0).targets) == std::vector<std::uint32_t>{0, 1, 2}));
    }
    const auto mixed = build(program("qubit[3] q;\nnegctrl @ ctrl @ x q[0], q[1], q[2];\n"));
    const auto& mg = gateAt(mixed, 0);
    CHECK((mg.name == "x" && indices(mg.controls) == std::vector<std::uint32_t>{0, 1} &&
           mg.negControl == std::vector<std::uint8_t>{1, 0}));
    CHECK(sameUnitary(unitary(mixed),
                      unitaryOf("qubit[3] q;\nx q[0];\nccx q[0], q[1], q[2];\nx q[0];\n")));

    const auto crz = build(program("qubit[2] q;\nctrl @ rz(0.3) q[0], q[1];\n"));
    CHECK(gateAt(crz, 0).name == "crz");
    const auto ccrz = build(program("qubit[3] q;\nctrl @ ctrl @ rz(0.3) q[0], q[1], q[2];\n"));
    const auto& cg = gateAt(ccrz, 0);
    CHECK((cg.name == "rz" && indices(cg.controls) == std::vector<std::uint32_t>{0, 1} &&
           indices(cg.targets) == std::vector<std::uint32_t>{2}));
    // |q2 q1 q0>: rz acts on q2 only where q0 = q1 = 1 (indices 3 and 7).
    num::Matrix expect = num::Matrix::identity(8);
    expect(3, 3) = std::polar(1.0, -0.15);
    expect(7, 7) = std::polar(1.0, 0.15);
    CHECK(sameUnitary(unitary(ccrz), expect));

    const auto cu = build(program("qubit[2] q;\nctrl @ U(0.3, 0.2, 0.1) q[0], q[1];\n"));
    CHECK((gateAt(cu, 0).name == "cu" &&
           gateAt(cu, 0).params == std::vector<double>{0.3, 0.2, 0.1, 0.0}));
    CHECK(sameUnitary(unitary(cu), unitaryOf("qubit[2] q;\ncu(0.3, 0.2, 0.1, 0) q[0], q[1];\n")));
}

TEST_CASE("gphase is a global phase alone and a phase gate under control") {
    const auto global = build(program("qubit q;\ngphase(0.7);\n"));
    REQUIRE(global.nodeCount() == 1);
    CHECK(sameUnitary(unitary(global), num::scale(num::Matrix::identity(2), std::polar(1.0, 0.7))));
    CHECK(global.depth() == 0);
    const auto p = build(program("qubit q;\nctrl @ gphase(0.7) q;\n"));
    CHECK((gateAt(p, 0).name == "p" && gateAt(p, 0).params == std::vector<double>{0.7}));
    const auto cp = build(program("qubit[2] q;\nctrl(2) @ gphase(0.7) q[0], q[1];\n"));
    CHECK((gateAt(cp, 0).name == "cp" &&
           indices(gateAt(cp, 0).targets) == std::vector<std::uint32_t>{0, 1}));
    const auto neg = build(program("qubit q;\nnegctrl @ gphase(0.7) q;\n"));
    CHECK((gateAt(neg, 0).name == "gphase" && gateAt(neg, 0).isNegControl(0)));
    CHECK(sameUnitary(unitary(neg), num::Matrix::fromRows({{std::polar(1.0, 0.7), 0}, {0, 1}})));
}

TEST_CASE("user gates expand with parameters, and modifiers apply to the whole body") {
    const std::string decl = "gate mygate(a) x0, x1 { rz(a) x0; cx x0, x1; }\n";
    const auto plain = build(program(decl + "qubit[3] q;\nmygate(0.3) q[0], q[1];\n"));
    REQUIRE(plain.nodeCount() == 2);
    CHECK((gateAt(plain, 0).name == "rz" && gateAt(plain, 1).name == "cx"));
    const auto u = unitary(plain); // oracle U on q0, q1 (q2 idle)

    const auto inv = build(program(decl + "qubit[3] q;\ninv @ mygate(0.3) q[0], q[1];\n"));
    CHECK((gateAt(inv, 0).name == "cx" && gateAt(inv, 1).params == std::vector<double>{-0.3}));
    CHECK(sameUnitary(unitary(inv), num::adjoint(u)));

    const auto sq = build(program(decl + "qubit[3] q;\npow(2) @ mygate(0.3) q[0], q[1];\n"));
    CHECK(sq.nodeCount() == 4);
    CHECK(sameUnitary(unitary(sq), num::matmul(u, u)));

    // ctrl @ G on control q2: |0><0| ⊗ I + |1><1| ⊗ U, q2 the most significant qubit.
    const auto ctl = build(program(decl + "qubit[3] q;\nctrl @ mygate(0.3) q[2], q[0], q[1];\n"));
    CHECK((gateAt(ctl, 0).name == "crz" && gateAt(ctl, 1).name == "ccx"));
    const auto u2 = unitary(build(program(decl + "qubit[2] q;\nmygate(0.3) q[0], q[1];\n")));
    const num::Matrix p0 = num::Matrix::fromRows({{1, 0}, {0, 0}}),
                      p1 = num::Matrix::fromRows({{0, 0}, {0, 1}});
    const num::Matrix expect = num::add(num::kron(p0, num::Matrix::identity(4)), num::kron(p1, u2));
    CHECK(sameUnitary(unitary(ctl), expect));

    // gphase inside a controlled user gate becomes a phase on the control (spec 13 §3).
    const auto phased =
        build(program("gate g2 a { gphase(0.7); x a; }\nqubit[2] q;\nctrl @ g2 q[1], q[0];\n"));
    const num::Matrix ex = num::scale(libraryMatrix("x"), std::polar(1.0, 0.7));
    CHECK(sameUnitary(unitary(phased),
                      num::add(num::kron(p0, num::Matrix::identity(2)), num::kron(p1, ex))));
}

TEST_CASE("broadcast over registers applies modifiers per element") {
    const auto c = build(program("qubit[2] a;\nqubit[2] b;\nctrl @ rx(0.2) a, b;\ninv @ t a;\n"));
    REQUIRE(c.nodeCount() == 4);
    CHECK((gateAt(c, 0).name == "crx" &&
           indices(gateAt(c, 0).targets) == std::vector<std::uint32_t>{0, 2}));
    CHECK((gateAt(c, 1).name == "crx" &&
           indices(gateAt(c, 1).targets) == std::vector<std::uint32_t>{1, 3}));
    CHECK((gateAt(c, 2).name == "tdg" && gateAt(c, 3).name == "tdg"));
}
