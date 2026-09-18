// Spec 07 §2 — kernel fast paths, controlled gates, measurement, sampling, memory guard.
#include "Gates.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "Numerics/Checks.hpp"
#include "Numerics/Tensor.hpp"
#include <map>

using namespace qtest;
using Catch::Approx;

namespace {
std::vector<Complex> randomState(std::uint32_t n, std::uint64_t seed) {
    core::Random rng(seed);
    std::vector<Complex> v(std::size_t{1} << n);
    double norm = 0;
    for (auto& c : v) {
        c = Complex(rng.normal(), rng.normal());
        norm += std::norm(c);
    }
    double s = 1.0 / std::sqrt(norm);
    for (auto& c : v) c *= s;
    return v;
}
// Reference: build the full 2^n operator with num::embed and multiply.
std::vector<Complex> reference(const std::vector<Complex>& psi, const Matrix& u,
                               const std::vector<std::uint32_t>& targets, std::uint32_t n) {
    std::vector<std::size_t> t(targets.begin(), targets.end());
    Matrix full = num::embed(u, t, n);
    num::Vector out = num::matvec(full, psi);
    return std::vector<Complex>(out.begin(), out.end());
}
void requireClose(std::span<const Complex> a, const std::vector<Complex>& b, double tol = 1e-12) {
    REQUIRE(a.size() == b.size());
    double worst = 0;
    for (std::size_t i = 0; i < b.size(); ++i) worst = std::max(worst, std::abs(a[i] - b[i]));
    REQUIRE(worst < tol);
}
} // namespace

TEST_CASE("single-qubit kernels match the embedded reference") {
    const std::uint32_t n = 8;
    auto psi = randomState(n, 11);
    for (std::uint32_t t = 0; t < n; ++t) {
        for (const auto& [name, u] : std::vector<std::pair<std::string, Matrix>>{
                 {"X", X()}, {"Z", Z()}, {"H", H()}, {"S", S()}, {"T", T()}, {"SX", SX()},
                 {"RZ", RZ(0.77)}, {"RY", RY(-1.3)}, {"phase", phase(0.42)}}) {
            INFO(name << " on q" << t);
            StateVectorBackend sv;
            REQUIRE(sv.allocate(n).has_value());
            REQUIRE(sv.setAmplitudes(psi).has_value());
            REQUIRE(sv.applyGate(u, q({t})).has_value());
            requireClose(sv.amplitudes(), reference(psi, u, {t}, n));
        }
    }
}

TEST_CASE("GateClass fast paths equal the generic kernel") {
    const std::uint32_t n = 8;
    auto psi = randomState(n, 23);
    struct Case { const char* name; Matrix u; GateClass cls; std::vector<std::uint32_t> t; };
    std::vector<Case> cases{
        {"PauliX", X(), GateClass::PauliX, {3}},
        {"PauliZ", Z(), GateClass::PauliZ, {5}},
        {"Diagonal", phase(0.9), GateClass::Diagonal, {2}},
        {"Identity", I2(), GateClass::Identity, {1}},
        {"Cnot", CX(), GateClass::Cnot, {2, 6}},
        {"Cz", CZ(), GateClass::Cz, {0, 7}},
        {"Swap", SWAP(), GateClass::Swap, {1, 4}},
    };
    for (auto& c : cases) {
        INFO(c.name);
        StateVectorBackend fast, generic;
        REQUIRE(fast.allocate(n).has_value());
        REQUIRE(generic.allocate(n).has_value());
        REQUIRE(fast.setAmplitudes(psi).has_value());
        REQUIRE(generic.setAmplitudes(psi).has_value());
        GateOp op;
        op.matrix = c.u;
        op.cls = c.cls;
        for (auto x : c.t) op.targets.push_back(QubitIndex{x});
        REQUIRE(fast.apply(op).has_value());
        REQUIRE(generic.applyGate(c.u, op.targets).has_value());
        requireClose(fast.amplitudes(), std::vector<Complex>(generic.amplitudes().begin(), generic.amplitudes().end()));
    }
}

TEST_CASE("controlled application equals the embedded controlled matrix") {
    const std::uint32_t n = 6;
    auto psi = randomState(n, 31);
    StateVectorBackend sv;
    REQUIRE(sv.allocate(n).has_value());
    REQUIRE(sv.setAmplitudes(psi).has_value());
    // Controlled-RY on target 4 with controls {0, 2}: build the 8x8 reference on {0,2,4}.
    Matrix ry = RY(0.83);
    Matrix ccry(8, 8);
    for (std::size_t i = 0; i < 8; ++i) ccry(i, i) = 1.0;
    // Basis |t c2 c0> little-endian: controls are bits 0 and 1, target bit 2.
    const std::size_t c0 = 1, c1 = 2, tgt = 4;
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t j = 0; j < 8; ++j) ccry(i, j) = (i == j) ? Complex(1) : Complex(0);
    for (std::size_t a = 0; a < 2; ++a)
        for (std::size_t b = 0; b < 2; ++b) {
            std::size_t base = c0 * 0; (void)base;
            std::size_t i = (a ? c0 : 0) + (b ? c1 : 0);
            (void)i;
        }
    // Rows/cols where both controls are set: indices 3 (=0b011) and 7 (=0b111) differ in the target bit.
    ccry(3, 3) = ry(0, 0); ccry(3, 7) = ry(0, 1);
    ccry(7, 3) = ry(1, 0); ccry(7, 7) = ry(1, 1);
    REQUIRE(sv.applyControlled(ry, q({0, 2}), q({4})).has_value());
    requireClose(sv.amplitudes(), reference(psi, ccry, {0, 2, 4}, n));
    (void)tgt;
}

