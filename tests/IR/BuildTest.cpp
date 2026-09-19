// Spec 14 §2–§3, 25 §3.1 — circuits built from source: node counts, depth, layers, register-to-wire
// mapping, spans, input binding, durations, and the QFT unitary oracle.
#include "IrTestUtil.hpp"
#include <cmath>
#include <complex>
#include <numbers>

using namespace qlab;
using namespace irtest;

namespace {
std::vector<std::size_t> layerSizes(const ir::Circuit& c) {
    std::vector<std::size_t> v;
    for (const auto& l : c.layers())
        v.push_back(l.size());
    return v;
}
num::Vector applyToZero(const num::Matrix& u) {
    num::Vector psi(u.rows, num::Complex{});
    psi[0] = 1.0;
    return num::matvec(u, psi);
}
} // namespace

TEST_CASE("Bell circuit from source: nodes, little-endian measurement, depth, layers, state") {
    const auto c =
        build(program("qubit[2] q;\nbit[2] c;\nh q[0];\ncx q[0], q[1];\nc = measure q;\n"));
    CHECK(c.qubitCount() == 2);
    CHECK(c.clbitCount() == 2);
    CHECK(c.nodeCount() == 4);
    CHECK(c.size() == 4);
    CHECK(c.gateCounts() ==
          std::map<std::string, std::size_t>{{"cx", 1}, {"h", 1}, {"measure", 2}});
    CHECK(gateAt(c, 0).name == "h");
    CHECK(indices(gateAt(c, 1).targets) == std::vector<std::uint32_t>{0, 1});
    const auto ns = nodes(c);
    const auto& m0 = std::get<ir::Measure>(*ns[2]);
    const auto& m1 = std::get<ir::Measure>(*ns[3]);
    CHECK((m0.qubit.index == 0 && m0.bit.index == 0)); // q[i] -> c[i]
    CHECK((m1.qubit.index == 1 && m1.bit.index == 1));
    CHECK(c.depth() == 3);
    CHECK(layerSizes(c) == std::vector<std::size_t>{1, 1, 2});
    CHECK(c.twoQubitCount() == 1);
    // Spec 25 §3.1: H0 CX01 |00> = (1, 0, 0, 1)/√2 to 1e-14.
    const auto psi = applyToZero(unitary(build(program("qubit[2] q;\nh q[0];\ncx q[0], q[1];\n"))));
    const double r = 1.0 / std::sqrt(2.0);
    CHECK(std::abs(psi[0] - r) < 1e-14);
    CHECK(std::abs(psi[1]) < 1e-14);
    CHECK(std::abs(psi[2]) < 1e-14);
    CHECK(std::abs(psi[3] - r) < 1e-14);
}

TEST_CASE("GHZ-5 from source: chain of cx, ASAP measurement layers, GHZ amplitudes") {
    const std::string body =
        "qubit[5] q;\nbit[5] c;\nh q[0];\nfor int i in [0:3] { cx q[i], q[i+1]; }\n";
    const auto c = build(program(body + "c = measure q;\n"));
    CHECK(c.nodeCount() == 10);
    CHECK(c.twoQubitCount() == 4);
    for (std::size_t i = 0; i < 4; ++i)
        CHECK(indices(gateAt(c, 1 + i).targets) ==
              std::vector<std::uint32_t>{static_cast<std::uint32_t>(i),
                                         static_cast<std::uint32_t>(i + 1)});
    // Measurements join the moment right after the last gate on their wire.
    CHECK(layerSizes(c) == std::vector<std::size_t>{1, 1, 2, 2, 2, 2});
    CHECK(c.depth() == 6);
    const auto psi = applyToZero(unitary(build(program(body))));
    CHECK(std::abs(std::norm(psi[0]) - 0.5) < 1e-14);
    CHECK(std::abs(std::norm(psi[31]) - 0.5) < 1e-14);
    double rest = 0;
    for (std::size_t i = 1; i < 31; ++i)
        rest += std::norm(psi[i]);
    CHECK(rest < 1e-14);
}

