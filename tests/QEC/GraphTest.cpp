// Spec 16 §5.1, T09 §5.3 — decoding-graph construction from detectors: space, time and boundary
// edges with their weights, the circuit-level detector error model checked fault by fault against
// the stabilizer backend, and hook edges in the Tomita–Svore schedule.
#include "QEC/Experiment.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>

using namespace qlab;
using namespace qlab::qec;
using Catch::Approx;

namespace {
StabilizerCode shipped(const std::string& id) { return loadShippedCode(id).value(); }
} // namespace

TEST_CASE("Graph: phenomenological lattice of the repetition code has space, time and boundary edges") {
    const StabilizerCode code = shipped("repetition_bitflip_3");
    ExtractionOptions x;
    x.rounds = 3;
    const MemoryExperiment ex = planMemoryExperiment(code, x).value();
    NoiseParams n;
    n.setting = NoiseSetting::Phenomenological;
    n.dataError = DataErrorKind::BitFlip;
    n.p = 0.01;
    n.q = 0.02;
    const DecodingGraph g = buildDecodingGraph(ex, planNoise(ex, n).value()).value();
    REQUIRE(g.detectors == 2 * 4);   // two checks, layers 0..3 (the readout closes the lattice)
    REQUIRE(g.cls == FidelityClass::Exact);
    REQUIRE(g.hyperedgesDropped == 0);
    REQUIRE(g.undetectedLogicalProbability == 0.0);
    std::size_t space = 0, time = 0, boundary = 0;
    for (const GraphEdge& e : g.edges) {
        if (e.boundary()) {
            ++boundary;
            REQUIRE(e.correction.size() == 1);
        } else if (ex.detectors[e.a].layer == ex.detectors[e.b].layer) {
            ++space;
            REQUIRE(e.correction == std::vector<std::pair<std::uint32_t, char>>{{1u, 'X'}});   // the shared qubit
            REQUIRE(e.observableMask == 0);                                                     // Z̄ = Z0 only
        } else {
            ++time;
            REQUIRE(ex.detectors[e.a].check == ex.detectors[e.b].check);
            REQUIRE(ex.detectors[e.b].layer == ex.detectors[e.a].layer + 1);
            REQUIRE(e.correction.empty());   // a measurement fault needs no data correction
            REQUIRE(e.probability == Approx(0.02).epsilon(1e-12));
            REQUIRE(e.weight == Approx(std::log(0.98 / 0.02)).epsilon(1e-12));   // T09 §6.2
        }
        if (!e.correction.empty()) {
            REQUIRE(e.probability == Approx(0.01).epsilon(1e-12));
            REQUIRE(e.weight == Approx(std::log(0.99 / 0.01)).epsilon(1e-12));
        }
    }
    // Data errors strike before rounds 0, 1, 2: one space edge (qubit 1) and two boundary edges
    // (qubits 0 and 2) per layer; syndrome flips of rounds 0, 1, 2 give two time edges each.
    REQUIRE(space == 3);
    REQUIRE(boundary == 6);
    REQUIRE(time == 6);
    // X on qubit 0 flips the readout of Z̄ = Z0: that boundary edge carries the logical action.
    const std::uint32_t e0 = g.findEdge(ex.detectorAt(0, 1));
    REQUIRE(e0 != kNoIndex);
    REQUIRE(g.edges[e0].correction.front().first == 0);
    REQUIRE(g.edges[e0].observableMask == 1);
    REQUIRE(g.findEdge(ex.detectorAt(1, 1), ex.detectorAt(0, 1)) != kNoIndex);   // order-insensitive
    REQUIRE(g.findEdge(ex.detectorAt(0, 0), ex.detectorAt(1, 3)) == kNoIndex);
}

TEST_CASE("Graph: depolarizing data errors put 2p/3 on each matching component") {
    const StabilizerCode code = shipped("surface_rot_3");
    const MemoryExperiment ex = planMemoryExperiment(code, ExtractionOptions{.rounds = 2}).value();
    NoiseParams n;
    n.setting = NoiseSetting::Phenomenological;
    n.p = 0.03;
    n.q = 0.0;
    const DecodingGraph g = buildDecodingGraph(ex, planNoise(ex, n).value()).value();
    REQUIRE(g.ambiguousEdges == 0);
    REQUIRE(g.hyperedgesDecomposed == 0);   // Y = X·Z is split by check type, not decomposed
    for (const GraphEdge& e : g.edges) {
        // X or Y (Z checks) and Z or Y (X checks) each have probability 2p/3; the two qubits of a
        // boundary check of the other type share one boundary edge: 2·(2p/3)(1 − 2p/3).
        const double single = 0.02, merged = 2 * 0.02 * 0.98;
        const bool ok = std::abs(e.probability - single) < 1e-12 || std::abs(e.probability - merged) < 1e-12;
        INFO("edge " << e.a << "–" << e.b << " p = " << e.probability);
        REQUIRE(ok);
        const CheckType type = ex.detectors[e.a].type;
        for (const auto& [q, letter] : e.correction) REQUIRE(letter == 'X');   // Z readout: X flips it
        if (type == CheckType::X) {   // Z errors: invisible to the Z-basis readout
            REQUIRE(e.correction.empty());
            REQUIRE(e.observableMask == 0);
        }
    }
}

