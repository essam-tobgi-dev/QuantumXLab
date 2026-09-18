// Spec 16 §3, §9; T09 §4, §8.1 — encoders and transversal logical gates. Oracles: every generator
// reads +1 on the encoded state and the logical operators read the encoded input (state-vector
// backend for n ≤ 9, stabilizer backend for all codes); a non-CSS code corrects every single-qubit
// error after encoding; transversal gates exist exactly where the theory says.
#include "IR/IR.hpp"
#include "QEC/QEC.hpp"
#include "QSim/QSim.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <memory>

using namespace qlab;
using namespace qlab::qec;
using Catch::Approx;

namespace {
StabilizerCode shipped(const std::string& id) { return loadShippedCode(id).value(); }

// Input state on data qubit 0, then the encoder; returns ⟨P⟩ for the requested operators.
std::vector<double> encodedExpectations(qsim::IBackend& backend, const StabilizerCode& code, const std::string& input,
                                        const std::vector<PauliString>& operators) {
    REQUIRE(backend.allocate(code.n).has_value());
    ir::Circuit prep;
    prep.setQubitCount(code.n);
    if (input == "1" || input == "-") prep.add(*ir::makeGate("x", {ir::Wire{0}}));
    if (input == "+" || input == "-") prep.add(*ir::makeGate("h", {ir::Wire{0}}));
    const ir::Circuit encoder = buildEncoder(code).value();
    REQUIRE(ir::verify(encoder).has_value());
    for (const ir::Circuit* c : {static_cast<const ir::Circuit*>(&prep), &encoder})
        for (ir::NodeId id : c->topologicalOrder()) {
            const auto op = ir::toGateOp(std::get<ir::Gate>(c->node(id)));
            REQUIRE(op.has_value());
            REQUIRE(backend.apply(*op).has_value());
        }
    std::vector<double> out;
    for (const PauliString& p : operators) out.push_back(backend.expectation(p.toQsim()).value());
    return out;
}
} // namespace

TEST_CASE("Encoder: every generator reads +1 and the logical operators read the input state") {
    for (const std::string& id : shippedCodeIds()) {
        const StabilizerCode code = shipped(id);
        std::vector<PauliString> ops = code.stabilizers;
        ops.push_back(code.logicalZ[0]);
        ops.push_back(code.logicalX[0]);
        const std::size_t m = code.stabilizers.size();
        // ⟨Z̄⟩, ⟨X̄⟩ for |0⟩, |1⟩, |+⟩, |−⟩ (spec 16 §9).
        const std::vector<std::pair<std::string, std::array<double, 2>>> inputs = {
            {"0", {1.0, 0.0}}, {"1", {-1.0, 0.0}}, {"+", {0.0, 1.0}}, {"-", {0.0, -1.0}}};
        for (const auto& [input, logical] : inputs) {
            INFO(id << " encoding |" << input << ">");
            std::vector<std::unique_ptr<qsim::IBackend>> backends;
            backends.push_back(qsim::makeBackend(qsim::Kind::Stabilizer));
            if (code.n <= 9) backends.push_back(qsim::makeBackend(qsim::Kind::StateVector));   // exact amplitudes
            for (auto& backend : backends) {
                const auto values = encodedExpectations(*backend, code, input, ops);
                for (std::size_t j = 0; j < m; ++j) REQUIRE(values[j] == Approx(1.0).margin(1e-12));
                REQUIRE(values[m] == Approx(logical[0]).margin(1e-12));
                REQUIRE(values[m + 1] == Approx(logical[1]).margin(1e-12));
            }
        }
    }
}

TEST_CASE("Encoder: the repetition encoder is the textbook pair of CNOTs") {
    const Schedule s = planEncoder(shipped("repetition_bitflip_3")).value();
    REQUIRE(s.ops.size() == 2);
    for (const Op& op : s.ops) {
        REQUIRE(op.kind == OpKind::CX);
        REQUIRE(op.a == 0);   // |ψ⟩|00⟩ → α|000⟩ + β|111⟩ (T09 §4.1)
    }
    REQUIRE(s.ops[0].b + s.ops[1].b == 3);
    // Conjugation: the encoder maps Z0 to Z̄ = Z0 and X0 to X̄ = XXX exactly.
    REQUIRE(conjugated(PauliString::parse("XII").value(), s.ops).value() == PauliString::parse("XXX").value());
    REQUIRE(conjugated(PauliString::parse("ZII").value(), s.ops).value() == PauliString::parse("ZII").value());
    REQUIRE(conjugated(PauliString::parse("IZI").value(), s.ops).value() == PauliString::parse("ZZI").value());
    // Signs are tracked: h maps Y to −Y, cx maps X⊗Z to −Y⊗Y.
    const std::vector<Op> h = {{OpKind::H, 0}}, cx = {{OpKind::CX, 0, 1}};
    REQUIRE(conjugated(PauliString::parse("Y").value(), h).value() == PauliString::parse("-Y").value());
    REQUIRE(conjugated(PauliString::parse("XZ").value(), cx).value() == PauliString::parse("-YY").value());
    const std::vector<Op> measure = {{OpKind::Measure, 0}};
    REQUIRE(conjugated(PauliString::parse("Z").value(), measure).error().code == err::NotClifford);
}

