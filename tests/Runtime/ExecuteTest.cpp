// Spec 15 §3–§4, spec 25 §5 — the three execution models on the shipped devices: terminal Born
// sampling (Bell, GHZ-5), per-shot execution with feedforward (teleportation), determinism, and the
// classical memory layout of §4.
#include "RuntimeTestUtil.hpp"
#include "Data/Fidelity.hpp"
#include <cmath>

using namespace rtest;
using Catch::Approx;

TEST_CASE("Bell on the state vector gives only 00 and 11, each 1/2 within 5 sigma") {
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 4096;
    options.seed = 12345;
    const RunResult r = lab("sc_heavyhex_27").run(readAsset("Programs/Examples/Basics/bell.qasm"), options);

    REQUIRE(r.backend == qsim::Kind::StateVector);
    REQUIRE(r.shotsCompleted == 4096);
    REQUIRE(r.counts.total() == 4096);
    REQUIRE(r.counts.distinct() == 2);
    const double s = sigma(0.5, 4096);
    REQUIRE(std::abs(r.probability("00") - 0.5) < 5.0 * s);
    REQUIRE(std::abs(r.probability("11") - 0.5) < 5.0 * s);
    REQUIRE(r.counts.count("01") == 0);
    REQUIRE(r.counts.count("10") == 0);
    // Model (a) holds the whole distribution, so the exact Born probabilities are stored too.
    REQUIRE(r.exact.has_value());
    REQUIRE((*r.exact)[0] == Approx(0.5).margin(1e-12));
    REQUIRE((*r.exact)[3] == Approx(0.5).margin(1e-12));
    REQUIRE(r.exactClass == data::FidelityClass::Exact);
    // ⟨Z0⟩ = ⟨Z1⟩ = 0, ⟨Z0Z1⟩ = 1 on the two physical qubits the layout chose.
    const std::uint32_t a = r.qubits.front(), b = r.qubits.back();
    const Expectation* zz = find(r, std::format("Z{}Z{}", std::min(a, b), std::max(a, b)));
    REQUIRE(zz != nullptr);
    REQUIRE(zz->value == Approx(1.0).margin(1e-12));
    REQUIRE(zz->exact.has_value());
    REQUIRE(*zz->exact == Approx(1.0).margin(1e-12));
    const Expectation* z0 = find(r, std::format("Z{}", a));
    REQUIRE(z0 != nullptr);
    REQUIRE(std::abs(z0->value) < 5.0 * z0->stderr_ + 1e-12);
    REQUIRE(z0->cls == data::FidelityClass::Statistical);
    REQUIRE(r.marginals.size() == 2);
    for (const Marginal& m : r.marginals) REQUIRE(std::abs(m.p1 - 0.5) < 5.0 * s);
}

TEST_CASE("GHZ-5 produces only 00000 and 11111") {
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 2048;
    options.seed = 7;
    const RunResult r = lab("sc_heavyhex_27").run(readAsset("Programs/Examples/Basics/ghz_5.qasm"), options);
    REQUIRE(r.counts.distinct() == 2);
    const double s = sigma(0.5, 2048);
    REQUIRE(std::abs(r.probability("00000") - 0.5) < 5.0 * s);
    REQUIRE(std::abs(r.probability("11111") - 0.5) < 5.0 * s);
    REQUIRE(r.qubits.size() == 5);
    REQUIRE(r.layout.bits == 5);
    // Every measured pair of adjacent qubits is perfectly correlated.
    std::size_t pairs = 0;
    for (const Expectation& e : r.expectations)
        if (e.observable.find('Z') != e.observable.rfind('Z')) {
            REQUIRE(e.value == Approx(1.0).margin(1e-12));
            ++pairs;
        }
    REQUIRE(pairs >= 1);
}

TEST_CASE("teleportation runs per shot and reproduces the input state statistics") {
    // ry(0.7)|0> teleported to q[2]: P(out = 1) = sin^2(0.35) (Assets/.../teleportation.qasm).
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 8192;
    options.seed = 424242;
    const RunResult r = lab("sc_heavyhex_27").run(readAsset("Programs/Examples/Protocols/teleportation.qasm"), options);

    REQUIRE(r.memory.size() == 8192);
    const RegisterInfo* out = r.layout.find("out");
    REQUIRE(out != nullptr);
    std::uint64_t ones = 0;
    for (const ShotRecord& s : r.memory) ones += s.bits[out->first] != 0;
    const double p = static_cast<double>(ones) / 8192.0;
    const double expected = std::sin(0.35) * std::sin(0.35);
    INFO("P(out = 1) = " << p << ", expected " << expected);
    REQUIRE(std::abs(p - expected) < 5.0 * sigma(expected, 8192));
    // The two Bell-measurement bits are uniform over the four branches: every arm must be taken.
    std::array<std::uint64_t, 4> arms{};
    const RegisterInfo* m = r.layout.find("m");
    REQUIRE(m != nullptr);
    for (const ShotRecord& s : r.memory) ++arms[s.bits[m->first] + 2u * s.bits[m->first + 1]];
    for (std::uint64_t n : arms) REQUIRE(std::abs(static_cast<double>(n) / 8192.0 - 0.25) < 5.0 * sigma(0.25, 8192));
}

