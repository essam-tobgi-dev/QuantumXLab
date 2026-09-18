// Spec 21 §3.17 — Wigner function oracles: vacuum, Fock |1⟩, coherent state (sign convention),
// displaced-parity brute force, normalisation and the negativity volume of |1⟩. Headless.
#include "Core/Random.hpp"
#include "Numerics/Expm.hpp"
#include "Numerics/Matrix.hpp"
#include "Viz/Math/Wigner.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

using namespace qlab;
using namespace qlab::viz::math;
using Catch::Approx;
using num::Complex;
using num::Matrix;

namespace {
constexpr double kPi = std::numbers::pi;

Matrix fock(std::size_t n, std::size_t dim) {
    Matrix rho(dim, dim);
    rho(n, n) = 1.0;
    return rho;
}

Matrix coherent(Complex beta, std::size_t dim) {
    std::vector<Complex> c(dim);
    Complex term = std::exp(-0.5 * std::norm(beta)); // c_n = e^{−|β|²/2} βⁿ/√n!
    for (std::size_t n = 0; n < dim; ++n) {
        c[n] = term;
        term *= beta / std::sqrt(static_cast<double>(n + 1));
    }
    return num::projector(c);
}

// (2/π) Tr[D†(α) ρ D(α) P] with D(α) = exp(α a† − α* a) built in a `dim`-level truncation.
// `mirrored` evaluates (2/π) Tr[D(α) ρ D†(α) P] instead, the form spec 21 §3.17 originally printed.
double displacedParity(const Matrix& rhoSmall, Complex alpha, std::size_t dim, bool mirrored = false) {
    Matrix g(dim, dim);
    for (std::size_t n = 0; n + 1 < dim; ++n) {
        const double s = std::sqrt(static_cast<double>(n + 1));
        g(n + 1, n) = alpha * s;             // α a†
        g(n, n + 1) = -std::conj(alpha) * s; // −α* a
    }
    auto d = num::expm(g);
    REQUIRE(d.has_value());
    Matrix rho(dim, dim);
    for (std::size_t i = 0; i < rhoSmall.rows; ++i)
        for (std::size_t j = 0; j < rhoSmall.cols; ++j) rho(i, j) = rhoSmall(i, j);
    const Matrix m = mirrored ? num::matmul(*d, num::matmul(rho, num::adjoint(*d)))
                              : num::matmul(num::adjoint(*d), num::matmul(rho, *d));
    double tr = 0.0;
    for (std::size_t n = 0; n < dim; ++n) tr += ((n & 1u) ? -1.0 : 1.0) * m(n, n).real();
    return 2.0 / kPi * tr;
}
} // namespace

TEST_CASE("Wigner function of the vacuum is (2/pi) exp(-2|alpha|^2)") {
    const Matrix vac = fock(0, 12);
    for (const Complex alpha : {Complex(0, 0), Complex(0.3, 0), Complex(0, -0.7), Complex(1.1, 0.4), Complex(-2.0, 1.5)}) {
        auto w = wignerAt(vac, alpha);
        REQUIRE(w.has_value());
        CHECK(*w == Approx(2.0 / kPi * std::exp(-2.0 * std::norm(alpha))).margin(1e-8));
    }
}

TEST_CASE("Wigner function of |1> is -2/pi at the origin and (2/pi)(4|a|^2 - 1)exp(-2|a|^2) elsewhere") {
    const Matrix one = fock(1, 8);
    CHECK(*wignerAt(one, Complex(0, 0)) == Approx(-2.0 / kPi).margin(1e-12));
    CHECK(*wignerAt(one, Complex(0, 0)) < 0.0);
    for (const Complex alpha : {Complex(0.5, 0), Complex(0.2, 0.9), Complex(-1.4, 0.3)}) {
        const double r2 = std::norm(alpha);
        CHECK(*wignerAt(one, alpha) == Approx(2.0 / kPi * (4.0 * r2 - 1.0) * std::exp(-2.0 * r2)).margin(1e-10));
    }
    // Fock |n⟩ at the origin: (2/π)(−1)ⁿ — the parity of the state.
    for (std::size_t n = 0; n <= 7; ++n)
        CHECK(*wignerAt(fock(n, 8), Complex(0, 0)) == Approx((n % 2 ? -2.0 : 2.0) / kPi).margin(1e-12));
}

