// Spec 25 §3 oracles on the state-vector backend.
#include "Gates.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Tensor.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qtest;
using Catch::Approx;

TEST_CASE("Bell state amplitudes") {
    StateVectorBackend sv;
    REQUIRE(sv.allocate(2).has_value());
    REQUIRE(sv.applyGate(H(), q({0})).has_value());
    REQUIRE(sv.applyGate(CX(), q({0, 1})).has_value());
    auto a = sv.amplitudes();
    const double s = 1.0 / std::sqrt(2.0);
    REQUIRE(a[0].real() == Approx(s).margin(1e-14));
    REQUIRE(std::abs(a[1]) == Approx(0).margin(1e-14));
    REQUIRE(std::abs(a[2]) == Approx(0).margin(1e-14));
    REQUIRE(a[3].real() == Approx(s).margin(1e-14));
    REQUIRE(sv.stateNorm() == Approx(1.0).margin(1e-14));
    // ⟨ZZ⟩ = 1, ⟨XX⟩ = 1, ⟨ZI⟩ = 0 (T01 §7).
    REQUIRE(sv.expectation(*PauliString::parse("ZZ")).value() == Approx(1.0).margin(1e-13));
    REQUIRE(sv.expectation(*PauliString::parse("XX")).value() == Approx(1.0).margin(1e-13));
    REQUIRE(sv.expectation(*PauliString::parse("ZI")).value() == Approx(0.0).margin(1e-13));
    REQUIRE(sv.expectation(*PauliString::parse("YY")).value() == Approx(-1.0).margin(1e-13));
}

TEST_CASE("GHZ state on 5 qubits") {
    StateVectorBackend sv;
    REQUIRE(sv.allocate(5).has_value());
    REQUIRE(sv.applyGate(H(), q({0})).has_value());
    for (std::uint32_t i = 0; i + 1 < 5; ++i)
        REQUIRE(sv.applyGate(CX(), q({i, i + 1})).has_value());
    auto a = sv.amplitudes();
    const double s = 1.0 / std::sqrt(2.0);
    REQUIRE(a[0].real() == Approx(s).margin(1e-14));
    REQUIRE(a[31].real() == Approx(s).margin(1e-14));
    for (std::size_t i = 1; i < 31; ++i)
        REQUIRE(std::abs(a[i]) == Approx(0).margin(1e-14));
}

TEST_CASE("QFT-4 reproduces the DFT matrix up to global phase") {
    const std::uint32_t n = 4;
    const std::size_t dim = 1u << n;
    Matrix built(dim, dim);
    for (std::size_t col = 0; col < dim; ++col) {
        StateVectorBackend sv;
        REQUIRE(sv.allocate(n).has_value());
        std::vector<Complex> init(dim, Complex{});
        init[col] = 1.0;
        REQUIRE(sv.setAmplitudes(init).has_value());
        // Standard QFT: for j = n-1 .. 0: H(j), then controlled phases from lower qubits; final
        // swaps.
        for (int j = static_cast<int>(n) - 1; j >= 0; --j) {
            REQUIRE(sv.applyGate(H(), q({static_cast<std::uint32_t>(j)})).has_value());
            for (int k = j - 1; k >= 0; --k) {
                double angle = std::numbers::pi / std::pow(2.0, j - k);
                REQUIRE(sv.applyControlled(phase(angle), q({static_cast<std::uint32_t>(k)}),
                                           q({static_cast<std::uint32_t>(j)}))
                            .has_value());
            }
        }
        for (std::uint32_t i = 0; i < n / 2; ++i)
            REQUIRE(sv.applyGate(SWAP(), q({i, n - 1 - i})).has_value());
        auto amps = sv.amplitudes();
        for (std::size_t row = 0; row < dim; ++row)
            built(row, col) = amps[row];
    }
    Matrix dft(dim, dim);
    const double norm = 1.0 / std::sqrt(static_cast<double>(dim));
    for (std::size_t r = 0; r < dim; ++r)
        for (std::size_t c = 0; c < dim; ++c)
            dft(r, c) =
                norm * std::exp(Complex(0, 2.0 * std::numbers::pi * static_cast<double>(r * c) /
                                               static_cast<double>(dim)));
    REQUIRE(num::equalUpToGlobalPhase(built, dft, 1e-12));
}

TEST_CASE("Grover on 3 qubits reaches the analytic success probability") {
    // N = 8, M = 1, marked |101>. After k iterations P = sin²((2k+1)θ), sinθ = 1/√8.
    const std::uint32_t n = 3;
    StateVectorBackend sv;
    REQUIRE(sv.allocate(n).has_value());
    for (std::uint32_t i = 0; i < n; ++i)
        REQUIRE(sv.applyGate(H(), q({i})).has_value());
    const std::size_t marked = 0b101;
    auto oracle = [&] {
        std::vector<Complex> a(sv.amplitudes().begin(), sv.amplitudes().end());
        a[marked] = -a[marked];
        REQUIRE(sv.setAmplitudes(a).has_value());
    };
    auto diffusion = [&] {
        for (std::uint32_t i = 0; i < n; ++i)
            REQUIRE(sv.applyGate(H(), q({i})).has_value());
        std::vector<Complex> a(sv.amplitudes().begin(), sv.amplitudes().end());
        for (std::size_t i = 1; i < a.size(); ++i)
            a[i] = -a[i];
        REQUIRE(sv.setAmplitudes(a).has_value());
        for (std::uint32_t i = 0; i < n; ++i)
            REQUIRE(sv.applyGate(H(), q({i})).has_value());
    };
    const double theta = std::asin(1.0 / std::sqrt(8.0));
    for (int k = 1; k <= 2; ++k) {
        oracle();
        diffusion();
        double p = std::norm(sv.amplitudes()[marked]);
        double expected = std::pow(std::sin((2 * k + 1) * theta), 2.0);
        REQUIRE(p == Approx(expected).margin(1e-12));
    }
    REQUIRE(std::norm(sv.amplitudes()[marked]) == Approx(0.9453125).margin(1e-12));
}

TEST_CASE("Teleportation moves the state to the third qubit") {
    core::Random rng(7);
    // |ψ⟩ = RY(0.7) RZ(0.3) |0⟩ on q0; Bell pair on q1,q2.
    StateVectorBackend sv;
    REQUIRE(sv.allocate(3).has_value());
    REQUIRE(sv.applyGate(RZ(0.3), q({0})).has_value());
    REQUIRE(sv.applyGate(RY(0.7), q({0})).has_value());
    auto before = sv.reducedDensityMatrix(q({0}));
    REQUIRE(before.has_value());
    REQUIRE(sv.applyGate(H(), q({1})).has_value());
    REQUIRE(sv.applyGate(CX(), q({1, 2})).has_value());
    REQUIRE(sv.applyGate(CX(), q({0, 1})).has_value());
    REQUIRE(sv.applyGate(H(), q({0})).has_value());
    auto m = sv.measure(q({0, 1}), rng);
    REQUIRE(m.has_value());
    if (m->bits[1])
        REQUIRE(sv.applyGate(X(), q({2})).has_value());
    if (m->bits[0])
        REQUIRE(sv.applyGate(Z(), q({2})).has_value());
    auto after = sv.reducedDensityMatrix(q({2}));
    REQUIRE(after.has_value());
    REQUIRE(num::maxAbsNorm(num::sub(*before, *after)) < 1e-12);
}
