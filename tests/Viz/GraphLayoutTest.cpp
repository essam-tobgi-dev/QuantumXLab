// Spec 21 §3.12, §3.8 — coupling graph of the shipped devices (node/edge counts, positions,
// calibration colouring with legend, mapping overlay, hit testing) and the entanglement graph of a
// Bell pair. Headless.
#include "Hardware/Hardware.hpp"
#include "Viz/Layout/GraphLayout.hpp"
#include "Viz/Math/Color.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>

using namespace qlab;
using namespace qlab::viz;
using namespace qlab::viz::layout;
using Catch::Approx;

TEST_CASE("coupling graph of sc_heavyhex_27 has 27 nodes and 28 edges at the device's coordinates") {
    auto loaded = hw::loadShippedDevice("sc_heavyhex_27");
    REQUIRE(loaded.has_value());
    const DeviceGraph g = buildCouplingGraph(loaded->device, &loaded->calibration);
    CHECK(g.nodes.size() == 27);
    CHECK(g.dataNodes == 27);
    CHECK(g.edges.size() == 28);
    // Nodes sit exactly where device.json puts them, no two on the same spot.
    std::set<std::pair<long, long>> spots;
    for (std::size_t q = 0; q < 27; ++q) {
        CHECK(g.nodes[q].qubit == QubitIndex{static_cast<std::uint32_t>(q)});
        CHECK(g.nodes[q].pos.x == Approx(loaded->device.qubits[q].pos[0]));
        CHECK(g.nodes[q].pos.y == Approx(loaded->device.qubits[q].pos[1]));
        spots.insert({std::lround(g.nodes[q].pos.x * 1000), std::lround(g.nodes[q].pos.y * 1000)});
    }
    CHECK(spots.size() == 27);
    CHECK(g.extent.width() > 0.0);
    CHECK(g.extent.height() > 0.0);
    // Every edge joins two coupled qubits of the device; heavy-hex degree is at most 3.
    std::vector<int> degree(27, 0);
    for (const GraphEdge& e : g.edges) {
        CHECK(loaded->device.adjacent(e.a.get(), e.b.get()));
        ++degree[e.a.get()];
        ++degree[e.b.get()];
    }
    for (int d : degree) {
        CHECK(d >= 1);
        CHECK(d <= 3);
    }
}

TEST_CASE("coupling graph colours by calibration with a min/max legend (spec 22 §4)") {
    auto loaded = hw::loadShippedDevice("sc_heavyhex_27");
    REQUIRE(loaded.has_value());
    CouplingOptions o;
    o.nodeField = NodeField::T1;
    o.edgeField = EdgeField::Error2q;
    const DeviceGraph g = buildCouplingGraph(loaded->device, &loaded->calibration, o);
    REQUIRE(g.nodeLegend.valid);
    REQUIRE(g.edgeLegend.valid);
    double lo = 1e9, hi = 0.0;
    for (const auto& q : loaded->calibration.qubits) {
        lo = std::min(lo, q.t1.value.v);
        hi = std::max(hi, q.t1.value.v);
    }
    CHECK(g.nodeLegend.min == Approx(lo));
    CHECK(g.nodeLegend.max == Approx(hi));
    CHECK(g.nodeLegend.title == "T1");
    CHECK(g.nodeLegend.minLabel.find("s") != std::string::npos); // carries its unit ("… µs")
    for (const GraphNode& n : g.nodes) {
        REQUIRE(n.value.has_value());
        const glm::vec3 want = math::sequentialColor(g.nodeLegend.normalised(*n.value));
        CHECK(glm::length(n.color - want) < 1e-6f); // the node colour is the legend colour of its value
    }
    for (const GraphEdge& e : g.edges) {
        REQUIRE(e.value.has_value());
        CHECK(*e.value >= g.edgeLegend.min);
        CHECK(*e.value <= g.edgeLegend.max);
    }
    // Another field re-scales; no calibration → nothing is colour-coded and the legend says so.
    o.nodeField = NodeField::ReadoutError;
    CHECK(buildCouplingGraph(loaded->device, &loaded->calibration, o).nodeLegend.max < 1.0);
    const DeviceGraph bare = buildCouplingGraph(loaded->device, nullptr);
    CHECK_FALSE(bare.nodeLegend.valid);
    CHECK(bare.edges.size() == 28);
}

