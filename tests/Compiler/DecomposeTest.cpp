// Spec 14 §4, 25 §4 — decomposition to each native set preserves the unitary up to global phase
// and emits native gates only: all library gates, random U angles, controlled and multi-controlled
// gates, negative controls, directed couplers, ion entangler range, QL4050 and QL4070.
#include "CompilerTestUtil.hpp"
#include <cmath>

using namespace ctest;

namespace {
struct NativeCase { std::string label; compiler::Target target; };

std::vector<NativeCase> nativeCases() {
    return {{"{U, cx}", compiler::Target::universal()},
            {"sc_fixed_5 cx", targetOf("sc_fixed_5")},
            {"sc_fixed_5 ecr", targetOf("sc_fixed_5", "ecr")},
            {"sc_tunable_grid_54 cz", targetOf("sc_tunable_grid_54")},
            {"sc_tunable_grid_54 siswap", targetOf("sc_tunable_grid_54", "siswap")},
            {"ion_chain_11 ms", targetOf("ion_chain_11")}};
}

// Decomposes and checks: native gates only, same unitary up to global phase (tolerance 1e-9).
ir::Circuit lowered(const ir::Circuit& source, const compiler::Target& target, double tol = 1e-9) {
    ir::Circuit c = source;
    auto st = compiler::decompose(c, target);
    INFO((st ? std::string() : st.error().format()));
    REQUIRE(st.has_value());
    REQUIRE(ir::verify(c).has_value());
    for (const ir::Gate* g : gatesOf(c)) {
        INFO("emitted " << g->name << " on " << g->width() << " wire(s) for target " << target.name);
        REQUIRE(target.accepts(*g));
        REQUIRE((g->targets.size() == 1 ? target.isNative1q(g->name) : target.isNative2q(g->name)));
    }
    REQUIRE(num::equalUpToGlobalPhase(unitary(source), unitary(c, source.qubitCount()), tol));
    return c;
}
} // namespace

TEST_CASE("every library gate decomposes to every native set with the same unitary") {
    core::Random rng(42);
    for (const auto& nc : nativeCases())
        for (const ir::GateDef& d : ir::gates::all()) {
            if (d.name == "unitary" || d.name == "gphase") continue;
            for (int trial = 0; trial < (d.nParams > 0 ? 6 : 1); ++trial) {
                std::vector<double> params;
                for (int k = 0; k < d.nParams; ++k) params.push_back(angle(rng));
                std::vector<std::uint32_t> wires;
                for (int q = 0; q < d.nQubits; ++q) wires.push_back(static_cast<std::uint32_t>(d.nQubits - 1 - q));
                INFO(nc.label << ": " << d.name);
                lowered(circuit(static_cast<std::uint32_t>(d.nQubits), {G(d.name, wires, params)}), nc.target);
            }
        }
}

TEST_CASE("random U(θ,φ,λ) and inverted gates decompose exactly") {
    core::Random rng(5);
    for (const auto& nc : nativeCases())
        for (int trial = 0; trial < 40; ++trial) {
            ir::Gate inv = G("siswap", {1, 0});
            inv.adjoint = true;   // no named inverse: lowered through the inverted rule
            ir::Gate invU = G("iswap", {0, 1});
            invU.adjoint = true;
            INFO(nc.label);
            lowered(circuit(2, {G("U", {0}, {angle(rng), angle(rng), angle(rng)}), G("u3", {1}, {angle(rng), angle(rng), angle(rng)}),
                                inv, G("u2", {0}, {angle(rng), angle(rng)}), invU}),
                    nc.target);
        }
}