TEST_CASE("Graph: the circuit-level detector error model agrees with the stabilizer backend fault by fault") {
    const StabilizerCode code = shipped("surface_rot_3");
    ExtractionOptions x;
    x.rounds = 2;
    NoiseParams n;
    n.p = 1e-3;
    const auto runner = ExperimentRunner::create(code, x, n, "union_find").value();
    const DecodingGraph& g = *runner.graph();
    // Graph-like without approximation: no hyperedges, no ambiguity, no invisible logical fault.
    REQUIRE(g.cls == FidelityClass::Exact);
    REQUIRE(g.hyperedgesDecomposed == 0);
    REQUIRE(g.hyperedgesDropped == 0);
    REQUIRE(g.ambiguousEdges == 0);
    REQUIRE(g.undetectedLogicalProbability == 0.0);
    const auto faults = enumerateFaults(runner.noise());
    REQUIRE(g.faults == faults.size());
    auto worker = runner.worker();
    core::Random rng(3);
    std::size_t logicalFaults = 0;
    for (const FaultEvent& f : faults) {
        const auto tableau = worker.shotWithFaults({f}, rng, SampleEngine::Stabilizer).value();
        const auto frame = worker.shotWithFaults({f}, rng, SampleEngine::PauliFrame).value();
        REQUIRE(tableau.events == frame.events);
        REQUIRE(tableau.logical == frame.logical);
        // Every single fault fires at most two detectors of each check type and is decoded.
        std::size_t xEvents = 0, zEvents = 0;
        for (std::size_t d = 0; d < tableau.events.size(); ++d)
            if (tableau.events[d]) (runner.experiment().detectors[d].type == CheckType::X ? xEvents : zEvents)++;
        REQUIRE(xEvents <= 2);
        REQUIRE(zEvents <= 2);
        REQUIRE_FALSE(tableau.decoderRejected);
        REQUIRE_FALSE(tableau.failure);   // circuit-level distance 3: no single fault defeats the decoder
        logicalFaults += tableau.logical[0];
    }
    REQUIRE(logicalFaults > 0);   // some faults do flip the raw readout; the decoder undoes them
}

TEST_CASE("Graph: hook faults give edges between checks two steps apart, perpendicular to the logical") {
    // T09 §5.2: an X on an X-ancilla after its second CNOT spreads to its SW and SE data qubits, a
    // horizontal pair; in the Z-check graph it joins the checks to the west and east of that pair.
    const StabilizerCode code = shipped("surface_rot_5");
    ExtractionOptions x;
    x.rounds = 2;
    const MemoryExperiment ex = planMemoryExperiment(code, x).value();
    const auto sampler = TableauSampler::create(ex.schedule).value();
    const std::uint32_t xCheck = 9;   // X ancilla at (4, 4): order NW, NE, SW, SE = 7, 12, 6, 11
    REQUIRE(code.ancillas[xCheck].coord == Coord2{4, 4});
    REQUIRE(code.ancillas[xCheck].order == std::vector<std::uint32_t>{7, 12, 6, 11});
    const std::uint32_t ancilla = code.n + xCheck;
    std::uint32_t seen = 0, afterSecond = kNoIndex;
    for (std::uint32_t i = 0; i < ex.schedule.ops.size() && afterSecond == kNoIndex; ++i)
        if (ex.schedule.ops[i].kind == OpKind::CX && ex.schedule.ops[i].a == ancilla && ++seen == 2) afterSecond = i;
    FaultEvent hook;
    hook.afterOp = afterSecond;
    hook.qubitA = ancilla;
    hook.pauliA = 'X';
    core::Random rng(5);
    std::vector<std::uint8_t> bits;
    REQUIRE(sampler.run(std::span<const FaultEvent>(&hook, 1), rng, bits).has_value());
    const auto events = ex.detectionEvents(bits);
    std::vector<std::uint32_t> fired;
    for (std::uint32_t d = 0; d < events.size(); ++d)
        if (events[d]) fired.push_back(d);
    REQUIRE(fired.size() == 2);
    // X reaches data 6 at (3, 3) in CNOT layer 2 and data 11 at (5, 3) in layer 3. Their shared Z
    // check (4, 2) has already met both (layers 0 and 2), sees the pair in the next round and
    // cancels. Z check (2, 4) meets data 6 later in the same round (layer 3) and fires in layer 0;
    // Z check (6, 4) met data 11 earlier (layer 1) and fires in layer 1: a space-time diagonal
    // between checks of the same row y = 4, two cells apart in x.
    const Coord2 a = code.ancillas[ex.detectors[fired[0]].check].coord, b = code.ancillas[ex.detectors[fired[1]].check].coord;
    REQUIRE(a == Coord2{2, 4});
    REQUIRE(b == Coord2{6, 4});
    REQUIRE(ex.detectors[fired[0]].layer == 0);
    REQUIRE(ex.detectors[fired[1]].layer == 1);
    REQUIRE(ex.observableValues(bits)[0] == 0);
    // The circuit-level graph holds that edge; the code-capacity graph does not.
    NoiseParams n;
    n.p = 1e-3;
    const DecodingGraph circuit = buildDecodingGraph(ex, planNoise(ex, n).value()).value();
    REQUIRE(circuit.findEdge(fired[0], fired[1]) != kNoIndex);
    n.setting = NoiseSetting::CodeCapacity;
    const DecodingGraph capacity = buildDecodingGraph(ex, planNoise(ex, n).value()).value();
    REQUIRE(capacity.findEdge(fired[0], fired[1]) == kNoIndex);
    REQUIRE(capacity.edges.size() < circuit.edges.size());
}
