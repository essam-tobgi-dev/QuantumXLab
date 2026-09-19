// Spec 14 §8, 25 §4 — SABRE routing: every two-qubit gate of the output sits on a coupled pair of
// data qubits, the routed circuit equals the source after undoing the recorded mapping (checker
// and an independent permutation-matrix oracle), control-flow bodies restore the layout, routing
// `none` validates (QL4030), and the 127-qubit performance budget of spec 24 §6.
#include "CompilerTestUtil.hpp"
#include "Core/Timer.hpp"
#include "Numerics/Tensor.hpp"
#include <cmath>
#include <iostream>

using namespace ctest;

namespace {
compiler::CouplingGraph graphOf(const std::string& id) {
    const auto& d = device(id);
    auto g = compiler::CouplingGraph::build(d.device, &d.calibration);
    REQUIRE(g.has_value());
    return std::move(*g);
}
ir::Circuit randomCircuit(core::Random& rng, std::uint32_t n, std::size_t twoQubitGates) {
    std::vector<ir::Gate> gs;
    while (twoQubitGates > 0) {
        const auto a = static_cast<std::uint32_t>(rng.uniformInt(n));
        auto b = static_cast<std::uint32_t>(rng.uniformInt(n - 1));
        if (b >= a)
            ++b;
        switch (rng.uniformInt(4)) {
        case 0:
            gs.push_back(G("h", {a}));
            break;
        case 1:
            gs.push_back(G("rz", {a}, {angle(rng)}));
            break;
        default:
            gs.push_back(G("cx", {a, b}));
            --twoQubitGates;
            break;
        }
    }
    return circuit(n, std::move(gs));
}
// Independent structural check against hw::Device (not the compiler's own graph).
void requireOnCouplingMap(const ir::Circuit& c, const hw::Device& dev) {
    REQUIRE(c.isPhysical());
    REQUIRE(c.qubitCount() == dev.qubitCount());
    for (const ir::Gate* g : gatesOf(c)) {
        for (ir::Wire w : g->wires())
            REQUIRE_FALSE(dev.isCoupler(w.index));
        if (g->width() == 2) {
            INFO(g->name << " $" << g->wires()[0].index << ", $" << g->wires()[1].index);
            REQUIRE(dev.adjacent(g->wires()[0].index, g->wires()[1].index));
        }
    }
}
} // namespace

TEST_CASE("routing respects the coupling map and preserves the circuit on the shipped devices") {
    core::Random rng(99);
    struct Case {
        std::string id;
        std::uint32_t qubits;
        std::size_t gates;
    };
    for (const Case& k :
         {Case{"sc_fixed_5", 5, 40}, Case{"sc_heavyhex_27", 9, 50}, Case{"sc_heavyhex_127", 10, 40},
          Case{"sc_tunable_grid_54", 8, 40}, Case{"ion_chain_11", 8, 30}})
        for (int trial = 0; trial < 3; ++trial) {
            const auto g = graphOf(k.id);
            const ir::Circuit source = randomCircuit(rng, k.qubits, k.gates);
            auto layout = compiler::chooseLayout(source, g,
                                                 trial == 0 ? compiler::LayoutPolicy::Trivial
                                                            : compiler::LayoutPolicy::NoiseAware,
                                                 11, 100000, nullptr);
            REQUIRE(layout.has_value());
            compiler::RouteOptions o;
            o.seed = 1000 + static_cast<std::uint64_t>(trial);
            o.noiseAware = trial == 2;
            auto routed = compiler::route(source, layout->layout, g, o);
            INFO(k.id << " trial " << trial << (routed ? std::string() : routed.error().format()));
            REQUIRE(routed.has_value());
            requireOnCouplingMap(routed->circuit, device(k.id).device);
            REQUIRE(ir::verify(routed->circuit).has_value());
            CHECK(routed->circuit.qubitRegisters().empty());
            CHECK(routed->swaps == countGates(routed->circuit, "swap"));
            CHECK(gatesOf(routed->circuit).size() == gatesOf(source).size() + routed->swaps);
            CHECK(routed->circuit.layout() == routed->initialLayout.v2p);
            CHECK(routed->circuit.meta()["final_layout"].get<std::vector<std::uint32_t>>() ==
                  routed->finalLayout.v2p);
            CHECK(routed->initialLayout.injective());
            CHECK(routed->finalLayout.injective());
            if (device(k.id).device.allToAll)
                CHECK(routed->swaps == 0);
            compiler::EquivalenceOptions eq;
            eq.states = 6;
            eq.maxUnitaryQubits = 8;
            const auto report = compiler::checkEquivalence(source, routed->circuit, eq);
            REQUIRE(report.has_value());
            INFO(report->detail << " on " << report->wires << " wires by "
                                << compiler::equivalenceMethodName(report->method));
            REQUIRE(report->equivalent);
        }
}