TEST_CASE("the same seed reproduces bit-identical memory, a different seed does not") {
    RunOptions options;
    options.noise = NoiseSource::Calibrated;
    options.shots = 512;
    options.seed = 99;
    Lab& sc = lab("sc_fixed_5");
    const std::string text = readAsset("Programs/Examples/Protocols/teleportation.qasm");
    const RunResult a = sc.run(text, options);
    const RunResult b = sc.run(text, options);
    REQUIRE(a.seed == 99);
    REQUIRE(a.memory.size() == b.memory.size());
    for (std::size_t s = 0; s < a.memory.size(); ++s) REQUIRE(a.memory[s].bits == b.memory[s].bits);

    options.seed = 100;
    const RunResult c = sc.run(text, options);
    bool differs = false;
    for (std::size_t s = 0; s < a.memory.size() && !differs; ++s) differs = a.memory[s].bits != c.memory[s].bits;
    REQUIRE(differs);
    // Terminal-measurement runs are deterministic in the same way (model (a) materialises in key order).
    RunOptions ideal = options;
    ideal.noise = NoiseSource::Ideal;
    ideal.seed = 5;
    const std::string bell = readAsset("Programs/Examples/Basics/bell.qasm");
    REQUIRE(sc.run(bell, ideal).memory == sc.run(bell, ideal).memory);
}

TEST_CASE("the classical layout is little-endian per register and the key is printed MSB first") {
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 64;
    options.seed = 3;
    // c[0] = 1, c[1] = 0, c[2] = 1 -> the key prints c[2]c[1]c[0] = "101".
    const RunResult r = lab("sc_fixed_5").run(source("qubit[3] q;\nbit[3] c;\nx q[0];\nx q[2];\nc = measure q;\n"),
                                              options);
    REQUIRE(r.counts.distinct() == 1);
    REQUIRE(r.counts.count("101") == 64);
    const RegisterInfo* c = r.layout.find("c");
    REQUIRE(c != nullptr);
    REQUIRE(c->size == 3);
    REQUIRE(r.layout.value(r.memory.front().bits, *c) == 5);
}

TEST_CASE("a runtime loop that hits its iteration bound truncates the shot with QL5020") {
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 8;
    options.seed = 1;
    options.maxLoopIterations = 4;
    // `c` never changes inside the body, so the condition stays true and the bound applies.
    const RunResult r = lab("sc_fixed_5").run(
        source("qubit[1] q;\nbit c;\nbit d;\nx q[0];\nc = measure q[0];\nwhile (c == 1) { x q[0]; d = c; }\n"), options);
    REQUIRE(r.memory.size() == 8);
    for (const ShotRecord& s : r.memory) REQUIRE(s.truncated);
    bool warned = false;
    for (const lang::Diagnostic& d : r.diagnostics) warned = warned || d.id() == "QL5020";
    REQUIRE(warned);
}

TEST_CASE("snapshots are published at the configured cadence with the schedule time of the boundary") {
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 16;
    options.seed = 2;
    options.cadence = SnapshotCadence::Gate;
    const RunResult r = lab("sc_fixed_5").run(readAsset("Programs/Examples/Basics/bell.qasm"), options);
    REQUIRE(r.snapshots.size() >= 2);
    double last = -1.0;
    for (const RunSnapshot& s : r.snapshots) {
        REQUIRE(s.state != nullptr);
        REQUIRE(s.state->probabilities.has_value());
        REQUIRE(s.timeS >= last);
        last = s.timeS;
    }
    REQUIRE(r.snapshots.back().timeS > 0.0); // the compiled circuit is timed
    REQUIRE(r.finalState != nullptr);
    REQUIRE(r.finalState->amplitudes.has_value());
    // Above 20 qubits the cadence drops to barriers (spec 15 §4).
    REQUIRE(resolveCadence(SnapshotCadence::Gate, 21) == SnapshotCadence::Barrier);
    REQUIRE(resolveCadence(SnapshotCadence::Gate, 20) == SnapshotCadence::Gate);
}
