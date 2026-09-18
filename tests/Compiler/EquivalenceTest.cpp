// Spec 14 §10 — the equivalence checker: global-phase invariance, sensitivity to small errors,
// mapping awareness (initial and final layout, routing ancillas), the Clifford tableau method on
// wide circuits, measurement and feed-forward, work limits and defcal-only gates.
#include "CompilerTestUtil.hpp"
#include <cmath>

using namespace ctest;
using compiler::EquivalenceMethod;

namespace {
compiler::EquivalenceReport check(const ir::Circuit& a, const ir::Circuit& b, compiler::EquivalenceOptions o = {}) {
    auto r = compiler::checkEquivalence(a, b, o);
    INFO((r ? r->detail : r.error().format()));
    REQUIRE(r.has_value());
    return *r;
}
compiler::EquivalenceOptions with(EquivalenceMethod m) {
    compiler::EquivalenceOptions o;
    o.method = m;
    return o;
}
// A "compiled" circuit on `wires` physical wires with explicit layouts.
ir::Circuit mapped(std::uint32_t wires, std::vector<ir::Gate> gates, std::vector<std::uint32_t> initial, std::vector<std::uint32_t> last) {
    ir::Circuit c = circuit(wires, std::move(gates), true);
    c.setLayout(std::move(initial));
    c.meta()["final_layout"] = last;
    return c;
}
} // namespace

TEST_CASE("global phase is ignored, a 1e-6 angle error or a relative phase is not") {
    const ir::Circuit ref = circuit(3, {G("h", {0}), G("t", {0}), G("cx", {0, 1}), G("ry", {2}, {0.4}), G("ccx", {0, 1, 2}), G("s", {1})});
    const ir::Circuit phased = circuit(3, {G("h", {0}), G("rz", {0}, {kPi / 4}), G("cx", {0, 1}), G("ry", {2}, {0.4}), G("ccx", {0, 1, 2}), G("rz", {1}, {kPi / 2}), G("gphase", {}, {1.3})});
    const double delta = 2e-4;
    const ir::Circuit off = circuit(3, {G("h", {0}), G("t", {0}), G("cx", {0, 1}), G("ry", {2}, {0.4 + delta}), G("ccx", {0, 1, 2}), G("s", {1})});
    const ir::Circuit relative = circuit(3, {G("h", {0}), G("t", {0}), G("cx", {0, 1}), G("ry", {2}, {0.4}), G("ccx", {0, 1, 2}), G("sdg", {1})});
    for (EquivalenceMethod m : {EquivalenceMethod::Auto, EquivalenceMethod::Unitary, EquivalenceMethod::RandomStates}) {
        const auto same = check(ref, phased, with(m));
        CHECK(same.equivalent);
        CHECK(same.worstOverlap > 1.0 - 1e-12);
        CHECK(same.method == (m == EquivalenceMethod::Auto ? EquivalenceMethod::Unitary : m));
        CHECK_FALSE(check(ref, off, with(m)).equivalent);        // 1 − cos(δ/2) = 5e-9 > ε
        CHECK_FALSE(check(ref, relative, with(m)).equivalent);
    }
    CHECK(check(ref, phased, with(EquivalenceMethod::Unitary)).inputs == 8);
    CHECK(check(ref, phased, with(EquivalenceMethod::RandomStates)).inputs == 32);
    // U_a† U_b is a conjugate of Ry(δ) on one qubit, so |tr|/2^n = cos(δ/2) = 1 − δ²/8 + O(δ⁴).
    const auto r = check(ref, off, with(EquivalenceMethod::Unitary));
    CHECK(std::abs((1.0 - r.worstOverlap) - delta * delta / 8.0) < 1e-12);
}

TEST_CASE("the recorded layouts are undone before comparing; routing ancillas start and end in |0>") {
    const ir::Circuit ref = circuit(2, {G("h", {0}), G("t", {1}), G("cx", {0, 1})});
    // q0 starts on $2, q1 on $0; a swap moves q0 to $1 (an ancilla until then), then cx $1, $0.
    const std::vector<ir::Gate> gates = {G("h", {2}), G("t", {0}), G("swap", {2, 1}), G("cx", {1, 0})};
    for (EquivalenceMethod m : {EquivalenceMethod::Unitary, EquivalenceMethod::RandomStates}) {
        const auto ok = check(ref, mapped(5, gates, {2, 0}, {1, 0}), with(m));
        CHECK(ok.equivalent);
        CHECK(ok.wires == 3);                                     // $0, $1, $2: idle wires are not simulated
        CHECK_FALSE(check(ref, mapped(5, gates, {2, 0}, {2, 0}), with(m)).equivalent);   // wrong final layout
        CHECK_FALSE(check(ref, mapped(5, gates, {1, 0}, {1, 0}), with(m)).equivalent);   // wrong initial layout
        // An ancilla that is left excited is a miscompile even if the program qubits look right.
        std::vector<ir::Gate> dirty = gates;
        dirty.push_back(G("x", {2}));
        CHECK_FALSE(check(ref, mapped(5, dirty, {2, 0}, {1, 0}), with(m)).equivalent);
    }
    auto bad = compiler::checkEquivalence(ref, mapped(5, gates, {2}, {1}));
    REQUIRE_FALSE(bad.has_value());   // layout size ≠ program qubits
}