TEST_CASE("independent oracle: U_routed = P_final (U_source) P_initial† on sc_fixed_5") {
    core::Random rng(5);
    const auto g = graphOf("sc_fixed_5");
    for (int trial = 0; trial < 5; ++trial) {
        const ir::Circuit source = randomCircuit(rng, 5, 25);
        compiler::RouteOptions o;
        o.seed = static_cast<std::uint64_t>(trial);
        auto routed = compiler::route(source, compiler::Layout::identity(5), g, o);
        REQUIRE(routed.has_value());
        CHECK(routed->swaps > 0); // 25 random cx on a T-shaped tree cannot all be adjacent
        // Permutation matrices: |x⟩ on program qubits → the basis state with bit layout[v] = x_v.
        auto permutation = [](const compiler::Layout& l) {
            num::Matrix p(32, 32);
            for (std::size_t x = 0; x < 32; ++x) {
                std::size_t y = 0;
                for (std::uint32_t v = 0; v < 5; ++v)
                    if ((x >> v) & 1u)
                        y |= std::size_t{1} << l.v2p[v];
                p(y, x) = 1.0;
            }
            return p;
        };
        const num::Matrix want =
            num::matmul(num::matmul(permutation(routed->finalLayout), unitary(source)),
                        num::adjoint(permutation(routed->initialLayout).view()));
        REQUIRE(num::equalUpToGlobalPhase(want, unitary(routed->circuit), 1e-10));
    }
}

