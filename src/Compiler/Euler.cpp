// Spec 14 §4.2, §5.2; T02 (1.1), (1.4), (1.5) — Euler angles and native single-qubit synthesis.
// With sx = e^{iπ/4} Rx(π/2) the exact forms are
//   U(θ,φ,λ)   = e^{i((φ+λ)/2 + π/2)} · Rz(φ+π) · sx · Rz(θ+π) · sx · Rz(λ)
//   U(π/2,φ,λ) = e^{i((φ+λ)/2 − π/4)} · Rz(φ+π/2) · sx · Rz(λ−π/2)
//   U(π,φ,λ)   = e^{i(φ+λ+π)/2}      · x · Rz(λ−φ+π)
//   U(0,φ,λ)   = e^{i(φ+λ)/2}        · Rz(φ+λ)
//   U(θ,φ,λ)   = e^{i(φ+λ)/2}        · Rz(φ) · Ry(θ) · Rz(λ)                      (ZYZ, ions)
// (matrix order; the rightmost factor acts first). Rz(a + 2πk) = (−1)^k Rz(a) is tracked in `phase`.
#include "Compiler/Euler.hpp"
#include "Compiler/CircuitUtil.hpp"
#include <cmath>
#include <numbers>

namespace qlab::compiler {
namespace {
constexpr double kPi = std::numbers::pi;

// Appends rz(a) reduced to (−π, π]; a multiple of 2π is dropped. Returns the phase π·k it leaves.
double pushRz(std::vector<ir::gates::GateRewrite>& out, double a) {
    long k = 0;
    const double w = wrapAngle(a, &k);
    if (std::abs(w) > kAngleEps) out.push_back({"rz", {w}});
    return kPi * static_cast<double>(k);
}
bool near(double a, double b) { return std::abs(wrapAngle(a - b)) < kAngleEps; }
} // namespace

EulerAngles eulerAngles(num::ConstMatrixView m) {
    EulerAngles e;
    const double a00 = std::abs(m(0, 0)), a10 = std::abs(m(1, 0));
    e.theta = 2.0 * std::atan2(a10, a00);
    if (a10 < kAngleEps) {            // diagonal: only φ + λ is defined
        e.theta = 0.0;
        e.phase = std::arg(m(0, 0));
        e.phi = 0.0;
        e.lambda = std::arg(m(1, 1)) - e.phase;
    } else if (a00 < kAngleEps) {     // anti-diagonal: only φ − λ is defined
        e.theta = kPi;
        e.lambda = 0.0;
        e.phase = std::arg(-m(0, 1));
        e.phi = std::arg(m(1, 0)) - e.phase;
    } else {
        e.phase = std::arg(m(0, 0));
        e.phi = std::arg(m(1, 0)) - e.phase;
        e.lambda = std::arg(-m(0, 1)) - e.phase;
    }
    return e;
}

bool isFrameChange(std::string_view n) {
    return n == "rz" || n == "p" || n == "phase" || n == "u1" || n == "z" || n == "s" || n == "sdg" ||
           n == "t" || n == "tdg" || n == "id";
}

std::size_t OneQubitSequence::pulses() const {
    std::size_t n = 0;
    for (const auto& g : gates)
        if (!isFrameChange(g.name)) ++n;
    return n;
}

OneQubitSequence synthesize1q(const EulerAngles& a, Basis1q basis) {
    OneQubitSequence s;
    const double half = (a.phi + a.lambda) / 2.0;
    const bool diagonal = a.theta < kAngleEps;
    const bool flip = std::abs(a.theta - kPi) < kAngleEps;

    if (basis == Basis1q::U) {
        s.phase = a.phase;
        if (diagonal) {
            const double l = wrapAngle(a.phi + a.lambda);      // diag(1, e^{iλ}) is 2π-periodic
            if (std::abs(l) > kAngleEps) s.gates.push_back({"U", {0.0, 0.0, l}});
        } else {
            s.gates.push_back({"U", {a.theta, wrapAngle(a.phi), wrapAngle(a.lambda)}});
        }
        return s;
    }
    if (diagonal) {
        s.phase = a.phase + half + pushRz(s.gates, a.phi + a.lambda);
        return s;
    }
    if (basis == Basis1q::ZYZ) {
        if (near(a.phi, -kPi / 2) && near(a.lambda, kPi / 2)) {      // Rz(−π/2) Ry(θ) Rz(π/2) = Rx(θ)
            long k1 = 0, k2 = 0;
            wrapAngle(a.phi + kPi / 2, &k1);
            wrapAngle(a.lambda - kPi / 2, &k2);
            s.gates.push_back({"rx", {a.theta}});
            s.phase = a.phase + half + kPi * static_cast<double>(k1 + k2);
            return s;
        }
        if (flip && near(a.phi - a.lambda, kPi)) {                    // Rz(−π) Ry(π) = −iX = Rx(π)
            long k = 0;
            wrapAngle(a.phi - a.lambda + kPi, &k);
            s.gates.push_back({"rx", {kPi}});
            s.phase = a.phase + half + kPi * static_cast<double>(k);
            return s;
        }
        if (flip) {                                                   // Rz(φ) Ry(π) Rz(λ) = Rz(φ−λ) Ry(π)
            s.gates.push_back({"ry", {kPi}});
            s.phase = a.phase + half + pushRz(s.gates, a.phi - a.lambda);
            return s;
        }
        s.phase = a.phase + half;
        s.phase += pushRz(s.gates, a.lambda);
        s.gates.push_back({"ry", {a.theta}});
        s.phase += pushRz(s.gates, a.phi);
        return s;
    }
    // ZSX
    if (flip) {
        s.phase = a.phase + half + kPi / 2 + pushRz(s.gates, a.lambda - a.phi + kPi);
        s.gates.push_back({"x", {}});
        return s;
    }
    if (std::abs(a.theta - kPi / 2) < kAngleEps) {
        s.phase = a.phase + half - kPi / 4;
        s.phase += pushRz(s.gates, a.lambda - kPi / 2);
        s.gates.push_back({"sx", {}});
        s.phase += pushRz(s.gates, a.phi + kPi / 2);
        return s;
    }
    s.phase = a.phase + half + kPi / 2;
    s.phase += pushRz(s.gates, a.lambda);
    s.gates.push_back({"sx", {}});
    s.phase += pushRz(s.gates, a.theta + kPi);
    s.gates.push_back({"sx", {}});
    s.phase += pushRz(s.gates, a.phi + kPi);
    return s;
}

OneQubitSequence synthesize1q(num::ConstMatrixView m, Basis1q basis) { return synthesize1q(eulerAngles(m), basis); }

} // namespace qlab::compiler