TEST_CASE("Encoder: the five-qubit code corrects every single-qubit error on the stabilizer backend") {
    // Non-CSS: the memory experiment starts from the encoder instead of a projected product state, so
    // all four checks are deterministic in round 0 (spec 16 §3 "explicit encoder for small codes").
    const StabilizerCode code = shipped("five_qubit");
    ExtractionOptions x;
    x.rounds = 1;
    x.includeLogicalPrep = false;
    x.assumeCodeStateInput = true;
    const MemoryExperiment ex = planMemoryExperiment(code, x).value();
    REQUIRE(ex.detectors.size() == 4);
    const Schedule encoder = planEncoder(code).value();
    Schedule full = ex.schedule;
    full.ops.insert(full.ops.begin(), encoder.ops.begin(), encoder.ops.end());
    const auto sampler = TableauSampler::create(full).value();
    auto lookup = LookupDecoder::create(code).value();
    core::Random rng(515);
    std::vector<std::uint8_t> bits;
    REQUIRE(sampler.run({}, rng, bits).has_value());
    REQUIRE(ex.detectionEvents(bits) == std::vector<std::uint8_t>(4, 0));
    REQUIRE(ex.observableValues(bits) == std::vector<std::uint8_t>{0});
    for (std::uint32_t q = 0; q < code.n; ++q)
        for (char letter : {'X', 'Y', 'Z'}) {
            FaultEvent f;
            f.afterOp = static_cast<std::uint32_t>(encoder.ops.size() - 1);   // right after encoding
            f.qubitA = q;
            f.pauliA = letter;
            REQUIRE(sampler.run(std::span<const FaultEvent>(&f, 1), rng, bits).has_value());
            const auto events = ex.detectionEvents(bits);
            INFO(letter << " on qubit " << q);
            REQUIRE(events == syndromeOf(code.stabilizers, PauliString::single(5, q, letter)));   // perfect code: all distinct
            const auto correction = lookup.decode(SyndromeLattice::foldLayers(ex, events)).value();
            REQUIRE(correction.pauli == PauliString::single(5, q, letter));
            const std::uint8_t predicted = correction.pauli.commutesWith(code.logicalZ[0]) ? 0 : 1;
            REQUIRE((ex.observableValues(bits)[0] ^ predicted) == 0);   // corrected readout: still |0̄⟩
        }
}

TEST_CASE("Logical gates: transversal gates exist exactly where the theory says") {
    // Pauli gates: every code. X̄ anticommutes with Z̄: conjugation flips its sign.
    for (const std::string& id : shippedCodeIds()) {
        const StabilizerCode code = shipped(id);
        const Schedule xbar = planLogicalGate(code, LogicalGate::X).value();
        REQUIRE(xbar.ops.size() == code.logicalX[0].weight());
        REQUIRE(conjugated(code.logicalZ[0], xbar.ops).value() == PauliString::parse("-" + code.logicalZ[0].str()).value());
        REQUIRE(buildLogicalGate(code, LogicalGate::Z).has_value());
    }
    // T09 §4.4: the Steane code has transversal H, S (as sdg⊗7) and CNOT.
    const StabilizerCode steane = shipped("steane_7");
    const Schedule h = planLogicalGate(steane, LogicalGate::H).value();
    REQUIRE(h.ops.size() == 7);
    const Schedule s = planLogicalGate(steane, LogicalGate::S).value();
    REQUIRE(s.ops.size() == 7);
    for (const Op& op : s.ops) REQUIRE(op.kind == OpKind::Sdg);
    // S̄ X̄ S̄† = Ȳ = i X̄ Z̄ = −Y⊗7.
    REQUIRE(conjugated(steane.logicalX[0], s.ops).value() == PauliString::parse("-YYYYYYY").value());
    const auto cnot = buildLogicalGate(steane, LogicalGate::CNOT).value();
    REQUIRE(cnot.qubitCount() == 14);
    REQUIRE(cnot.gateCounts().at("cx") == 7);
    // T09 §4.5 / §8.1: no transversal H on the five-qubit code, none on the surface or Shor codes;
    // no transversal CNOT on a non-CSS code.
    for (const char* id : {"five_qubit", "surface_rot_3", "shor_9", "repetition_phaseflip_3"}) {
        INFO(id);
        REQUIRE(planLogicalGate(shipped(id), LogicalGate::H).error().code == err::NoGate);
        REQUIRE(planLogicalGate(shipped(id), LogicalGate::S).error().code == err::NoGate);
    }
    // Bit-flip repetition code: s⊗3 |111⟩ = i³ |111⟩ = −i |111⟩ is S̄†, so S̄ = sdg⊗3; no H.
    const Schedule repetitionS = planLogicalGate(shipped("repetition_bitflip_3"), LogicalGate::S).value();
    REQUIRE(repetitionS.ops == std::vector<Op>{{OpKind::Sdg, 0}, {OpKind::Sdg, 1}, {OpKind::Sdg, 2}});
    REQUIRE(planLogicalGate(shipped("repetition_bitflip_3"), LogicalGate::H).error().code == err::NoGate);
    REQUIRE(planLogicalGate(shipped("five_qubit"), LogicalGate::CNOT).error().code == err::NoGate);
    // CSS codes: qubit-wise cx. For Shor's labelling (X̄ = Z⊗9) the physical direction is reversed.
    const Schedule surface = planLogicalGate(shipped("surface_rot_3"), LogicalGate::CNOT).value();
    REQUIRE(surface.ops.front() == Op{OpKind::CX, 0, 9});
    const Schedule shor = planLogicalGate(shipped("shor_9"), LogicalGate::CNOT).value();
    REQUIRE(shor.ops.front() == Op{OpKind::CX, 9, 0});
}