TEST_CASE("bodies of control nodes are routed and leave the layout as they found it") {
    const ir::Circuit source = build(program(R"(qubit[4] q; bit[2] m; bit[4] out;
h q[0]; cx q[0], q[3]; m[0] = measure q[0];
if (m[0] == 1) { cx q[1], q[2]; cx q[1], q[3]; h q[2]; } else { cx q[2], q[0]; }
cx q[0], q[2]; out = measure q;
)"));
    const auto g = graphOf("sc_fixed_5");
    auto routed = compiler::route(source, compiler::Layout::identity(4), g);
    REQUIRE(routed.has_value());
    REQUIRE(ir::verify(routed->circuit).has_value());
    const ir::Branch* branch = nullptr;
    for (auto id : routed->circuit.topologicalOrder())
        if (const auto* b = std::get_if<ir::Branch>(&routed->circuit.node(id)))
            branch = b;
    REQUIRE(branch != nullptr);
    for (const ir::Circuit* body : {&*branch->thenBody, &*branch->elseBody}) {
        CHECK(body->isPhysical());
        CHECK(body->qubitCount() == 5);
        for (const ir::Gate* gt : gatesOf(*body))
            if (gt->width() == 2)
                CHECK(g.adjacent(gt->wires()[0].index, gt->wires()[1].index));
        CHECK(countGates(*body, "swap") % 2 == 0); // every swap of a body is undone at its end
    }
    CHECK(countGates(*branch->thenBody, "swap") >=
          2); // q1–q3 are not coupled under any placement of the star
    // Spec 14 §8: an inserted swap carries the span of the gate it enables (here: of its `if`).
    for (const ir::Gate* gt : gatesOf(*branch->thenBody))
        if (gt->name == "swap")
            CHECK(gt->span.line == 5);
    for (const ir::Gate* gt : gatesOf(routed->circuit))
        if (gt->name == "swap") {
            CHECK(gt->span.line >= 4);
            CHECK(gt->span.file == "test.qasm");
        }
    const auto report = compiler::checkEquivalence(source, routed->circuit);
    REQUIRE(report.has_value());
    INFO(report->detail);
    CHECK(report->equivalent);
    CHECK(report->method == compiler::EquivalenceMethod::RandomStates);
}

TEST_CASE(
    "routing none validates the coupling map (QL4030); physical programs are adopted as written") {
    const auto g = graphOf("sc_fixed_5");
    const ir::Circuit ok = circuit(3, {G("cx", {0, 1}), G("cx", {1, 2})});
    auto applied = compiler::applyLayout(ok, compiler::Layout{{0, 1, 3}}, g);
    REQUIRE(applied.has_value());
    CHECK(applied->swaps == 0);
    REQUIRE(gatesOf(applied->circuit).size() == 2);
    CHECK(gatesOf(applied->circuit)[1]->targets[0].index == 1); // cx q[1], q[2] → cx $1, $3
    CHECK(gatesOf(applied->circuit)[1]->targets[1].index == 3);
    ir::Gate far = G("cx", {0, 2});
    far.span = SourceSpan{7, 1, 7, 12, "p.qasm"};
    auto bad = compiler::applyLayout(circuit(3, {far}), compiler::Layout{{0, 1, 3}}, g);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().diagnosticId == "QL4030");
    CHECK(bad.error().code == compiler::err::CouplingViolation);
    REQUIRE(bad.error().span.has_value());
    CHECK(bad.error().span->line == 7);
    CHECK(bad.error().format().find("$0 and $3") != std::string::npos);

    const ir::Circuit physical =
        build(program("pragma qlab.layout physical\nbit c; h $1; cx $1, $3; c = measure $3;"));
    auto adopted = compiler::adoptPhysical(physical, g);
    REQUIRE(adopted.has_value());
    CHECK(adopted->circuit.qubitCount() == 5);
    CHECK(adopted->initialLayout == compiler::Layout::identity(physical.qubitCount()));
    auto wrong =
        compiler::adoptPhysical(build(program("pragma qlab.layout physical\ncx $0, $4;")), g);
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().diagnosticId == "QL4030");
    // Layout errors: not injective, off the device, a coupler qubit.
    CHECK_FALSE(compiler::route(ok, compiler::Layout{{0, 0, 1}}, g).has_value());
    CHECK_FALSE(compiler::route(ok, compiler::Layout{{0, 1, 9}}, g).has_value());
    CHECK_FALSE(compiler::route(ok, compiler::Layout{{0, 1, 60}}, graphOf("sc_tunable_grid_54"))
                    .has_value());
}

TEST_CASE("performance: SABRE on sc_heavyhex_127 with 5000 two-qubit gates (spec 24 §6: 500 ms in "
          "Release)") {
    core::Random rng(2026);
    const auto g = graphOf("sc_heavyhex_127");
    const ir::Circuit source = randomCircuit(rng, 127, 5000);
    auto layout = compiler::trivialLayout(127, g);
    REQUIRE(layout.has_value());
    core::Timer timer;
    auto routed = compiler::route(source, *layout, g);
    const double ms = timer.ms();
    REQUIRE(routed.has_value());
    requireOnCouplingMap(routed->circuit, device("sc_heavyhex_127").device);
    std::cout << "[perf] SABRE 127 qubits, 5000 two-qubit gates: " << ms << " ms, " << routed->swaps
              << " swaps (budget 500 ms in Release)\n";
#ifdef NDEBUG
    CHECK(ms < 500.0);
#else
    CHECK(ms <
          30000.0); // loose bound for the unoptimised Debug build on a shared machine (≈ 3 s alone)
#endif
}