TEST_CASE("controlled gates: ABC, Gray code, negative controls, controlled multi-qubit gates") {
    core::Random rng(9);
    const auto universal = compiler::Target::universal();
    const auto transmon = targetOf("sc_fixed_5");
    for (const compiler::Target* t : {&universal, &transmon}) {
        // ctrl @ U with a random phase on U: the phase becomes a relative phase on the control.
        for (int trial = 0; trial < 10; ++trial)
            lowered(circuit(2, {G("cu", {1, 0}, {angle(rng), angle(rng), angle(rng), angle(rng)})}), *t);
        // negctrl @ x, mixed polarity ctrl/negctrl @ ry, ctrl(2) @ U, ctrl(3) @ x, ctrl(3) @ p.
        lowered(circuit(2, {controlled(G("x", {0}), {1}, {1})}), *t);
        lowered(circuit(3, {controlled(G("ry", {2}, {0.7}), {0, 1}, {0, 1})}), *t);
        lowered(circuit(3, {controlled(G("U", {1}, {0.4, 1.1, -2.0}), {2, 0})}), *t);
        lowered(circuit(4, {controlled(G("x", {3}), {0, 1, 2})}), *t);
        lowered(circuit(4, {controlled(G("p", {0}, {0.9}), {1, 2, 3}, {1, 0, 0})}), *t);
        lowered(circuit(5, {controlled(G("h", {4}), {0, 1, 2, 3})}), *t, 1e-8);
        // Controlled multi-qubit bases: the rule phase must become a controlled phase.
        lowered(circuit(4, {controlled(G("swap", {2, 3}), {0, 1})}), *t);
        lowered(circuit(3, {controlled(G("rxx", {1, 2}, {0.8}), {0})}), *t);
        lowered(circuit(3, {controlled(G("ecr", {0, 2}), {1})}), *t);       // rule phase e^{iπ/4}
        lowered(circuit(4, {controlled(G("cswap", {1, 2, 3}), {0}, {1})}), *t);
        // gphase under (negative) control is a phase gate on the control (spec 13 §3).
        lowered(circuit(2, {controlled(G("gphase", {}, {0.6}), {0, 1}, {1, 0})}), *t);
        ir::Gate adj = controlled(G("siswap", {0, 1}), {2});
        adj.adjoint = true;
        lowered(circuit(3, {adj}), *t);
    }
}

TEST_CASE("gate counts of the spec 14 §4.3 constructions") {
    const auto universal = compiler::Target::universal();
    auto cxCount = [&](ir::Circuit c) {
        REQUIRE(compiler::decompose(c, universal).has_value());
        return countGates(c, "cx");
    };
    CHECK(cxCount(circuit(3, {G("ccx", {0, 1, 2})})) == 6);                       // Toffoli: 6 cx
    CHECK(cxCount(circuit(3, {G("cswap", {0, 1, 2})})) == 8);                     // Fredkin: ccx + 2 cx
    CHECK(cxCount(circuit(2, {G("swap", {0, 1})})) == 3);
    CHECK(cxCount(circuit(2, {G("cu", {0, 1}, {0.3, 0.2, 0.1, 0.0})})) == 2);     // ABC: 2 cx
    // Gray code, n = 3: 2^3 − 1 controlled-V (2 cx each) + 2^3 − 2 cx between the controls.
    CHECK(cxCount(circuit(4, {controlled(G("U", {3}, {0.3, 0.2, 0.1}), {0, 1, 2})})) == 7 * 2 + 6);
    // The T-count of the Toffoli is 7 before single-qubit lowering (T02 §3.2).
    auto e = compiler::expandToCx(G("ccx", {0, 1, 2}));
    REQUIRE(e.has_value());
    std::size_t tCount = 0;
    for (const auto& g : e->gates) tCount += (g.name == "t" || g.name == "tdg") ? 1 : 0;
    CHECK(tCount == 7);
    CHECK(e->phase == 0.0);
}