TEST_CASE(
    "QFT-4 from source: 12 gates, depth 8, layers, unitary equals the discrete Fourier transform") {
    const auto c = build(program(R"(qubit[4] q;
for int i in [3:-1:0] {
  h q[i];
  for int j in [i-1:-1:0] {
    cp(pi / (1 << (i - j))) q[j], q[i];
  }
}
swap q[0], q[3];
swap q[1], q[2];
)"));
    CHECK(c.nodeCount() == 12);
    CHECK(c.gateCounts() == std::map<std::string, std::size_t>{{"cp", 6}, {"h", 4}, {"swap", 2}});
    const auto& first = gateAt(c, 1); // cp(π/2) q[2], q[3]
    CHECK(first.name == "cp");
    CHECK(indices(first.targets) == std::vector<std::uint32_t>{2, 3});
    CHECK(std::abs(first.params[0] - std::numbers::pi / 2) < 1e-15);
    CHECK(c.depth() == 8);
    CHECK(layerSizes(c) == std::vector<std::size_t>{1, 1, 2, 2, 2, 1, 2, 1});
    // Spec 25 §3.1: F_jk = 2^{-n/2} e^{2πi jk/2^n}, compared up to global phase (1e-12).
    const auto u = unitary(c);
    num::Matrix f(16, 16);
    for (std::size_t j = 0; j < 16; ++j)
        for (std::size_t k = 0; k < 16; ++k)
            f(j, k) =
                0.25 * std::polar(1.0, 2 * std::numbers::pi * static_cast<double>(j * k) / 16.0);
    CHECK(num::equalUpToGlobalPhase(u, f, 1e-12));
    CHECK(num::isUnitary(u.view(), 1e-12));
}

TEST_CASE("registers map to wires and bits in declaration order, little-endian within a register") {
    const auto c = build(program(R"(qubit[2] a;
qubit b;
qubit[3] r;
bit[2] m;
bit n;
x r[1];
h r;
cx a, r[0:1];
n = measure b;
m = measure a;
)"));
    REQUIRE(c.qubitRegisters().size() == 3);
    CHECK((c.qubitRegisters()[1].first == 2 && c.qubitRegisters()[1].scalar));
    CHECK((c.qubitRegisters()[2].first == 3 && c.qubitRegisters()[2].size == 3));
    REQUIRE(c.bitRegisters().size() == 2);
    CHECK((c.bitRegisters()[1].name == "n" && c.bitRegisters()[1].first == 2 &&
           c.bitRegisters()[1].scalar));
    CHECK(indices(gateAt(c, 0).targets) == std::vector<std::uint32_t>{4});
    for (std::uint32_t i = 0; i < 3; ++i)
        CHECK(indices(gateAt(c, 1 + i).targets) == std::vector<std::uint32_t>{3 + i});
    CHECK(indices(gateAt(c, 4).targets) == std::vector<std::uint32_t>{0, 3});
    CHECK(indices(gateAt(c, 5).targets) == std::vector<std::uint32_t>{1, 4});
    const auto ns = nodes(c);
    CHECK((std::get<ir::Measure>(*ns[6]).qubit.index == 2 &&
           std::get<ir::Measure>(*ns[6]).bit.index == 2));
    CHECK((std::get<ir::Measure>(*ns[8]).qubit.index == 1 &&
           std::get<ir::Measure>(*ns[8]).bit.index == 1));
    CHECK(c.wireName(ir::Wire{2}) == "b");
    CHECK(c.bitName(ir::ClassicalBit{1}) == "m[1]");
}

TEST_CASE("every node carries the span of its statement; expanded calls keep the call site") {
    const auto c = build(program(R"(gate pair a, b { h a; cx a, b; }
qubit[3] q;
bit[3] c;
pair q[0], q[1];
for int i in [0:1] {
  x q[i];
}
c[0] = measure q[0];
if (c[0] == 1) {
  z q[2];
}
)"));
    // Lines: 3 gate, 4 qubit, 5 bit, 6 pair call, 7 for, 8 x, 10 measure, 11 if, 12 z.
    const auto ns = nodes(c);
    REQUIRE(ns.size() == 6);
    const std::vector<std::uint32_t> lines = {6, 6, 8, 8, 10, 11};
    for (std::size_t i = 0; i < ns.size(); ++i) {
        INFO("node " << i);
        CHECK(ir::nodeSpan(*ns[i]).line == lines[i]);
        CHECK(ir::nodeSpan(*ns[i]).file == "test.qasm");
    }
    const auto& br = std::get<ir::Branch>(*ns[5]);
    CHECK(ir::nodeSpan((*br.thenBody).node((*br.thenBody).topologicalOrder()[0])).line == 12);
}

TEST_CASE("inputs bind from ParamMap, fall back to defaults, and are QL4011 when unbound") {
    const std::string src = program(
        "input float theta = 0.5;\ninput float phi;\nqubit q;\nrz(theta) q;\nrx(2 * phi) q;\n");
    const auto c = build(src, {{"phi", 0.25}});
    REQUIRE(c.nodeCount() == 2);
    CHECK(gateAt(c, 0).params == std::vector<double>{0.5});
    CHECK(gateAt(c, 1).params == std::vector<double>{0.5});
    const auto bound = build(src, {{"phi", -1.0}, {"theta", 1.25}});
    REQUIRE(bound.nodeCount() == 2);
    CHECK(gateAt(bound, 0).params == std::vector<double>{1.25});
    CHECK(gateAt(bound, 1).params == std::vector<double>{-2.0});
    auto unbound = tryBuild(src);
    REQUIRE_FALSE(unbound.has_value());
    CHECK(unbound.error().diagnosticId == "QL4011");
    CHECK(unbound.error().code == ErrorCode::Compiler_ + 11);
    // A build-time `if` on an input folds away (spec 14 §2).
    const auto folded =
        build(program("input int prep = 0;\nqubit q;\nif (prep == 1) { x q; } else { h q; }\n"),
              {{"prep", 1}});
    REQUIRE(folded.nodeCount() == 1);
    CHECK(gateAt(folded, 0).name == "x");
}

TEST_CASE("delay and box durations are integer picoseconds plus a symbolic dt count") {
    const auto c = build(program(R"(qubit[2] q;
delay[100ns] q[0];
delay[4dt] q;
delay[1.5ns + 2dt];
box[1ms] { x q[0]; }
barrier;
)"));
    const auto ns = nodes(c);
    REQUIRE(ns.size() == 5);
    const auto& d0 = std::get<ir::Delay>(*ns[0]);
    CHECK((d0.duration.ps.get() == 100000 && d0.duration.dt == 0 &&
           indices(d0.wires) == std::vector<std::uint32_t>{0}));
    const auto& d1 = std::get<ir::Delay>(*ns[1]);
    CHECK((d1.duration.ps.get() == 0 && d1.duration.dt == 4 && d1.duration.symbolic()));
    CHECK(indices(d1.wires) == std::vector<std::uint32_t>{0, 1});
    const auto& d2 = std::get<ir::Delay>(*ns[2]);
    CHECK((d2.duration.ps.get() == 1500 && d2.duration.dt == 2 && d2.wires.empty()));
    CHECK(d2.duration.resolve(Picoseconds{222}).get() == 1944);
    const auto& box = std::get<ir::Box>(*ns[3]);
    REQUIRE(box.duration.has_value());
    CHECK(box.duration->ps.get() == 1000000000);
    CHECK((*box.body).nodeCount() == 1);
    CHECK(std::get<ir::Barrier>(*ns[4]).wires.empty());
    CHECK(c.depth() == 1); // directives do not add moments
    CHECK(c.onWire(ir::Wire{1}).size() ==
          3); // delay[4dt] q, the all-wire delay, the all-wire barrier
}
