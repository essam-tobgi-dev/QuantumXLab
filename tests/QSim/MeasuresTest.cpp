// Spec 07 §8, spec 21 §2 — entanglement and state measures against closed forms (T01 §5, §7–§8).
#include "Circuits.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "Numerics/Checks.hpp"

using namespace qtest;
using Catch::Approx;

namespace {
std::vector<Complex> amplitudes(const StateVectorBackend& sv) { return {sv.amplitudes().begin(), sv.amplitudes().end()}; }

StateVectorBackend prepared(std::uint32_t n, const Circuit& c) {
    StateVectorBackend sv;
    REQUIRE(sv.allocate(n).has_value());
    REQUIRE(applyAll(sv, c).has_value());
    return sv;
}
Circuit bellCircuit() { return {{"h", H(), q({0}), {}}, {"cx", CX(), q({0, 1}), {}}}; }
Circuit ghz3Circuit() { return {{"h", H(), q({0}), {}}, {"cx", CX(), q({0, 1}), {}}, {"cx", CX(), q({1, 2}), {}}}; }

std::vector<Complex> randomPure(std::size_t dim, core::Random& rng) {
    std::vector<Complex> v(dim);
    double norm = 0;
    for (auto& c : v) { c = Complex(rng.normal(), rng.normal()); norm += std::norm(c); }
    for (auto& c : v) c /= std::sqrt(norm);
    return v;
}
} // namespace

TEST_CASE("Measures: von Neumann entropy and mutual information") {
    auto bell = prepared(2, bellCircuit());
    auto half = bell.reducedDensityMatrix(q({1}));
    REQUIRE(half.has_value());
    REQUIRE(measures::entropyBits(*half) == Approx(1.0).margin(1e-12));
    REQUIRE(measures::entropyBits(num::projector(amplitudes(bell))) == Approx(0.0).margin(1e-12));
    // Bell pair: I(A:B) = 1 + 1 − 0 = 2 bits, the maximum for two qubits (spec 21 §2.2).
    auto rhoA = bell.reducedDensityMatrix(q({0}));
    auto rhoAB = bell.reducedDensityMatrix(q({0, 1}));
    REQUIRE(measures::mutualInformation(*rhoA, *half, *rhoAB) == Approx(2.0).margin(1e-12));
    // Product state: every entropy vanishes.
    auto prod = prepared(2, {{"ry", RY(0.9), q({0}), {}}, {"rx", RX(-2.1), q({1}), {}}});
    auto p0 = prod.reducedDensityMatrix(q({0}));
    auto p1 = prod.reducedDensityMatrix(q({1}));
    REQUIRE(measures::entropyBits(*p0) == Approx(0.0).margin(1e-12));
    REQUIRE(measures::mutualInformation(*p0, *p1, *prod.reducedDensityMatrix(q({0, 1}))) == Approx(0.0).margin(1e-12));
    // GHZ pair (q0, q1): classically correlated, S = 1 each and 1 jointly, so I = 1 bit.
    auto ghz = prepared(3, ghz3Circuit());
    REQUIRE(measures::mutualInformation(*ghz.reducedDensityMatrix(q({0})), *ghz.reducedDensityMatrix(q({1})),
                                        *ghz.reducedDensityMatrix(q({0, 1}))) == Approx(1.0).margin(1e-12));
    // Maximally mixed pair: S = 2 bits, purity 1/4.
    const Matrix mixed = num::scale(Matrix::identity(4), 0.25);
    REQUIRE(measures::entropyBits(mixed) == Approx(2.0).margin(1e-12));
    REQUIRE(measures::purity(mixed) == Approx(0.25).margin(1e-15));
}

TEST_CASE("Measures: concurrence of Bell, product, general pure and Werner states") {
    // All four Bell states have C = 1 (T01 §8).
    const Circuit bell = bellCircuit();
    for (const auto& [name, extra] : std::vector<std::pair<const char*, Matrix>>{{"Phi+", I2()}, {"Phi-", Z()}, {"Psi+", X()}, {"Psi-", Y()}}) {
        INFO(name);
        Circuit c = bell;
        c.push_back({name, extra, q({0}), {}});
        auto conc = measures::concurrence(num::projector(amplitudes(prepared(2, c))));
        REQUIRE(conc.has_value());
        REQUIRE(*conc == Approx(1.0).margin(1e-7));
    }
    StateVectorBackend zero;
    REQUIRE(zero.allocate(2).has_value());
    REQUIRE(measures::concurrence(num::projector(amplitudes(zero))).value() == Approx(0.0).margin(1e-7));
    // General pure state a|00⟩ + b|01⟩ + c|10⟩ + d|11⟩: C = 2|ad − bc|. The eigenvalue square roots of
    // the rank-one ρρ̃ amplify round-off to ~1e-8, hence the tolerance.
    core::Random rng(8128);
    for (int trial = 0; trial < 20; ++trial) {
        const auto psi = randomPure(4, rng);
        const double expected = 2.0 * std::abs(psi[0] * psi[3] - psi[1] * psi[2]);
        INFO("trial " << trial << " expected C = " << expected);
        REQUIRE(measures::concurrence(num::projector(psi)).value() == Approx(expected).margin(1e-7));
    }
    // Werner state p|Φ+⟩⟨Φ+| + (1 − p) I/4: C = max(0, (3p − 1)/2).
    const Matrix phi = num::projector(amplitudes(prepared(2, bell)));
    for (double p : {0.0, 0.2, 1.0 / 3.0, 0.5, 0.8, 1.0}) {
        INFO("p = " << p);
        Matrix werner = num::add(num::scale(phi, p), num::scale(Matrix::identity(4), 0.25 * (1.0 - p)));
        REQUIRE(measures::concurrence(werner).value() == Approx(std::max(0.0, 1.5 * p - 0.5)).margin(1e-7));
    }
    REQUIRE(measures::concurrence(Matrix::identity(2)).error().code == err::BadTargets);
}