TEST_CASE("Clifford circuits of any width are compared exactly through their Pauli tableaux") {
    const std::uint32_t n = 60;
    std::vector<ir::Gate> a{G("h", {0})}, b{G("rz", {0}, {kPi / 2}), G("sx", {0}), G("rz", {0}, {kPi / 2})};
    for (std::uint32_t q = 0; q + 1 < n; ++q) {
        a.push_back(G("cx", {q, q + 1}));
        b.push_back(G("h", {q + 1}));           // cx = (I⊗H) cz (I⊗H)
        b.push_back(G("cz", {q + 1, q}));
        b.push_back(G("h", {q + 1}));
    }
    a.push_back(G("s", {n - 1}));
    b.push_back(G("p", {n - 1}, {kPi / 2}));
    const auto same = check(circuit(n, a), circuit(n, b));
    CHECK(same.equivalent);
    CHECK(same.method == EquivalenceMethod::Clifford);
    CHECK(same.inputs == 2 * n);
    std::vector<ir::Gate> wrong = b;
    wrong.push_back(G("z", {n / 2}));           // a sign on one stabilizer generator
    const auto diff = check(circuit(n, a), circuit(n, wrong));
    CHECK_FALSE(diff.equivalent);
    CHECK(diff.method == EquivalenceMethod::Clifford);
    // The first generator whose image carries an X on qubit 30 is Z_0 → X_0 X_1 ⋯ X_59: its sign flips.
    CHECK(diff.detail.find("Z_0") != std::string::npos);
    // Conjugation tableau of h, s, cx (T02 §5.1).
    auto t = compiler::cliffordTableauOf(circuit(2, {G("h", {0}), G("s", {0}), G("cx", {0, 1})}));
    REQUIRE(t.has_value());
    CHECK(t->imageX(0).text() == "+IZ");        // X0 → Z0 (h) → Z0 (s) → Z0 (Z on a control passes cx)
    CHECK(t->imageZ(0).text() == "+XY");        // Z0 → X0 → Y0 → Y0 X1 (X on a control spreads to the target)
    CHECK(t->imageX(1).text() == "+XI");
    CHECK(t->imageZ(1).text() == "+ZZ");        // Z on a target spreads to the control
    CHECK_FALSE(compiler::cliffordTableauOf(circuit(1, {G("t", {0})})).has_value());
    CHECK(compiler::cliffordTableauOf(circuit(2, {G("ecr", {0, 1}), G("iswap", {0, 1}), G("rxx", {0, 1}, {kPi / 2})})).has_value());
    auto forced = compiler::checkEquivalence(circuit(1, {G("t", {0})}), circuit(1, {G("t", {0})}), with(EquivalenceMethod::Clifford));
    CHECK_FALSE(forced.has_value());
}

TEST_CASE("measurement and feed-forward: teleportation against its decomposition and against a wrong correction") {
    const std::string body = R"(qubit[3] q; bit[2] m; bit out;
ry(0.7) q[0]; h q[1]; cx q[1], q[2]; cx q[0], q[1]; h q[0];
m[0] = measure q[0]; m[1] = measure q[1];
if (m[1] == 1) { x q[2]; }
if (m[0] == 1) { CORRECTION q[2]; }
reset q[0];
out = measure q[2];
)";
    auto text = [&](const std::string& correction) {
        std::string s = body;
        s.replace(s.find("CORRECTION"), 10, correction);
        return program(s);
    };
    const ir::Circuit ref = build(text("z"));
    ir::Circuit lowered = ref;
    REQUIRE(compiler::decompose(lowered, targetOf("sc_fixed_5")).has_value());
    const auto ok = check(ref, lowered);
    CHECK(ok.equivalent);
    CHECK(ok.method == EquivalenceMethod::RandomStates);
    CHECK(ok.inputs == 64);                                       // 32 states × 2 outcome plans
    CHECK_FALSE(check(ref, build(text("x"))).equivalent);
    CHECK_FALSE(check(ref, build(text("h"))).equivalent);
    // s instead of z differs by a phase on |1⟩ of a qubit that is measured next: the two programs
    // are operationally identical, and the checker says so.
    CHECK(check(ref, build(text("s"))).equivalent);
    auto unitary = compiler::checkEquivalence(ref, lowered, with(EquivalenceMethod::Unitary));
    CHECK_FALSE(unitary.has_value());                             // not a measurement-free circuit
}

TEST_CASE("work limit and defcal-only gates make the automatic check report Skipped") {
    core::Random rng(3);
    std::vector<ir::Gate> gs;
    for (int k = 0; k < 200; ++k) {
        const auto a = static_cast<std::uint32_t>(rng.uniformInt(8));
        gs.push_back(k % 2 ? G("cx", {a, (a + 1) % 8}) : G("ry", {a}, {angle(rng)}));
    }
    const ir::Circuit c = circuit(8, gs);
    compiler::EquivalenceOptions o;
    o.workLimit = 2 * 200 * 256 * 4;          // four random states fit, the 256 basis states do not
    const auto limited = check(c, c, o);
    CHECK(limited.method == EquivalenceMethod::RandomStates);
    CHECK(limited.inputs == 4);
    CHECK(limited.equivalent);
    o.workLimit = 1000;
    const auto skipped = check(c, c, o);
    CHECK(skipped.method == EquivalenceMethod::Skipped);
    CHECK_FALSE(skipped.equivalent);
    ir::Gate opaque = G("x", {0});
    opaque.name = "my_pulse";
    opaque.opaque = true;
    const auto noMatrix = check(circuit(1, {opaque}), circuit(1, {opaque}));
    CHECK(noMatrix.method == EquivalenceMethod::Skipped);
    CHECK(noMatrix.detail.find("defcal") != std::string::npos);
}