TEST_CASE("coupling graph: mapping overlay, hit testing, couplers and all-to-all devices") {
    auto five = hw::loadShippedDevice("sc_fixed_5");
    REQUIRE(five.has_value());
    const std::vector<std::uint32_t> virtualToPhysical{2, 0}; // program q0 → physical 2, q1 → physical 0
    CouplingOptions o;
    o.layout = virtualToPhysical;
    const DeviceGraph g = buildCouplingGraph(five->device, &five->calibration, o);
    REQUIRE(g.nodes.size() == 5);
    CHECK(g.edges.size() == 4);
    CHECK(g.nodes[2].virtualQubit == std::optional<std::uint32_t>{0}); // printed inside the node
    CHECK(g.nodes[0].virtualQubit == std::optional<std::uint32_t>{1});
    CHECK_FALSE(g.nodes[1].virtualQubit.has_value());
    // Hit testing in layout units: on a node, on the middle of an edge, in empty space.
    CHECK(g.nodeAt(g.nodes[3].pos, 0.3) == std::optional<std::size_t>{3});
    const GraphEdge& e0 = g.edges[0];
    const glm::dvec2 mid = 0.5 * (g.nodes[e0.a.get()].pos + g.nodes[e0.b.get()].pos);
    CHECK_FALSE(g.nodeAt(mid, 0.2).has_value());
    CHECK(g.edgeAt(mid, 0.1) == std::optional<std::size_t>{0});
    CHECK_FALSE(g.edgeAt({100.0, 100.0}, 0.1).has_value());

    auto grid = hw::loadShippedDevice("sc_tunable_grid_54");
    REQUIRE(grid.has_value());
    const DeviceGraph gg = buildCouplingGraph(grid->device, &grid->calibration);
    CHECK(gg.dataNodes == 54);           // couplers are nodes of their own kind, not qubits
    CHECK(gg.edges.size() == 93);        // one per grid edge (SPEC_DEVIATIONS #5)
    std::size_t couplers = 0;
    for (const GraphNode& n : gg.nodes) couplers += n.coupler ? 1 : 0;
    CHECK(couplers == 93);
    CHECK(gg.edges[0].coupler.has_value());

    auto ions = hw::loadShippedDevice("ion_chain_11");
    REQUIRE(ions.has_value());
    CHECK(buildCouplingGraph(ions->device, &ions->calibration).edges.size() == 55); // all-to-all: C(11, 2)
}

TEST_CASE("entanglement graph of a Bell pair: one full-weight edge labelled C = 1 (spec 21 §5)") {
    Reductions red;
    red.nQubits = 3;
    for (std::uint32_t q = 0; q < 3; ++q) {
        SingleReduction s;
        s.qubit = QubitIndex{q};
        s.entropyBits = q < 2 ? 1.0 : 0.0; // qubits 0, 1 form the pair; qubit 2 is a spectator in |0⟩
        red.singles.push_back(s);
    }
    PairReduction bell{QubitIndex{0}, QubitIndex{1}, {1.0, 1.0, 0.0, 2.0, 1.0}};
    PairReduction none{QubitIndex{0}, QubitIndex{2}, {1.0, 0.0, 1.0, 0.0, 0.0}};
    red.pairs = {bell, none};
    const DeviceGraph g = buildEntanglementGraph(nullptr, 3, red);
    REQUIRE(g.nodes.size() == 3);
    REQUIRE(g.edges.size() == 2);
    CHECK(g.edges[0].weight == Approx(1.0));          // I/2 = 1: full width and opacity
    REQUIRE(g.edges[0].concurrence.has_value());
    CHECK(*g.edges[0].concurrence == Approx(1.0));
    CHECK(g.edges[1].weight == Approx(0.0));
    CHECK_FALSE(g.edges[1].concurrence.has_value());  // I ≤ 0.01: no label
    CHECK(*g.nodes[0].value == Approx(1.0));
    CHECK(*g.nodes[2].value == Approx(0.0));
    CHECK(g.edgeLegend.max == Approx(2.0));
    // Without a device the register sits on a circle with unit spacing between neighbours.
    const auto ring = circlePositions(12);
    CHECK(glm::length(ring[0] - ring[1]) == Approx(2.0 * (12.0 / (2.0 * M_PI)) * std::sin(M_PI / 12.0)));
    CHECK(paletteIndexOf(qec::QubitRole::AncillaX) != paletteIndexOf(qec::QubitRole::AncillaZ));
}