TEST_CASE("Measures: Schmidt coefficients") {
    const double s = 1.0 / std::sqrt(2.0);
    const auto ghz = amplitudes(prepared(3, ghz3Circuit()));
    for (const auto& cut : {q({0}), q({1}), q({0, 2}), q({2, 1})}) {
        auto sch = measures::schmidtCoefficients(ghz, 3, cut);
        REQUIRE(sch.has_value());
        REQUIRE(sch->size() == 2);
        REQUIRE((*sch)[0] == Approx(s).margin(1e-12));
        REQUIRE((*sch)[1] == Approx(s).margin(1e-12));
    }
    const auto prod = amplitudes(prepared(3, {{"ry", RY(1.3), q({0}), {}}, {"h", H(), q({2}), {}}}));
    auto one = measures::schmidtCoefficients(prod, 3, q({1, 2}));
    REQUIRE((*one)[0] == Approx(1.0).margin(1e-12));
    REQUIRE((*one)[1] == Approx(0.0).margin(1e-12));
    // Σ λ² = 1 and λ descending for a random state; the entropy matches the reduced state's.
    core::Random rng(99);
    const auto psi = randomPure(16, rng);
    auto lam = measures::schmidtCoefficients(psi, 4, q({3, 0}));
    REQUIRE(lam.has_value());
    double sum = 0, entropy = 0;
    for (std::size_t k = 0; k < lam->size(); ++k) {
        sum += (*lam)[k] * (*lam)[k];
        if (k > 0) REQUIRE((*lam)[k] <= (*lam)[k - 1]);
        if ((*lam)[k] > 1e-12) entropy -= (*lam)[k] * (*lam)[k] * std::log2((*lam)[k] * (*lam)[k]);
    }
    REQUIRE(sum == Approx(1.0).margin(1e-12));
    StateVectorBackend sv;
    REQUIRE(sv.allocate(4).has_value());
    REQUIRE(sv.setAmplitudes(psi).has_value());
    REQUIRE(measures::entropyBits(*sv.reducedDensityMatrix(q({0, 3}))) == Approx(entropy).margin(1e-10));
    REQUIRE(measures::schmidtCoefficients(psi, 4, q({0, 1, 2, 3})).error().code == err::BadTargets);
    REQUIRE(measures::schmidtCoefficients(psi, 4, q({1, 1})).error().code == err::BadTargets);
}

TEST_CASE("Measures: Bloch vector, purity and fidelity") {
    auto bloch = [](const Circuit& c) {
        auto sv = prepared(1, c);
        auto r = measures::blochVector(num::projector(amplitudes(sv)));
        REQUIRE(r.has_value());
        return *r;
    };
    auto near = [](std::array<double, 3> r, std::array<double, 3> e) {
        for (int k = 0; k < 3; ++k) REQUIRE(r[k] == Approx(e[k]).margin(1e-12));
    };
    near(bloch({}), {0, 0, 1});                                        // |0⟩ at the north pole
    near(bloch({{"x", X(), q({0}), {}}}), {0, 0, -1});                 // |1⟩
    near(bloch({{"h", H(), q({0}), {}}}), {1, 0, 0});                  // |+⟩
    near(bloch({{"h", H(), q({0}), {}}, {"s", S(), q({0}), {}}}), {0, 1, 0});   // |+i⟩ = S|+⟩
    near(bloch({{"h", H(), q({0}), {}}, {"sdg", Sdg(), q({0}), {}}}), {0, -1, 0}); // |−i⟩
    // A qubit of a random 3-qubit state: r = (⟨X⟩, ⟨Y⟩, ⟨Z⟩) and |r|² = 2 Tr ρ² − 1 (spec 21 §2.2).
    auto sv = prepared(3, randomUniversal(3, 30, 4711));
    auto rho1 = sv.reducedDensityMatrix(q({1}));
    auto r = measures::blochVector(*rho1);
    REQUIRE(r.has_value());
    REQUIRE((*r)[0] == Approx(sv.expectation(*PauliString::parse("IXI")).value()).margin(1e-12));
    REQUIRE((*r)[1] == Approx(sv.expectation(*PauliString::parse("IYI")).value()).margin(1e-12));
    REQUIRE((*r)[2] == Approx(sv.expectation(*PauliString::parse("IZI")).value()).margin(1e-12));
    const double r2 = (*r)[0] * (*r)[0] + (*r)[1] * (*r)[1] + (*r)[2] * (*r)[2];
    REQUIRE(r2 == Approx(2.0 * measures::purity(*rho1) - 1.0).margin(1e-12));
    REQUIRE(measures::blochVector(Matrix::identity(4)).error().code == err::BadTargets);
    // Fidelity: ⟨φ|ρ|φ⟩ for a pure target, squared Uhlmann for a mixed one.
    const auto plus = amplitudes(prepared(1, {{"h", H(), q({0}), {}}}));
    const std::vector<Complex> zeroKet{1.0, 0.0};
    REQUIRE(measures::fidelityTo(num::projector(plus), zeroKet) == Approx(0.5).margin(1e-12));
    REQUIRE(measures::fidelityTo(num::projector(plus), num::projector(plus)) == Approx(1.0).margin(1e-9));
    REQUIRE(measures::fidelityTo(num::scale(Matrix::identity(2), 0.5), num::projector(zeroKet)) == Approx(0.5).margin(1e-9));
}