TEST_CASE("QL4050 above eight controls and QL4070 for a gate without a rule") {
    const auto universal = compiler::Target::universal();
    std::vector<std::uint32_t> nine;
    for (std::uint32_t q = 0; q < 9; ++q) nine.push_back(q);
    ir::Gate big = controlled(G("x", {9}), nine, {1, 0, 0, 0, 0, 0, 0, 0, 0});
    big.span = SourceSpan{4, 3, 4, 20, "t.qasm"};
    ir::Circuit c = circuit(10, {big});
    auto st = compiler::decompose(c, universal);
    REQUIRE_FALSE(st.has_value());
    CHECK(st.error().diagnosticId == "QL4050");
    CHECK(st.error().code == compiler::err::TooManyControls);
    REQUIRE(st.error().span.has_value());
    CHECK(st.error().span->line == 4);
    CHECK(c.nodeCount() == 1);   // a failed pass leaves the circuit untouched

    num::Matrix m = num::Matrix::identity(8);
    auto custom = ir::makeUnitary(m, {W(0), W(1), W(2)});
    REQUIRE(custom.has_value());
    ir::Circuit c2 = circuit(3, {*custom});
    st = compiler::decompose(c2, targetOf("sc_fixed_5"));
    REQUIRE_FALSE(st.has_value());
    CHECK(st.error().diagnosticId == "QL4070");
    CHECK(st.error().message.find("sc_fixed_5") != std::string::npos);
}

TEST_CASE("directed coupler: a cx against the native direction is reversed with Hadamards") {
    const auto& dev = device("sc_fixed_5").device;
    REQUIRE(dev.nativeDirection(0, 1));
    REQUIRE_FALSE(dev.nativeDirection(1, 0));
    for (const char* entangler : {"cx", "ecr"}) {
        const auto target = targetOf("sc_fixed_5", entangler);
        ir::Circuit source = circuit(5, {G("cx", {1, 0}), G(entangler, {1, 0}), G("swap", {3, 4})}, true);
        ir::Circuit c = lowered(source, target);
        for (const ir::Gate* g : gatesOf(c))
            if (g->targets.size() == 2) {
                INFO(g->name << " $" << g->targets[0].index << ", $" << g->targets[1].index);
                CHECK(dev.nativeDirection(g->targets[0].index, g->targets[1].index));
            }
    }
    // On a virtual circuit the direction is not known yet: the gate is left as written.
    ir::Circuit virt = circuit(2, {G("cx", {1, 0})});
    REQUIRE(compiler::decompose(virt, targetOf("sc_fixed_5")).has_value());
    CHECK(gatesOf(virt).size() == 1);
}

TEST_CASE("ions: every entangler is one ms with |θ| ≤ π/2; larger angles split in two") {
    const auto ion = targetOf("ion_chain_11");
    CHECK(ion.entangler == "ms");
    CHECK(ion.basis == compiler::Basis1q::ZYZ);
    for (double theta : {0.3, -1.2, kPi / 2, 2.5, -3.0, 5.5, 2 * kPi}) {
        ir::Circuit c = lowered(circuit(2, {G("rxx", {0, 1}, {theta})}), ion);
        const double w = compiler::wrapAngle(theta);
        const std::size_t want = std::abs(w) < 1e-12 ? 0 : std::abs(w) > kPi / 2 + 1e-12 ? 2 : 1;
        CHECK(countGates(c, "ms") == want);
        for (const ir::Gate* g : gatesOf(c)) CHECK(std::abs(g->params[0]) <= kPi / 2 + 1e-12);
    }
    // One MS per ZZ-type interaction: cp, cz, crz, rzz, ryy (T06 §6); a cx costs one as well.
    for (const char* name : {"cp", "crz", "rzz", "ryy"}) CHECK(countGates(lowered(circuit(2, {G(name, {0, 1}, {0.9})}), ion), "ms") == 1);
    CHECK(countGates(lowered(circuit(2, {G("cz", {0, 1})}), ion), "ms") == 1);
    CHECK(countGates(lowered(circuit(2, {G("cx", {0, 1})}), ion), "ms") == 1);
}
