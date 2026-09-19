// Spec 21 §2.1–2.2, §5 — reduced states, Bloch vectors, entropies, mutual information,
// concurrence and Schmidt spectra against closed forms. Headless.
#include "Core/Random.hpp"
#include "Numerics/Matrix.hpp"
#include "Numerics/Tensor.hpp"
#include "Viz/Math/Reduced.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

using namespace qlab;
using namespace qlab::viz::math;
using Catch::Approx;

namespace {
constexpr double kTol = 1e-12;
const double kInvSqrt2 = 1.0 / std::sqrt(2.0);

std::vector<Complex> randomState(std::uint32_t n, std::uint64_t seed) {
    core::Random rng(seed);
    std::vector<Complex> psi(std::size_t{1} << n);
    double norm = 0.0;
    for (auto& a : psi) {
        a = Complex(rng.normal(), rng.normal());
        norm += std::norm(a);
    }
    for (auto& a : psi)
        a /= std::sqrt(norm);
    return psi;
}

BlochVector blochOf(const std::vector<Complex>& psi, std::uint32_t n, std::uint32_t k) {
    auto rho = reducedSingle(psi, n, k);
    REQUIRE(rho.has_value());
    auto r = blochVector(*rho);
    REQUIRE(r.has_value());
    return *r;
}
} // namespace

TEST_CASE("Bloch vectors of the cardinal states (|0> is the north pole)") {
    // H = −½ħωZ: |0⟩ is the ground state at +z (DEVELOPMENT.md); r = (Tr ρX, Tr ρY, Tr ρZ).
    const BlochVector zero = blochOf({1.0, 0.0}, 1, 0);
    CHECK(zero.x == Approx(0.0).margin(kTol));
    CHECK(zero.y == Approx(0.0).margin(kTol));
    CHECK(zero.z == Approx(1.0).margin(kTol));
    CHECK(zero.p1() == Approx(0.0).margin(kTol));

    const BlochVector one = blochOf({0.0, 1.0}, 1, 0);
    CHECK(one.z == Approx(-1.0).margin(kTol));
    CHECK(one.p1() == Approx(1.0).margin(kTol));

    const BlochVector plus = blochOf({kInvSqrt2, kInvSqrt2}, 1, 0);
    CHECK(plus.x == Approx(1.0).margin(kTol));
    CHECK(plus.y == Approx(0.0).margin(kTol));
    CHECK(plus.z == Approx(0.0).margin(kTol));
    CHECK(plus.theta() == Approx(std::numbers::pi / 2).margin(1e-12));
    CHECK(plus.phi() == Approx(0.0).margin(1e-12));

    const BlochVector plusI = blochOf({kInvSqrt2, Complex(0.0, kInvSqrt2)}, 1, 0);
    CHECK(plusI.x == Approx(0.0).margin(kTol));
    CHECK(plusI.y == Approx(1.0).margin(kTol));
    CHECK(plusI.z == Approx(0.0).margin(kTol));
    CHECK(plusI.phi() == Approx(std::numbers::pi / 2).margin(1e-12));

    const BlochVector minus = blochOf({kInvSqrt2, -kInvSqrt2}, 1, 0);
    CHECK(minus.x == Approx(-1.0).margin(kTol));
}

TEST_CASE("a mixed state lies inside the Bloch ball with |r|^2 = 2 Tr rho^2 - 1") {
    // ρ = 0.7|0⟩⟨0| + 0.3|+⟩⟨+|  →  r = (0.3, 0, 0.7).
    num::Matrix rho(2, 2);
    rho(0, 0) = 0.7 + 0.15;
    rho(1, 1) = 0.15;
    rho(0, 1) = rho(1, 0) = 0.15;
    auto r = blochVector(rho);
    REQUIRE(r.has_value());
    CHECK(r->x == Approx(0.3).margin(kTol));
    CHECK(r->y == Approx(0.0).margin(kTol));
    CHECK(r->z == Approx(0.7).margin(kTol));
    CHECK(r->norm() < 1.0);
    CHECK(r->norm() * r->norm() == Approx(2.0 * purity(rho) - 1.0).margin(kTol)); // spec 21 §2.2
    // Maximally mixed: the centre of the ball, one bit of entropy.
    num::Matrix mixed = num::Matrix::identity(2);
    mixed *= 0.5;
    CHECK(blochVector(mixed)->norm() == Approx(0.0).margin(kTol));
    CHECK(entropyBits(mixed) == Approx(1.0).margin(1e-12));
    CHECK_FALSE(blochVector(num::Matrix::identity(4)).has_value());
}

TEST_CASE("Bell pair: |r| = 0 on both qubits, I = 2 bits, C = 1 (spec 21 §5)") {
    const std::vector<Complex> bell{kInvSqrt2, 0.0, 0.0, kInvSqrt2};
    CHECK(blochOf(bell, 2, 0).norm() == Approx(0.0).margin(kTol));
    CHECK(blochOf(bell, 2, 1).norm() == Approx(0.0).margin(kTol));
    auto rho = reducedPair(bell, 2, 0, 1);
    REQUIRE(rho.has_value());
    auto m = pairMeasures(*rho);
    REQUIRE(m.has_value());
    CHECK(m->entropyI == Approx(1.0).margin(1e-10));
    CHECK(m->entropyJ == Approx(1.0).margin(1e-10));
    CHECK(m->entropyIJ == Approx(0.0).margin(1e-10));
    CHECK(m->mutualInformation == Approx(2.0).margin(1e-10));
    CHECK(m->concurrence == Approx(1.0).margin(1e-7)); // √ of a Jacobi eigenvalue: half the digits
    auto s = schmidtSpectrum(bell, 2, std::vector<QubitIndex>{QubitIndex{0}});
    REQUIRE(s.has_value());
    REQUIRE(s->coefficients.size() == 2);
    CHECK(s->coefficients[0] == Approx(kInvSqrt2).margin(kTol));
    CHECK(s->coefficients[1] == Approx(kInvSqrt2).margin(kTol));
    CHECK(s->rank == 2);
    CHECK(s->entropyBits == Approx(1.0).margin(1e-12));
}

