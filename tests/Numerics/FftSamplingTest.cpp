#include "Core/Random.hpp"
#include "Numerics/Numerics.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
using namespace qlab::num;
using Catch::Approx;

TEST_CASE("FFT of a pure tone lands in the right bin and Parseval holds") {
    std::size_t N = 256;
    std::size_t k0 = 17;
    Vector x(N);
    for (std::size_t n = 0; n < N; ++n)
        x[n] = std::polar(1.0, 2 * std::numbers::pi * static_cast<double>(k0 * n) /
                                   static_cast<double>(N));
    Vector X = fft(x);
    REQUIRE(std::abs(X[k0]) == Approx(static_cast<double>(N)).margin(1e-9));
    for (std::size_t k = 0; k < N; ++k)
        if (k != k0)
            REQUIRE(std::abs(X[k]) < 1e-9);
    double ex = norm2Squared(x), eX = norm2Squared(X) / static_cast<double>(N);
    REQUIRE(ex == Approx(eX).margin(1e-9));
    Vector back = fft(X, true);
    for (std::size_t n = 0; n < N; ++n)
        REQUIRE(std::abs(back[n] - x[n]) < 1e-12);
}

TEST_CASE("Bluestein N=1000 matches direct DFT") {
    std::size_t N = 1000;
    qlab::core::Random rng(2);
    Vector x(N);
    for (auto& v : x)
        v = Complex(rng.normal(), rng.normal());
    Vector X = fft(x);
    for (std::size_t k : {0u, 1u, 7u, 499u, 999u}) {
        Complex s{};
        for (std::size_t n = 0; n < N; ++n)
            s += x[n] * std::polar(1.0, -2 * std::numbers::pi * static_cast<double>(k * n) /
                                            static_cast<double>(N));
        REQUIRE(std::abs(X[k] - s) < 1e-8);
    }
    Vector back = fft(X, true);
    for (std::size_t n = 0; n < N; n += 97)
        REQUIRE(std::abs(back[n] - x[n]) < 1e-10);
}

TEST_CASE("windows have the stated ENBW within tolerance") {
    for (Window w :
         {Window::Rect, Window::Hann, Window::Hamming, Window::Blackman, Window::FlatTop}) {
        RealVector v = window(w, 4096);
        double s1 = 0, s2 = 0;
        for (double a : v) {
            s1 += a;
            s2 += a * a;
        }
        double enbw = static_cast<double>(v.size()) * s2 / (s1 * s1);
        REQUIRE(enbw == Approx(windowEnbw(w)).epsilon(0.01));
    }
    Spectrum sp = powerSpectrum(Vector(1024, Complex(1, 0)), 1e9, Window::Hann);
    REQUIRE(sp.powerDb[0] == Approx(0.0).margin(0.05)); // DC at full scale → 0 dBFS
    REQUIRE(sp.enbwHz == Approx(1.5 * 1e9 / 1024));
}

TEST_CASE("alias table and cumulative sampler reproduce the distribution") {
    std::vector<double> p = {0.1, 0.2, 0.3, 0.4};
    qlab::core::Random rng(77);
    std::size_t shots = 200000;
    auto counts = sampleCounts(p, shots, rng);
    double chi = chiSquare(counts, p);
    REQUIRE(chi < 16.3); // 3 dof, p = 0.001
    CumulativeSampler c(p);
    std::vector<std::size_t> cc(4, 0);
    for (std::size_t i = 0; i < shots; ++i)
        ++cc[c.sample(rng)];
    REQUIRE(chiSquare(cc, p) < 16.3);
    AliasTable degenerate(std::vector<double>{0.0, 1.0, 0.0});
    for (int i = 0; i < 100; ++i)
        REQUIRE(degenerate.sample(rng) == 1);
    Vector psi = {std::sqrt(0.25), std::sqrt(0.75)};
    REQUIRE(probabilities(psi)[1] == Approx(0.75));
}
