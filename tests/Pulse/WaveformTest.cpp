#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Pulse/Channel.hpp"
#include "Pulse/Waveform.hpp"

#include <cmath>
#include <numbers>

using namespace qlab;
using namespace qlab::pulse;
using Catch::Approx;

namespace {
// Trapezoidal integration of the sampled envelope, independent of Waveform::area().
Complex numericArea(const Waveform& w, int n = 200000) {
    const double h = w.duration / n;
    Complex acc = 0.5 * (w.sample(0.0) + w.sample(w.duration));
    for (int i = 1; i < n; ++i) acc += w.sample(i * h);
    return acc * h;
}
} // namespace

TEST_CASE("channel names round-trip") {
    for (std::string s : {"d[0]", "u[3,7]", "f[12]", "m[1]", "a[1]", "g", "r[5]", "ms[0,1]", "detect", "pump"}) {
        auto c = parseChannel(s);
        REQUIRE(c);
        REQUIRE(c->toString() == s);
    }
    REQUIRE_FALSE(parseChannel("d[a]").has_value());
    REQUIRE_FALSE(parseChannel("q[0]").has_value());
    REQUIRE_FALSE(parseChannel("u[0]").has_value());     // needs two indices
    REQUIRE_FALSE(parseChannel("d[0,1]").has_value());   // takes one
    REQUIRE(parseChannel("u[2,5]")->primaryQubit() == 2u);
    REQUIRE_FALSE(ChannelId::acquire(0).isDrivelike());
}

TEST_CASE("gaussian area matches the closed form") {
    const double T = 32e-9, sigma = 8e-9;
    auto w = Waveform::gaussian(T, sigma);
    REQUIRE(w.validate());
    // Closed form: [σ√(2π) erf(T/(2√2 σ)) − c·T] / (1−c)
    const double c = std::exp(-(T * T / 4.0) / (2 * sigma * sigma));
    const double expected =
        (sigma * std::sqrt(2 * std::numbers::pi) * std::erf(T / (2 * std::numbers::sqrt2 * sigma)) - c * T) / (1 - c);
    REQUIRE(w.area().real() == Approx(expected).epsilon(1e-12));
    REQUIRE(std::abs(w.area() - numericArea(w)) / expected < 1e-9);
    REQUIRE(w.sample(0.0).real() == Approx(0.0).margin(1e-15));
    REQUIRE(w.sample(T).real() == Approx(0.0).margin(1e-15));
    REQUIRE(w.sample(T / 2).real() == Approx(1.0).epsilon(1e-12));
    REQUIRE(w.maxAbs() == Approx(1.0).epsilon(1e-6));
}

TEST_CASE("gaussian_square area matches the closed form") {
    const double T = 380e-9, sigma = 16e-9, rise = 32e-9;
    auto w = Waveform::gaussianSquare(T, sigma, rise);
    REQUIRE(w.validate());
    REQUIRE(std::abs(w.area() - numericArea(w)) / w.area().real() < 1e-9);
    REQUIRE(w.sample(T / 2).real() == Approx(1.0).epsilon(1e-12)); // flat top
    REQUIRE(w.area().real() > T - 2 * rise);                        // flat part plus the edges
    REQUIRE(w.area().real() < T);
}

TEST_CASE("DRAG quadrature is -beta times the in-phase derivative and integrates to zero") {
    const double T = 32e-9, sigma = 8e-9, beta = 0.2481e-9;
    auto w = Waveform::drag(T, sigma, beta);
    REQUIRE(w.validate());
    for (double t : {4e-9, 10e-9, 16e-9, 24e-9}) {
        const double dg = gaussianShapeDerivative(t, T, sigma);
        REQUIRE(w.sample(t).imag() == Approx(beta * dg).epsilon(1e-12));
        REQUIRE(w.sample(t).real() == Approx(gaussianShape(t, T, sigma)).epsilon(1e-12));
    }
    REQUIRE(w.area().imag() == Approx(0.0).margin(1e-24));
    REQUIRE(std::abs(numericArea(w).imag()) < 1e-20);
    // With beta = -1/alpha (alpha the angular anharmonicity) the sign is negative for alpha < 0.
    REQUIRE(w.area().real() == Approx(gaussianShapeArea(T, sigma)).epsilon(1e-12));
}

TEST_CASE("cosine, constant, sech and slepian shapes") {
    REQUIRE(Waveform::cosine(40e-9).area().real() == Approx(20e-9).epsilon(1e-9));
    REQUIRE(Waveform::constant(100e-9, 0.5).area().real() == Approx(50e-9).epsilon(1e-12));
    auto s = Waveform::sech(60e-9, 12e-9);
    REQUIRE(s.validate());
    REQUIRE(std::abs(s.area() - numericArea(s)) / s.area().real() < 1e-6);
    auto sl = Waveform::slepian(40e-9, 0.2);
    REQUIRE(sl.validate());
    REQUIRE(sl.maxAbs() == Approx(1.0).epsilon(1e-3));
    REQUIRE(sl.sample(0.0).real() == Approx(0.0).margin(1e-12));
    REQUIRE(sl.sample(40e-9).real() == Approx(0.0).margin(1e-12));
}

TEST_CASE("waveform validation rejects bad parameters") {
    REQUIRE_FALSE(Waveform::gaussian(0.0, 1e-9).validate().has_value());
    REQUIRE_FALSE(Waveform::gaussian(32e-9, 0.0).validate().has_value());
    REQUIRE_FALSE(Waveform::gaussianSquare(32e-9, 8e-9, 20e-9).validate().has_value()); // 2·rise > T
    REQUIRE_FALSE(Waveform::constant(32e-9, 1.5).validate().has_value());               // |e| > 1
    REQUIRE(Waveform::constant(32e-9, -1.0).validate());                                // negative amp is legal
}

TEST_CASE("negative amplitudes (echoed CR halves) sample and integrate as the negated envelope") {
    // Regression: the scale was std::polar(amplitude, phase), which is NaN for amplitude < 0.
    const auto plus = Waveform::gaussianSquare(188.256e-9, 16e-9, 32e-9, 0.434, 0.3);
    const auto minus = Waveform::gaussianSquare(188.256e-9, 16e-9, 32e-9, -0.434, 0.3);
    for (double t : {0.0, 5e-9, 40e-9, 150e-9, 188.256e-9}) {
        REQUIRE(std::isfinite(minus.sample(t).real()));
        REQUIRE(std::abs(minus.sample(t) + plus.sample(t)) < 1e-15);
    }
    REQUIRE(std::abs(minus.area() + plus.area()) < 1e-24);
    REQUIRE(minus.maxAbs() == Approx(0.434).epsilon(1e-12));
    auto nanAmp = Waveform::constant(32e-9, std::nan(""));
    REQUIRE_FALSE(nanAmp.validate().has_value());
}

TEST_CASE("sampling uses zero-order hold on the dt grid") {
    const std::int64_t dtPs = 222;
    auto w = Waveform::gaussian(32e-9, 8e-9);
    auto s = w.sampled(dtPs);
    REQUIRE(s.size() == static_cast<std::size_t>(std::llround(32e-9 / (dtPs * 1e-12))));
    REQUIRE(s.front().real() == Approx(w.sample(0.0).real()));
    REQUIRE(s[10].real() == Approx(w.sample(10 * dtPs * 1e-12).real()));
    auto sp = w.spectrum(dtPs);
    REQUIRE(sp.magnitude.size() == sp.freqHz.size());
    REQUIRE(sp.magnitude[0] > 0.0); // DC component of a positive envelope
}
