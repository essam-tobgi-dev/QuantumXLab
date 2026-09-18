#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Numerics/Numerics.hpp"
#include <cmath>
#include <numbers>
using namespace qlab::num;
using Catch::Approx;

// Harmonic oscillator y'' = -y as (q, p): q' = p, p' = -q. Energy = (q²+p²)/2.
static const OdeRhs kSho = [](double, std::span<const Complex> y, std::span<Complex> d) { d[0] = y[1]; d[1] = -y[0]; };

TEST_CASE("rk4 has 4th-order convergence on SHO") {
    auto run = [](int steps) {
        Vector y = {1.0, 0.0};
        double h = 2 * std::numbers::pi / steps;
        for (int i = 0; i < steps; ++i) rk4Step(kSho, y, i * h, h);
        return std::abs(y[0] - 1.0);
    };
    double e1 = run(300), e2 = run(600);
    REQUIRE(e1 / e2 > 12.0); // ≈ 16 for order 4
    REQUIRE(e2 < 1e-8);
}

TEST_CASE("dopri5 conserves SHO energy to tolerance and steps adaptively") {
    Vector y = {1.0, 0.0};
    auto r = dopri5(kSho, y, 0.0, 20 * std::numbers::pi, 1e-10, 1e-12);
    REQUIRE(r.ok);
    REQUIRE(r.steps > 10);
    double E = (std::norm(y[0]) + std::norm(y[1])) / 2;
    REQUIRE(E == Approx(0.5).margin(1e-8));
    REQUIRE(std::abs(y[0] - 1.0) < 1e-6);
}

TEST_CASE("magnus2 is exact for a constant generator") {
    // y' = -i H y with H = σ_x; exact: exp(-i σx t).
    Matrix H = pauli::X.toMatrix();
    Vector y = {1.0, 0.0};
    double t = 0.0, h = 0.37;
    for (int i = 0; i < 5; ++i) {
        magnus2Step(2, [&](double, std::span<const Complex> x, std::span<Complex> out) {
            matvecInto(H, x, out); for (auto& v : out) v *= Complex(0, -1);
        }, y, t, h);
        t += h;
    }
    auto U = expmHermitian(H, t);
    REQUIRE(U);
    Vector ref = matvec(*U, Vector{1.0, 0.0});
    REQUIRE(std::abs(y[0] - ref[0]) < 1e-12);
    REQUIRE(std::abs(y[1] - ref[1]) < 1e-12);
}

TEST_CASE("dopri5 on a Lindblad-like decay reproduces exponential") {
    // y' = -γ y
    double g = 3.0;
    OdeRhs f = [&](double, std::span<const Complex> y, std::span<Complex> d) { d[0] = -g * y[0]; };
    Vector y = {1.0};
    dopri5(f, y, 0.0, 2.0, 1e-10, 1e-12);
    REQUIRE(std::abs(y[0] - std::exp(-6.0)) < 1e-9);
}
