#pragma once
// Shared fixtures for the Pulse tests: shipped pulse libraries loaded once per test binary, and
// small two-level propagation helpers used as independent physics oracles.
#include "Hardware/Hardware.hpp"
#include "Pulse/Pulse.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <complex>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pulsetest {

using Complex = std::complex<double>;
using U2 = std::array<Complex, 4>; // row-major 2×2

inline const std::vector<std::string>& shippedDevices() {
    static const std::vector<std::string> ids = {"sc_fixed_5",      "sc_heavyhex_27",
                                                 "sc_heavyhex_127", "sc_tunable_grid_54",
                                                 "ion_chain_11",    "ion_chain_32"};
    return ids;
}

inline const qlab::pulse::PulseLibrary& library(const std::string& id) {
    static std::map<std::string, std::unique_ptr<qlab::pulse::PulseLibrary>> cache;
    auto it = cache.find(id);
    if (it == cache.end()) {
        auto lib = qlab::pulse::loadPulses(qlab::hw::deviceRoot() / id);
        if (!lib)
            FAIL("loading " << id << ": " << lib.error().format());
        it = cache.emplace(id, std::make_unique<qlab::pulse::PulseLibrary>(std::move(*lib))).first;
    }
    return *it->second;
}

inline U2 mul(const U2& a, const U2& b) {
    return {a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3], a[2] * b[0] + a[3] * b[2],
            a[2] * b[1] + a[3] * b[3]};
}

// exp(−i h·(ωx X + ωy Y)/2) for a constant rotation vector, exact.
inline U2 rotationStep(double wx, double wy, double h) {
    const double w = std::hypot(wx, wy);
    if (w == 0.0)
        return {1.0, 0.0, 0.0, 1.0};
    const double c = std::cos(0.5 * w * h), s = std::sin(0.5 * w * h);
    const double nx = wx / w, ny = wy / w;
    const Complex I(0.0, 1.0);
    // cos − i sin (nx X + ny Y):  X = [[0,1],[1,0]], Y = [[0,−i],[i,0]]
    return {c, -I * s * Complex(nx, -ny), -I * s * Complex(nx, ny), c};
}

// Two-level propagator of piecewise-constant rotating-frame samples Ω_k [rad/s] held for dt,
// H = (Re Ω X + Im Ω Y)/2 (the convention of pulse::DriveEnvelope, T05 (7.1)).
inline U2 propagate(const std::vector<Complex>& omega, double dt) {
    U2 u{1.0, 0.0, 0.0, 1.0};
    for (const Complex& w : omega)
        u = mul(rotationStep(w.real(), w.imag(), dt), u);
    return u;
}

inline U2 rx(double theta) {
    return rotationStep(1.0, 0.0, theta);
}
inline U2 rz(double theta) {
    return {std::polar(1.0, -0.5 * theta), 0.0, 0.0, std::polar(1.0, 0.5 * theta)};
}

// Process fidelity |Tr(U†V)|²/4 of two single-qubit unitaries (phase-insensitive).
inline double processFidelity(const U2& u, const U2& v) {
    const Complex tr = std::conj(u[0]) * v[0] + std::conj(u[2]) * v[2] + std::conj(u[1]) * v[1] +
                       std::conj(u[3]) * v[3];
    return std::norm(tr) / 4.0;
}

} // namespace pulsetest
