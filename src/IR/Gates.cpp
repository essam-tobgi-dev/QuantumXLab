// Spec 14 §3 — gate definition table and single-qubit matrices.
#include "IR/Gates.hpp"
#include "Numerics/Matrix.hpp"
#include <cmath>
#include <format>
#include <unordered_map>

namespace qlab::ir::gates {
namespace {
using num::Complex;
using num::Matrix;
constexpr double kPi = 3.14159265358979323846;

const std::vector<GateDef>& table() {
    static const std::vector<GateDef> t = {
        // name, params, qubits, class, selfInv, diag, rotation, inverse, description
        {"id", 0, 1, GateClass::Identity, true, true, false, "id", "identity"},
        {"x", 0, 1, GateClass::PauliX, true, false, false, "x", "Pauli X"},
        {"y", 0, 1, GateClass::Generic, true, false, false, "y", "Pauli Y"},
        {"z", 0, 1, GateClass::PauliZ, true, true, false, "z", "Pauli Z"},
        {"h", 0, 1, GateClass::Generic, true, false, false, "h", "Hadamard"},
        {"s", 0, 1, GateClass::Diagonal, false, true, false, "sdg", "phase S = √Z"},
        {"sdg", 0, 1, GateClass::Diagonal, false, true, false, "s", "S†"},
        {"t", 0, 1, GateClass::Diagonal, false, true, false, "tdg", "T = Z^{1/4}"},
        {"tdg", 0, 1, GateClass::Diagonal, false, true, false, "t", "T†"},
        {"sx", 0, 1, GateClass::Generic, false, false, false, "sxdg", "√X"},
        {"sxdg", 0, 1, GateClass::Generic, false, false, false, "sx", "√X†"},
        {"p", 1, 1, GateClass::Diagonal, false, true, true, "p", "phase P(λ)"},
        {"phase", 1, 1, GateClass::Diagonal, false, true, true, "phase", "phase (legacy)"},
        {"u1", 1, 1, GateClass::Diagonal, false, true, true, "u1", "U1(λ) (legacy)"},
        {"rx", 1, 1, GateClass::Generic, false, false, true, "rx", "R_x(θ)"},
        {"ry", 1, 1, GateClass::Generic, false, false, true, "ry", "R_y(θ)"},
        {"rz", 1, 1, GateClass::Diagonal, false, true, true, "rz", "R_z(θ)"},
        {"u2", 2, 1, GateClass::Generic, false, false, false, "", "U2(φ,λ) (legacy)"},
        {"u3", 3, 1, GateClass::Generic, false, false, false, "", "U3(θ,φ,λ) (legacy)"},
        {"U", 3, 1, GateClass::Generic, false, false, false, "", "built-in U(θ,φ,λ)"},
        {"gphase", 1, 0, GateClass::Diagonal, false, true, true, "gphase", "global phase"},
        // two-qubit
        {"cx", 0, 2, GateClass::Cnot, true, false, false, "cx", "CNOT"},
        {"CX", 0, 2, GateClass::Cnot, true, false, false, "CX", "CNOT (legacy)"},
        {"cy", 0, 2, GateClass::Generic, true, false, false, "cy", "controlled Y"},
        {"cz", 0, 2, GateClass::Cz, true, true, false, "cz", "controlled Z"},
        {"ch", 0, 2, GateClass::Generic, true, false, false, "ch", "controlled H"},
        {"swap", 0, 2, GateClass::Swap, true, false, false, "swap", "SWAP"},
        {"cp", 1, 2, GateClass::Diagonal2, false, true, true, "cp", "controlled phase"},
        {"crx", 1, 2, GateClass::Generic, false, false, true, "crx", "controlled R_x"},
        {"cry", 1, 2, GateClass::Generic, false, false, true, "cry", "controlled R_y"},
        {"crz", 1, 2, GateClass::Diagonal2, false, true, true, "crz", "controlled R_z"},
        {"cu", 4, 2, GateClass::Generic, false, false, false, "", "controlled U(θ,φ,λ,γ)"},
        {"rxx", 1, 2, GateClass::Generic, false, false, true, "rxx", "R_xx(θ)"},
        {"ryy", 1, 2, GateClass::Generic, false, false, true, "ryy", "R_yy(θ)"},
        {"rzz", 1, 2, GateClass::Diagonal2, false, true, true, "rzz", "R_zz(θ)"},
        {"ms", 1, 2, GateClass::Generic, false, false, true, "ms", "Mølmer–Sørensen XX(θ)"},
        {"ecr", 0, 2, GateClass::Generic, true, false, false, "ecr", "echoed cross-resonance"},
        {"iswap", 0, 2, GateClass::Generic, false, false, false, "", "iSWAP"},
        {"siswap", 0, 2, GateClass::Generic, false, false, false, "", "√iSWAP"},
        {"fsim", 2, 2, GateClass::Generic, false, false, true, "fsim", "fSim(θ,φ)"},
        // three-qubit
        {"ccx", 0, 3, GateClass::Generic, true, false, false, "ccx", "Toffoli"},
        {"cswap", 0, 3, GateClass::Generic, true, false, false, "cswap", "Fredkin"},
        // synthetic: matrix carried on the node itself
        {"unitary", 0, 0, GateClass::Generic, false, false, false, "", "explicit matrix"},
    };
    return t;
}
} // namespace

const std::vector<GateDef>& all() { return table(); }

const GateDef* find(std::string_view name) {
    static const std::unordered_map<std::string_view, const GateDef*> idx = [] {
        std::unordered_map<std::string_view, const GateDef*> m;
        for (const auto& g : table()) m.emplace(g.name, &g);
        return m;
    }();
    auto it = idx.find(name);
    return it == idx.end() ? nullptr : it->second;
}
bool isKnown(std::string_view name) { return find(name) != nullptr; }

Matrix u3(double theta, double phi, double lambda) {
    const double c = std::cos(theta / 2.0), s = std::sin(theta / 2.0);
    Matrix m(2, 2);
    m(0, 0) = c;
    m(0, 1) = -std::polar(1.0, lambda) * s;
    m(1, 0) = std::polar(1.0, phi) * s;
    m(1, 1) = std::polar(1.0, phi + lambda) * c;
    return m;
}

std::optional<GateRewrite> inverseOf(std::string_view name, std::span<const double> params) {
    const GateDef* d = find(name);
    if (!d) return std::nullopt;
    if (d->rotation) {
        std::vector<double> p(params.begin(), params.end());
        for (auto& v : p) v = -v;
        return GateRewrite{std::string(d->inverseName.empty() ? name : d->inverseName), std::move(p)};
    }
    if (!d->inverseName.empty())
        return GateRewrite{std::string(d->inverseName), std::vector<double>(params.begin(), params.end())};
    if (name == "u3" || name == "U") {
        if (params.size() != 3) return std::nullopt;
        return GateRewrite{std::string(name), {-params[0], -params[2], -params[1]}};
    }
    if (name == "u2") { // U(π/2,φ,λ)† = U(−π/2,−λ,−φ), T02 (1.1)
        if (params.size() != 2) return std::nullopt;
        return GateRewrite{"u3", {-kPi / 2, -params[1], -params[0]}};
    }
    if (name == "cu") {
        if (params.size() != 4) return std::nullopt;
        return GateRewrite{"cu", {-params[0], -params[2], -params[1], -params[3]}};
    }
    return std::nullopt; // iswap, siswap: no named inverse, the node carries `adjoint`
}

std::optional<GateRewrite> powerOf(std::string_view name, std::span<const double> params, double k) {
    const GateDef* d = find(name);
    if (!d || !d->rotation) return std::nullopt;
    std::vector<double> p(params.begin(), params.end());
    for (auto& v : p) v *= k;
    return GateRewrite{std::string(name), std::move(p)};
}

std::optional<GateRewrite> namedPower(std::string_view name, std::span<const double> params, double k) {
    const GateDef* d = find(name);
    if (!d || !params.empty()) return std::nullopt;
    const double kr = std::nearbyint(k);
    const bool integral = std::abs(k - kr) < 1e-12;
    const auto odd = [&] { return std::fmod(std::abs(kr), 2.0) == 1.0; };
    // Phase family: every member is exactly p(λ₀), so the power is p(k·λ₀) for any real k.
    double base = 0;
    if (name == "z") base = kPi;
    else if (name == "s") base = kPi / 2;
    else if (name == "sdg") base = -kPi / 2;
    else if (name == "t") base = kPi / 4;
    else if (name == "tdg") base = -kPi / 4;
    if (base != 0) {
        const double lambda = k * base;
        const double r = std::remainder(lambda, 2 * kPi); // p(λ) has period 2π exactly
        constexpr double eps = 1e-12;
        if (std::abs(r) < eps) return GateRewrite{"", {}};
        if (std::abs(std::abs(r) - kPi) < eps) return GateRewrite{"z", {}};
        if (std::abs(r - kPi / 2) < eps) return GateRewrite{"s", {}};
        if (std::abs(r + kPi / 2) < eps) return GateRewrite{"sdg", {}};
        if (std::abs(r - kPi / 4) < eps) return GateRewrite{"t", {}};
        if (std::abs(r + kPi / 4) < eps) return GateRewrite{"tdg", {}};
        return GateRewrite{"p", {lambda}};
    }
    if (!integral) return std::nullopt;
    if (name == "sx" || name == "sxdg") { // SX² = X and SX⁴ = I exactly
        int m = static_cast<int>(std::fmod(kr, 4.0));
        if (m < 0) m += 4;
        if (name == "sxdg") m = (4 - m) % 4;
        static constexpr std::string_view cycle[4] = {"", "sx", "x", "sxdg"};
        return GateRewrite{std::string(cycle[m]), {}};
    }
    if (d->selfInverse) return GateRewrite{odd() ? std::string(name) : std::string(), {}};
    return std::nullopt;
}

namespace {
struct ControlledEntry { std::string_view name, base; std::size_t controls; };
// First entry per (base, controls) is the canonical name; `CX` is the legacy spelling of `cx`.
constexpr ControlledEntry kControlled[] = {
    {"cx", "x", 1}, {"CX", "x", 1}, {"ccx", "x", 2}, {"cy", "y", 1}, {"cz", "z", 1}, {"ch", "h", 1},
    {"cp", "p", 1}, {"crx", "rx", 1}, {"cry", "ry", 1}, {"crz", "rz", 1}, {"cswap", "swap", 1},
};
} // namespace

std::optional<ControlledRewrite> splitControlled(std::string_view name, std::span<const double> params) {
    // cu(θ,φ,λ,γ) = ctrl @ (e^{iγ} U): a plain controlled U only when γ is exactly zero.
    if (name == "cu") {
        if (params.size() != 4 || params[3] != 0.0) return std::nullopt;
        return ControlledRewrite{GateRewrite{"U", {params[0], params[1], params[2]}}, 1};
    }
    for (const auto& e : kControlled)
        if (e.name == name)
            return ControlledRewrite{GateRewrite{std::string(e.base), std::vector<double>(params.begin(), params.end())},
                                     e.controls};
    return std::nullopt;
}

std::optional<GateRewrite> joinControlled(std::string_view base, std::span<const double> params,
                                          std::size_t controls) {
    if (controls == 0) return GateRewrite{std::string(base), std::vector<double>(params.begin(), params.end())};
    if (base == "U" && controls == 1 && params.size() == 3)
        return GateRewrite{"cu", {params[0], params[1], params[2], 0.0}};
    for (const auto& e : kControlled)
        if (e.base == base && e.controls == controls)
            return GateRewrite{std::string(e.name), std::vector<double>(params.begin(), params.end())};
    return std::nullopt;
}

GateClass classify(std::string_view name, std::span<const double> params) {
    const GateDef* d = find(name);
    if (!d) return GateClass::Generic;
    if (d->rotation && d->nQubits == 1 && !params.empty()) {
        bool allZero = true;
        for (double v : params) allZero = allZero && std::abs(v) < 1e-15;
        if (allZero) return GateClass::Identity;
    }
    return d->cls;
}

} // namespace qlab::ir::gates
