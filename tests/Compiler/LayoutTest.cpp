// Spec 14 §7 — coupling graph (data qubits only, hop and reliability distances), trivial / dense /
// VF2 / noise-aware layouts, QL4060, and brute-force oracles for the noise score.
#include "CompilerTestUtil.hpp"
#include <algorithm>
#include <cmath>
#include <set>

using namespace ctest;

namespace {
compiler::CouplingGraph graphOf(const std::string& id, bool calibrated = true) {
    const auto& d = device(id);
    auto g = compiler::CouplingGraph::build(d.device, calibrated ? &d.calibration : nullptr);
    INFO((g ? std::string() : g.error().format()));
    REQUIRE(g.has_value());
    return std::move(*g);
}
ir::Circuit line(std::uint32_t n) {
    std::vector<ir::Gate> gs;
    for (std::uint32_t q = 0; q + 1 < n; ++q) gs.push_back(G("cx", {q, q + 1}));
    return circuit(n, gs);
}
bool everyInteractionCoupled(const compiler::Layout& l, const compiler::InteractionGraph& ig, const compiler::CouplingGraph& g) {
    return std::all_of(ig.edges.begin(), ig.edges.end(), [&](const auto& e) { return g.adjacent(l.v2p[e.a], l.v2p[e.b]); });
}
} // namespace

TEST_CASE("coupling graph: hop distances, data qubits only, reliability distances") {
    const auto g5 = graphOf("sc_fixed_5");   // T shape: 0-1, 1-2, 1-3, 3-4
    CHECK(g5.edges().size() == 4);
    CHECK(g5.hops(0, 1) == 1);
    CHECK(g5.hops(0, 4) == 3);
    CHECK(g5.hops(2, 4) == 3);
    CHECK(g5.diameter() == 3);
    CHECK(g5.adjacent(3, 1));
    CHECK_FALSE(g5.adjacent(0, 2));
    // Reliability path = sum of −ln(1 − r) along the only path of the tree.
    const auto& cal = device("sc_fixed_5").calibration;
    const double want = -std::log1p(-cal.edge(0, 1)->gateError2q.value) - std::log1p(-cal.edge(1, 3)->gateError2q.value) -
                        std::log1p(-cal.edge(3, 4)->gateError2q.value);
    CHECK(std::abs(g5.pathCost(0, 4) - want) < 1e-15);
    CHECK(std::abs(g5.edgeCost(1, 3) + std::log1p(-cal.edge(1, 3)->gateError2q.value)) < 1e-15);
    CHECK(std::isinf(g5.edgeCost(0, 2)));

    // 54 data qubits on a grid, 93 couplers that never hold a program qubit: hops = Manhattan distance.
    const auto grid = graphOf("sc_tunable_grid_54");
    const auto& dev = device("sc_tunable_grid_54").device;
    CHECK(grid.qubitCount() == 147);
    REQUIRE(grid.dataQubits().size() == 54);
    CHECK(grid.edges().size() == 93);
    for (std::uint32_t a : grid.dataQubits()) {
        CHECK_FALSE(dev.isCoupler(a));
        for (std::uint32_t b : grid.dataQubits()) {
            const double manhattan = std::abs(dev.qubits[a].pos[0] - dev.qubits[b].pos[0]) + std::abs(dev.qubits[a].pos[1] - dev.qubits[b].pos[1]);
            REQUIRE(grid.hops(a, b) == static_cast<std::uint32_t>(std::lround(manhattan)));
        }
    }
    for (std::uint32_t q = 54; q < 147; ++q) {
        CHECK_FALSE(grid.isData(q));
        CHECK(grid.neighbours(q).empty());
        CHECK(grid.hops(0, q) == compiler::CouplingGraph::kUnreachable);
    }
    const auto ion = graphOf("ion_chain_11");
    CHECK(ion.allToAll());
    CHECK(ion.edges().size() == 55);
    CHECK(ion.hops(0, 10) == 1);
    // Without calibration every edge costs 1 and the reliability distance is the hop count.
    const auto bare = graphOf("sc_heavyhex_27", false);
    CHECK(bare.reliabilityDistance(0, 26) == static_cast<double>(bare.hops(0, 26)));
}

TEST_CASE("VF2 embeds a line into heavy-hex exactly; trivial and dense layouts are valid") {
    for (const auto& [id, length] : std::vector<std::pair<std::string, std::uint32_t>>{{"sc_heavyhex_27", 12}, {"sc_heavyhex_127", 40}}) {
        const auto g = graphOf(id);
        const ir::Circuit c = line(length);
        const auto ig = compiler::interactionGraph(c);
        REQUIRE(ig.edges.size() == length - 1);
        const auto vf2 = compiler::vf2Layout(ig, g);
        REQUIRE(vf2.layout.has_value());
        CHECK_FALSE(vf2.budgetExceeded);
        CHECK(vf2.embeddings >= 1);
        CHECK(compiler::validateLayout(*vf2.layout, length, g).has_value());
        CHECK(everyInteractionCoupled(*vf2.layout, ig, g));
        // Exact embedding ⇒ routing needs no swap and keeps the layout.
        auto routed = compiler::route(c, *vf2.layout, g);
        REQUIRE(routed.has_value());
        CHECK(routed->swaps == 0);
        CHECK(routed->initialLayout == *vf2.layout);
        CHECK(routed->finalLayout == *vf2.layout);

        auto trivial = compiler::trivialLayout(length, g);
        REQUIRE(trivial.has_value());
        for (std::uint32_t v = 0; v < length; ++v) CHECK(trivial->v2p[v] == g.dataQubits()[v]);
        auto dense = compiler::denseLayout(ig, g, 7);
        REQUIRE(dense.has_value());
        CHECK(compiler::validateLayout(*dense, length, g).has_value());
        // The dense subgraph is connected: a BFS from one member reaches all of them inside the set.
        std::set<std::uint32_t> members(dense->v2p.begin(), dense->v2p.end()), seen{dense->v2p[0]};
        std::vector<std::uint32_t> stack{dense->v2p[0]};
        while (!stack.empty()) {
            const auto p = stack.back();
            stack.pop_back();
            for (auto q : g.neighbours(p))
                if (members.contains(q) && seen.insert(q).second) stack.push_back(q);
        }
        CHECK(seen.size() == members.size());
    }
    CHECK_FALSE(compiler::trivialLayout(6, graphOf("sc_fixed_5")).has_value());
}

