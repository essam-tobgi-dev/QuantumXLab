#pragma once
// Shared helpers for the qlab::noise tests: gate matrices (little-endian, targets[0] least
// significant), Result checks that print the error, and attachment shorthands.
#include "Noise/Noise.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Matrix.hpp"
#include "Numerics/Tensor.hpp"
#include "QSim/QSim.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <initializer_list>
#include <numbers>
#include <string>
#include <vector>

// REQUIRE that a Result/Status holds a value; on failure the error message is shown.
#define NOISE_REQUIRE_OK(expr)                                                                     \
    do {                                                                                           \
        auto&& noiseOk_ = (expr);                                                                  \
        INFO((noiseOk_ ? std::string() : noiseOk_.error().format()));                              \
        REQUIRE(noiseOk_.has_value());                                                             \
    } while (0)

namespace ntest {
using namespace qlab;
using num::Complex;
using num::Matrix;

inline std::vector<QubitIndex> q(std::initializer_list<std::uint32_t> ids) {
    std::vector<QubitIndex> out;
    for (auto i : ids)
        out.push_back(QubitIndex{i});
    return out;
}

inline Matrix X() {
    return Matrix::fromRows({{0, 1}, {1, 0}});
}
inline Matrix H() {
    const double s = 1.0 / std::numbers::sqrt2;
    return Matrix::fromRows({{s, s}, {s, -s}});
}
inline Matrix SX() { // √X = e^{iπ/4} R_X(π/2)
    return Matrix::fromRows(
        {{Complex(0.5, 0.5), Complex(0.5, -0.5)}, {Complex(0.5, -0.5), Complex(0.5, 0.5)}});
}
inline Matrix S() {
    return Matrix::fromRows({{1, 0}, {0, Complex(0, 1)}});
}
inline Matrix RY(double theta) {
    const double c = std::cos(0.5 * theta), s = std::sin(0.5 * theta);
    return Matrix::fromRows({{c, -s}, {s, c}});
}
// CX with control = targets[0] (least significant bit), target = targets[1].
inline Matrix CX() {
    return Matrix::fromRows({{1, 0, 0, 0}, {0, 0, 0, 1}, {0, 0, 1, 0}, {0, 1, 0, 0}});
}

inline noise::AttachedChannel attach(const noise::ChannelPtr& c, std::vector<QubitIndex> qubits,
                                     double durationS = 0.0,
                                     noise::Placement p = noise::Placement::After) {
    return noise::AttachedChannel{c, p, std::move(qubits), noise::Context{durationS, 0.0, false}};
}

// Unwraps a Result<ChannelPtr> inside a test.
inline noise::ChannelPtr channel(Result<noise::ChannelPtr> r) {
    INFO((r ? std::string() : r.error().format()));
    REQUIRE(r.has_value());
    return *r;
}

// Applies one attached channel with default options, requiring success.
inline void apply(qsim::IBackend& b, const noise::AttachedChannel& ac, core::Random& rng,
                  const noise::ApplyOptions& options = {}) {
    noise::ApplyReport report;
    NOISE_REQUIRE_OK(noise::applyChannel(b, ac, rng, options, report));
}

inline double expectation(const qsim::IBackend& b, std::string_view label) {
    auto p = qsim::PauliString::parse(label);
    REQUIRE(p.has_value());
    auto e = b.expectation(*p);
    NOISE_REQUIRE_OK(e);
    return *e;
}

} // namespace ntest