TEST_CASE("a coherent state |beta> is the vacuum Gaussian centred at +beta") {
    // This pins the sign convention W(α) = (2/π) Tr[D†(α) ρ D(α) P]: displacing by −α and
    // measuring parity. With D and D† exchanged the peak would sit at −β.
    const Complex beta(1.0, 0.5);
    const Matrix rho = coherent(beta, 41);
    CHECK(*wignerAt(rho, beta) == Approx(2.0 / kPi).margin(1e-8));
    CHECK(*wignerAt(rho, -beta) == Approx(2.0 / kPi * std::exp(-8.0 * std::norm(beta))).margin(1e-8));
    for (const Complex alpha : {Complex(0, 0), Complex(1.5, 0.2), Complex(0.4, 1.3), Complex(-0.6, -0.2)})
        CHECK(*wignerAt(rho, alpha) == Approx(2.0 / kPi * std::exp(-2.0 * std::norm(alpha - beta))).margin(1e-8));
}

TEST_CASE("the Laguerre expression equals the displaced parity of a random mixed state") {
    core::Random rng(0x5EED);
    const std::size_t small = 5;
    Matrix a(small, small);
    for (std::size_t i = 0; i < small; ++i)
        for (std::size_t j = 0; j < small; ++j) a(i, j) = Complex(rng.normal(), rng.normal());
    Matrix rho = num::matmul(a, num::adjoint(a)); // positive; normalise the trace
    rho *= 1.0 / num::trace(rho).real();
    for (const Complex alpha : {Complex(0, 0), Complex(0.4, -0.3), Complex(-0.9, 0.6), Complex(0.1, 1.2)})
        CHECK(*wignerAt(rho, alpha) == Approx(displacedParity(rho, alpha, 56)).margin(1e-9));
    // Spec correction: with D and D† exchanged the trace is W(−α), not W(α); the two differ for a
    // state without inversion symmetry, so the Laguerre expression fixes which one is meant.
    const Complex alpha(0.4, -0.3);
    CHECK(displacedParity(rho, alpha, 56, true) == Approx(*wignerAt(rho, -alpha)).margin(1e-9));
    CHECK(std::abs(*wignerAt(rho, alpha) - *wignerAt(rho, -alpha)) > 1e-3);
}

TEST_CASE("Wigner grid: normalisation, negativity volume of |1>, neutral colour at W = 0") {
    WignerOptions o;
    o.xMin = o.pMin = -6.0;
    o.xMax = o.pMax = 6.0;
    o.nx = o.ny = 201;
    auto vac = wignerGrid(fock(0, 4), o);
    REQUIRE(vac.has_value());
    CHECK(vac->integral == Approx(1.0).margin(1e-9));
    CHECK(vac->negativity == Approx(0.0).margin(1e-9));
    CHECK(vac->minValue >= 0.0);
    // α = (x + ip)/√2: on the axes the vacuum is (2/π) e^{−(x² + p²)}.
    CHECK(vac->at(100, 100) == Approx(2.0 / kPi).margin(1e-12));
    CHECK(vac->at(125, 100) == Approx(2.0 / kPi * std::exp(-vac->x(125) * vac->x(125))).margin(1e-12));

    auto one = wignerGrid(fock(1, 4), o);
    REQUIRE(one.has_value());
    CHECK(one->integral == Approx(1.0).margin(1e-9));
    // ∫|W| d²α − 1 = 4/√e − 2 (Kenfack & Życzkowski 2004). |W| has a kink on the circle |α| = ½,
    // so the trapezoid rule converges as h² with h = 0.06: tolerance 2e-3.
    CHECK(one->negativity == Approx(4.0 / std::sqrt(std::numbers::e) - 2.0).margin(2e-3));
    CHECK(one->minValue == Approx(-2.0 / kPi).margin(1e-12));
    CHECK(one->symmetricLimit() == Approx(2.0 / kPi).margin(1e-12));

    const auto pn = photonNumberDistribution(coherent(Complex(1.0, 0.0), 30));
    CHECK(pn[0] == Approx(std::exp(-1.0)).margin(1e-12)); // Poisson with mean |β|² = 1
    CHECK(pn[2] == Approx(std::exp(-1.0) / 2.0).margin(1e-12));
}

TEST_CASE("Wigner limits of spec 21 §3.17 are enforced") {
    CHECK_FALSE(wignerAt(Matrix(62, 62), Complex(0, 0)).has_value()); // N_max = 60 → at most 61 levels
    CHECK(wignerAt(fock(60, 61), Complex(0.5, 0.5)).has_value());
    WignerOptions big;
    big.nx = 202;
    CHECK_FALSE(wignerGrid(fock(0, 4), big).has_value());
    CHECK_FALSE(wignerAt(Matrix(3, 4), Complex(0, 0)).has_value());
    // High Fock states stay finite and bounded by 2/π far from the origin (no overflow).
    const double w = *wignerAt(fock(60, 61), Complex(6.0, 0.0));
    CHECK(std::isfinite(w));
    CHECK(std::abs(w) <= 2.0 / kPi + 1e-12);
}