TEST_CASE("measurement collapses and renormalizes") {
    core::Random rng(5);
    StateVectorBackend sv;
    REQUIRE(sv.allocate(3).has_value());
    REQUIRE(sv.applyGate(H(), q({0})).has_value());
    REQUIRE(sv.applyGate(CX(), q({0, 1})).has_value());
    auto m = sv.measure(q({0}), rng);
    REQUIRE(m.has_value());
    REQUIRE(m->probability == Approx(0.5).margin(1e-12));
    REQUIRE(sv.stateNorm() == Approx(1.0).margin(1e-12));
    // q1 must agree with q0 after the Bell measurement.
    auto m2 = sv.measure(q({1}), rng);
    REQUIRE(m2.has_value());
    REQUIRE(m2->bits[0] == m->bits[0]);
    REQUIRE(m2->probability == Approx(1.0).margin(1e-12));
}

TEST_CASE("sampling reproduces the Born distribution") {
    core::Random rng(99);
    StateVectorBackend sv;
    REQUIRE(sv.allocate(2).has_value());
    REQUIRE(sv.applyGate(RY(2.0 * std::acos(std::sqrt(0.3))), q({0})).has_value()); // P(1) = 0.7
    auto counts = sv.sample(q({0}), 200000, rng);
    REQUIRE(counts.has_value());
    double p1 = static_cast<double>((*counts)["1"]) / 200000.0;
    REQUIRE(p1 == Approx(0.7).margin(0.005));
    REQUIRE(sv.stateNorm() == Approx(1.0).margin(1e-12)); // sampling does not collapse
}

TEST_CASE("memory guard refuses an impossible allocation") {
    StateVectorBackend sv;
    auto r = sv.allocate(60);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == err::TooLarge);
    REQUIRE(StateVectorBackend::maxQubits() >= 20);
}

TEST_CASE("reduced states, entropy, concurrence and Schmidt coefficients") {
    StateVectorBackend sv;
    REQUIRE(sv.allocate(2).has_value());
    REQUIRE(sv.applyGate(H(), q({0})).has_value());
    REQUIRE(sv.applyGate(CX(), q({0, 1})).has_value());
    auto rho = sv.reducedDensityMatrix(q({0}));
    REQUIRE(rho.has_value());
    REQUIRE(std::abs((*rho)(0, 0) - Complex(0.5)) < 1e-12);
    REQUIRE(std::abs((*rho)(1, 1) - Complex(0.5)) < 1e-12);
    REQUIRE(std::abs((*rho)(0, 1)) < 1e-12);
    REQUIRE(measures::entropyBits(*rho) == Approx(1.0).margin(1e-10));
    REQUIRE(measures::purity(*rho) == Approx(0.5).margin(1e-12));
    auto bloch = measures::blochVector(*rho);
    REQUIRE(bloch.has_value());
    for (double c : *bloch) REQUIRE(std::abs(c) < 1e-12);
    // Concurrence of a Bell state is 1.
    std::vector<Complex> bell(sv.amplitudes().begin(), sv.amplitudes().end());
    Matrix full = num::projector(bell);
    auto conc = measures::concurrence(full);
    REQUIRE(conc.has_value());
    REQUIRE(*conc == Approx(1.0).margin(1e-8));
    auto sch = measures::schmidtCoefficients(bell, 2, q({0}));
    REQUIRE(sch.has_value());
    REQUIRE(sch->size() == 2);
    REQUIRE((*sch)[0] == Approx(1.0 / std::sqrt(2.0)).margin(1e-12));
    REQUIRE((*sch)[1] == Approx(1.0 / std::sqrt(2.0)).margin(1e-12));
    // A product state has zero entropy and unit leading Schmidt coefficient.
    StateVectorBackend prod;
    REQUIRE(prod.allocate(2).has_value());
    REQUIRE(prod.applyGate(H(), q({0})).has_value());
    auto rhoP = prod.reducedDensityMatrix(q({0}));
    REQUIRE(rhoP.has_value());
    REQUIRE(measures::entropyBits(*rhoP) == Approx(0.0).margin(1e-10));
    auto concP = measures::concurrence(num::projector(std::vector<Complex>(prod.amplitudes().begin(), prod.amplitudes().end())));
    REQUIRE(concP.has_value());
    REQUIRE(*concP == Approx(0.0).margin(1e-8));
}