TEST_CASE("VF2 proves that a degree-4 star does not embed in heavy-hex; a tiny budget reports QL4060") {
    const auto g = graphOf("sc_heavyhex_27");
    const ir::Circuit star = circuit(5, {G("cx", {0, 1}), G("cx", {0, 2}), G("cx", {0, 3}), G("cx", {0, 4})});
    const auto none = compiler::vf2Layout(compiler::interactionGraph(star), g);
    CHECK_FALSE(none.layout.has_value());
    CHECK_FALSE(none.budgetExceeded);        // refused by the degree sequence, not by the budget
    // A 12-ring does embed in heavy-hex (its unit cell is a 12-cycle); with a budget of 5 states
    // the search gives up, the policy falls back to the dense layout and says so.
    std::vector<ir::Gate> ring;
    for (std::uint32_t q = 0; q < 12; ++q) ring.push_back(G("cz", {q, (q + 1) % 12}));
    const ir::Circuit c = circuit(12, ring);
    const auto found = compiler::vf2Layout(compiler::interactionGraph(c), g);
    REQUIRE(found.layout.has_value());
    CHECK(everyInteractionCoupled(*found.layout, compiler::interactionGraph(c), g));
    std::vector<lang::Diagnostic> diags;
    auto choice = compiler::chooseLayout(c, g, compiler::LayoutPolicy::Vf2, 1, 5, &diags);
    REQUIRE(choice.has_value());
    CHECK(choice->vf2.budgetExceeded);
    CHECK(choice->policy == compiler::LayoutPolicy::Dense);
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].id() == "QL4060");
    CHECK(diags[0].severity == lang::Severity::Info);
    diags.clear();
    choice = compiler::chooseLayout(c, g, compiler::LayoutPolicy::Vf2, 1, 1'000'000, &diags);
    REQUIRE(choice.has_value());
    CHECK(choice->policy == compiler::LayoutPolicy::Vf2);
    CHECK(diags.empty());
}

TEST_CASE("noise-aware layout reaches the brute-force optimum of the spec 14 §7 score on sc_fixed_5") {
    const auto g = graphOf("sc_fixed_5");
    const std::vector<ir::Circuit> programs = {
        build(program("qubit[2] q; bit[2] c; h q[0]; cx q[0], q[1]; c = measure q;")),
        build(program("qubit[3] q; bit[3] c; h q[0]; cx q[0], q[1]; cx q[1], q[2]; cx q[1], q[2]; c = measure q;")),
        build(program("qubit[4] q; h q[0]; cx q[0], q[1]; cx q[0], q[2]; cx q[0], q[3];")),
    };
    for (const auto& c : programs) {
        const auto ig = compiler::interactionGraph(c);
        const std::uint32_t k = ig.qubits;
        // Brute force over every injective placement on the five data qubits.
        std::vector<std::uint32_t> perm{0, 1, 2, 3, 4};
        double best = INFINITY;
        do {
            compiler::Layout l;
            l.v2p.assign(perm.begin(), perm.begin() + k);
            best = std::min(best, compiler::layoutScore(l, ig, g));
        } while (std::next_permutation(perm.begin(), perm.end()));
        auto choice = compiler::chooseLayout(c, g, compiler::LayoutPolicy::NoiseAware, 5, 1'000'000, nullptr);
        REQUIRE(choice.has_value());
        INFO("k = " << k << ", layout " << choice->layout.text());
        CHECK(std::abs(choice->score - best) <= 1e-12 * std::max(1.0, best));
        auto dense = compiler::chooseLayout(c, g, compiler::LayoutPolicy::Dense, 5, 1'000'000, nullptr);
        REQUIRE(dense.has_value());
        CHECK(choice->score <= dense->score + 1e-15);
    }
    // The score of a given layout is the formula: edge cost × weight + readout + t_circ / T1.
    const auto ig = compiler::interactionGraph(programs[0]);
    const auto& cal = device("sc_fixed_5").calibration;
    const compiler::Layout l{{0, 1}};
    const double tCirc = ig.depth * g.typicalTwoQubitSeconds();
    double want = -std::log1p(-cal.edge(0, 1)->gateError2q.value);
    for (std::uint32_t q : {0u, 1u}) want += -std::log(cal.qubits[q].readoutFidelity()) + tCirc / cal.qubits[q].t1.value.si();
    CHECK(std::abs(compiler::layoutScore(l, ig, g) - want) < 1e-15);
}
