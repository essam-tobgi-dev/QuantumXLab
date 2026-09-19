// Spec 12 §8, 13 §7, 15 §4 — probes declared by `pragma qlab.probe`: their operands resolve to
// device qubits, and their values are computed on the worker at every snapshot boundary.
#include "Data/Fidelity.hpp"
#include "RuntimeTestUtil.hpp"
#include <cmath>

using namespace rtest;
using Catch::Approx;

namespace {
const ProbeValue* probeOf(const RunSnapshot& s, std::string_view kind) {
    for (const ProbeValue& p : s.probes)
        if (p.kind == kind)
            return &p;
    return nullptr;
}
} // namespace

TEST_CASE("bloch and entanglement probes follow the state through a Bell circuit") {
    Lab& l = lab("sc_fixed_5");
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 8;
    options.seed = 6;
    options.cadence = SnapshotCadence::Gate;
    const RunResult r =
        l.run(source("pragma qlab.probe bloch q[0] q[1]\n"
                     "pragma qlab.probe entanglement q[0]\n"
                     "qubit[2] q;\nbit[2] c;\nh q[0];\ncx q[0], q[1];\nc = measure q;\n"),
              options);
    REQUIRE(r.snapshots.size() >= 2);
    for (const RunSnapshot& s : r.snapshots) {
        REQUIRE(probeOf(s, "bloch") != nullptr);
        REQUIRE(probeOf(s, "bloch")->values.size() == 6); // x, y, z per qubit
        REQUIRE(probeOf(s, "bloch")->qubits.size() == 2);
        REQUIRE(probeOf(s, "entanglement") != nullptr);
        REQUIRE(probeOf(s, "entanglement")->values.size() == 2); // entropy, purity
        REQUIRE(probeOf(s, "entanglement")->cls == data::FidelityClass::Exact);
    }
    // Start: both qubits at the north pole (|0⟩ is the ground state, README).
    const ProbeValue& first = *probeOf(r.snapshots.front(), "bloch");
    REQUIRE(first.values[2] == Approx(1.0).margin(1e-12));
    REQUIRE(first.values[5] == Approx(1.0).margin(1e-12));
    REQUIRE(probeOf(r.snapshots.front(), "entanglement")->values[0] == Approx(0.0).margin(1e-12));

    // End: the Bell state has a maximally mixed single-qubit marginal, entropy 1 bit, purity 1/2.
    const ProbeValue& last = *probeOf(r.snapshots.back(), "bloch");
    for (double v : last.values)
        REQUIRE(std::abs(v) < 1e-9);
    const ProbeValue& ent = *probeOf(r.snapshots.back(), "entanglement");
    REQUIRE(ent.values[0] == Approx(1.0).margin(1e-9));
    REQUIRE(ent.values[1] == Approx(0.5).margin(1e-9));
}

TEST_CASE(
    "a density probe returns the reduced density matrix, and $-operands name physical qubits") {
    Lab& l = lab("sc_fixed_5");
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 4;
    options.seed = 1;
    options.cadence = SnapshotCadence::Gate;
    const RunResult r =
        l.run(source("pragma qlab.layout physical\npragma qlab.probe density $0 $1\n"
                     "pragma qlab.probe state\n"
                     "bit[2] c;\nh $0;\ncx $0, $1;\nc[0] = measure $0;\nc[1] = measure $1;\n"),
              options);
    REQUIRE(!r.snapshots.empty());
    const ProbeValue* d = probeOf(r.snapshots.back(), "density");
    REQUIRE(d != nullptr);
    REQUIRE(d->qubits == std::vector<std::uint32_t>{0, 1});
    REQUIRE(d->values.size() == 2 * 4 * 4); // 4×4 complex, real and imaginary interleaved
    // ρ of the Bell state: 1/2 on the corners, zero elsewhere.
    const auto at = [&](std::size_t i, std::size_t j) { return d->values[2 * (i * 4 + j)]; };
    REQUIRE(at(0, 0) == Approx(0.5).margin(1e-9));
    REQUIRE(at(3, 3) == Approx(0.5).margin(1e-9));
    REQUIRE(at(0, 3) == Approx(0.5).margin(1e-9));
    REQUIRE(at(1, 1) == Approx(0.0).margin(1e-9));
    for (std::size_t k = 1; k < d->values.size(); k += 2)
        REQUIRE(std::abs(d->values[k]) < 1e-9);
    // A `state` probe carries no values: the snapshot's own amplitudes are the state.
    const ProbeValue* st = probeOf(r.snapshots.back(), "state");
    REQUIRE(st != nullptr);
    REQUIRE(st->values.empty());
    REQUIRE(st->qubits.size() == 2);
}

TEST_CASE("a pulse-level run evaluates its probes on the integrated density matrix") {
    Lab& l = lab("sc_fixed_5");
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.pulseDecoherence = false;
    options.shots = 1;
    options.seed = 2;
    compiler::CompileOptions copts;
    copts.pulseLevel = true;
    const RunResult r =
        l.run(source("pragma qlab.layout physical\npragma qlab.probe bloch $0\nsx $0;\n"), options,
              copts);
    REQUIRE(r.backend == qsim::Kind::Lindblad);
    REQUIRE(r.snapshots.size() == 1);
    const ProbeValue* b = probeOf(r.snapshots.back(), "bloch");
    REQUIRE(b != nullptr);
    REQUIRE(b->cls == data::FidelityClass::Numerical);
    REQUIRE(b->values.size() == 3);
    // R_x(π/2)|0⟩ sits on −y of the Bloch sphere.
    REQUIRE(b->values[0] == Approx(0.0).margin(5e-3));
    REQUIRE(b->values[1] == Approx(-1.0).margin(5e-3));
    REQUIRE(b->values[2] == Approx(0.0).margin(5e-3));
}

TEST_CASE("probe operands accept a whole register and unknown qubits are dropped") {
    Lab& l = lab("sc_fixed_5");
    const Lab::Job job =
        l.compile(source("pragma qlab.probe bloch q\nqubit[3] q;\nbit[3] c;\n"
                         "h q[0];\ncx q[0], q[1];\ncx q[1], q[2];\nc = measure q;\n"));
    const lang::Program* program = l.session.program(job.program);
    const compiler::CompiledProgram* compiled = l.session.compiled(job.handle);
    REQUIRE(program != nullptr);
    REQUIRE(compiled != nullptr);
    const std::vector<ProbeRequest> probes = resolveProbes(*program, *compiled);
    REQUIRE(probes.size() == 1);
    REQUIRE(probes[0].kind == "bloch");
    REQUIRE(probes[0].qubits.size() == 3); // the register expands to its three qubits
    for (std::uint32_t q : probes[0].qubits)
        REQUIRE(q < l.session.device()->qubitCount());
    // They are the physical qubits the layout chose.
    const std::vector<std::uint32_t> used = usedQubits(compiled->circuit);
    REQUIRE(probes[0].qubits == used);
}