TEST_CASE("product state: no mutual information, no concurrence") {
    // |+⟩ ⊗ |0⟩ with qubit 0 = |0⟩, qubit 1 = |+⟩: amplitudes on indices 0 and 2.
    const std::vector<Complex> psi{kInvSqrt2, 0.0, kInvSqrt2, 0.0};
    CHECK(blochOf(psi, 2, 0).z == Approx(1.0).margin(kTol));
    CHECK(blochOf(psi, 2, 1).x == Approx(1.0).margin(kTol)); // little-endian: qubit 1 carries |+⟩
    auto m = pairMeasures(*reducedPair(psi, 2, 0, 1));
    REQUIRE(m.has_value());
    CHECK(m->mutualInformation == Approx(0.0).margin(1e-10));
    CHECK(m->concurrence == Approx(0.0).margin(1e-7));
}

TEST_CASE("Schmidt spectrum of GHZ is (1/sqrt2, 1/sqrt2) across every cut") {
    const std::uint32_t n = 4;
    std::vector<Complex> ghz(std::size_t{1} << n, 0.0);
    ghz.front() = ghz.back() = kInvSqrt2;
    for (const std::vector<QubitIndex>& cut :
         {std::vector<QubitIndex>{QubitIndex{0}},
          std::vector<QubitIndex>{QubitIndex{1}, QubitIndex{3}},
          std::vector<QubitIndex>{QubitIndex{0}, QubitIndex{1}, QubitIndex{2}}}) {
        auto s = schmidtSpectrum(ghz, n, cut);
        REQUIRE(s.has_value());
        REQUIRE(s->coefficients.size() >= 2);
        CHECK(s->coefficients[0] == Approx(kInvSqrt2).margin(kTol));
        CHECK(s->coefficients[1] == Approx(kInvSqrt2).margin(kTol));
        for (std::size_t k = 2; k < s->coefficients.size(); ++k)
            CHECK(s->coefficients[k] == Approx(0.0).margin(kTol));
        CHECK(s->rank == 2);
        CHECK(s->entropyBits == Approx(1.0).margin(1e-12));
    }
    // GHZ pairs are classically correlated only: I = 1 bit, C = 0 (the rest is traced out).
    auto m = pairMeasures(*reducedPair(ghz, n, 0, 3));
    REQUIRE(m.has_value());
    CHECK(m->mutualInformation == Approx(1.0).margin(1e-10));
    CHECK(m->concurrence == Approx(0.0).margin(1e-7));
    CHECK_FALSE(schmidtSpectrum(ghz, n, std::vector<QubitIndex>{}).has_value());
}

TEST_CASE("the O(2^n) kernels agree with the generic partial trace on a random state") {
    const std::uint32_t n = 6;
    const auto psi = randomState(n, 0xB10C);
    for (std::uint32_t k = 0; k < n; ++k) {
        const std::vector<std::size_t> keep{k};
        CHECK(num::approxEqual(*reducedSingle(psi, n, k), num::reducedState(psi, n, keep), 1e-13));
    }
    for (std::uint32_t i = 0; i < n; ++i)
        for (std::uint32_t j = 0; j < n; ++j) {
            if (i == j)
                continue;
            const std::vector<std::size_t> keep{i, j}; // i least significant, also when i > j
            CHECK(num::approxEqual(*reducedPair(psi, n, i, j), num::reducedState(psi, n, keep),
                                   1e-13));
        }
    // The same reductions from the density matrix |ψ⟩⟨ψ|.
    const num::Matrix rho = num::projector(psi);
    const std::vector<QubitIndex> pair{QubitIndex{4}, QubitIndex{1}};
    CHECK(
        num::approxEqual(*reducedFromDensity(rho, n, 2, pair), *reducedPair(psi, n, 4, 1), 1e-13));
    CHECK_FALSE(reducedSingle(psi, n, n).has_value());
    CHECK_FALSE(reducedPair(psi, n, 2, 2).has_value());
    CHECK_FALSE(reducedSingle(std::vector<Complex>(5), 2, 0).has_value());
}

TEST_CASE("leakage shortens the Bloch vector instead of being renormalised away") {
    // One transmon site with three levels: 0.9 |0⟩⟨0| + 0.1 |2⟩⟨2|.
    num::Matrix rho(3, 3);
    rho(0, 0) = 0.9;
    rho(2, 2) = 0.1;
    auto block = computationalBlock(rho, 1, 3);
    REQUIRE(block.has_value());
    REQUIRE(block->rows == 2);
    CHECK(num::trace(*block).real() == Approx(0.9).margin(kTol));
    CHECK(blochVector(*block)->z == Approx(0.9).margin(kTol));
}
