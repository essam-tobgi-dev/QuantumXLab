// Spec 14 §5.1, §5.3; T02 §4 — wire actions, the commutation rule, and inverse detection.
#include "Compiler/Commutation.hpp"
#include "Numerics/Matrix.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>
#include <unordered_map>

namespace qlab::compiler {
namespace {
constexpr double kTol = 1e-12;
constexpr std::uint8_t Z = kActsZ, X = kActsX, N = 0;

struct ActionEntry {
    std::string_view name;
    std::array<std::uint8_t, 3> act;
};
// Action per operand. Gates absent from the table (h, y, ry, swap, iswap, siswap, fsim, ryy, …)
// act generically on every operand.
constexpr ActionEntry kActions[] = {
    {"id", {Z | X, N, N}}, {"z", {Z, N, N}},     {"s", {Z, N, N}},   {"sdg", {Z, N, N}},
    {"t", {Z, N, N}},      {"tdg", {Z, N, N}},   {"p", {Z, N, N}},   {"phase", {Z, N, N}},
    {"u1", {Z, N, N}},     {"rz", {Z, N, N}},    {"x", {X, N, N}},   {"sx", {X, N, N}},
    {"sxdg", {X, N, N}},   {"rx", {X, N, N}},    {"cx", {Z, X, N}},  {"CX", {Z, X, N}},
    {"cz", {Z, Z, N}},     {"cp", {Z, Z, N}},    {"crz", {Z, Z, N}}, {"rzz", {Z, Z, N}},
    {"cy", {Z, N, N}},     {"ch", {Z, N, N}},    {"cry", {Z, N, N}}, {"cu", {Z, N, N}},
    {"crx", {Z, X, N}},    {"rxx", {X, X, N}},   {"ms", {X, X, N}},  {"ecr", {N, X, N}},
    {"ccx", {Z, Z, X}},    {"cswap", {Z, N, N}},
};

bool closeParams(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t k = 0; k < a.size(); ++k)
        if (!(std::abs(a[k] - b[k]) <= kTol))
            return false;
    return true;
}
bool samePolarity(const ir::Gate& a, const ir::Gate& b) {
    for (std::size_t j = 0; j < a.controls.size(); ++j)
        if (a.isNegControl(j) != b.isNegControl(j))
            return false;
    return true;
}
} // namespace

std::uint8_t wireAction(const ir::Gate& g, std::size_t k) {
    if (g.opaque)
        return 0;
    if (k >= g.targets.size())
        return kActsZ; // a control of either polarity
    if (g.targets.size() == 1 && (g.custom || g.name == "U" || g.name == "u2" || g.name == "u3")) {
        auto m = ir::baseMatrixOf(g);
        if (!m)
            return 0;
        std::uint8_t mask = 0;
        if (std::abs((*m)(0, 1)) < kTol && std::abs((*m)(1, 0)) < kTol)
            mask |= kActsZ;
        if (std::abs((*m)(0, 0) - (*m)(1, 1)) < kTol && std::abs((*m)(0, 1) - (*m)(1, 0)) < kTol)
            mask |= kActsX;
        return mask;
    }
    if (g.custom || k >= 3)
        return 0;
    static const std::unordered_map<std::string_view, std::array<std::uint8_t, 3>> index = [] {
        std::unordered_map<std::string_view, std::array<std::uint8_t, 3>> m;
        for (const ActionEntry& e : kActions)
            m.emplace(e.name, e.act);
        return m;
    }();
    const auto it = index.find(g.name);
    return it == index.end() ? std::uint8_t{0} : it->second[k];
}

bool commutes(const ir::Gate& a, const ir::Gate& b) {
    if (a.opaque || b.opaque)
        return false;
    const auto wa = a.wires(), wb = b.wires();
    for (std::size_t i = 0; i < wa.size(); ++i)
        for (std::size_t j = 0; j < wb.size(); ++j)
            if (wa[i] == wb[j] && (wireAction(a, i) & wireAction(b, j)) == 0)
                return false;
    return true;
}

bool operandSymmetric(std::string_view n) {
    return n == "cz" || n == "cp" || n == "swap" || n == "rzz" || n == "rxx" || n == "ryy" ||
           n == "ms" || n == "iswap" || n == "siswap" || n == "fsim";
}

bool sameOperands(const ir::Gate& a, const ir::Gate& b) {
    if (a.controls != b.controls || !samePolarity(a, b))
        return false;
    if (a.targets == b.targets)
        return true;
    return a.targets.size() == 2 && b.targets.size() == 2 && !a.custom && !b.custom &&
           operandSymmetric(a.name) && operandSymmetric(b.name) && a.targets[0] == b.targets[1] &&
           a.targets[1] == b.targets[0];
}

bool isNamedInverseOf(const ir::Gate& a, const ir::Gate& b) {
    if (a.opaque || b.opaque || a.custom || b.custom || !sameOperands(a, b))
        return false;
    if (a.adjoint != b.adjoint)
        return a.name == b.name && closeParams(a.params, b.params);
    if (a.adjoint)
        return false;
    const ir::GateDef* def = ir::gates::find(a.name);
    if (!def)
        return false;
    if (def->selfInverse)
        return a.name == b.name; // x·x, h·h, cx·cx, swap·swap, ecr·ecr
    if (def->rotation) {         // θ → −θ under the same name
        if (a.name != b.name || a.params.size() != b.params.size())
            return false;
        for (std::size_t k = 0; k < a.params.size(); ++k)
            if (!(std::abs(a.params[k] + b.params[k]) <= kTol))
                return false;
        return true;
    }
    const auto rw = ir::gates::inverseOf(a.name, a.params); // s·sdg, sx·sxdg, U·U†, cu·cu†
    return rw && rw->name == b.name && closeParams(rw->params, b.params);
}

bool isInverseOf(const ir::Gate& a, const ir::Gate& b) {
    if (a.opaque || b.opaque || !sameOperands(a, b))
        return false;
    if (isNamedInverseOf(a, b))
        return true;
    if (a.width() > 2 || a.width() == 0)
        return false;
    auto ma = ir::matrixOf(a), mb = ir::matrixOf(b);
    if (!ma || !mb || ma->rows != mb->rows)
        return false;
    const num::Matrix prod = num::matmul(*mb, *ma);
    const num::Complex d0 = prod(0, 0);
    if (std::abs(std::abs(d0) - 1.0) > kTol)
        return false;
    if (!a.controls.empty() && std::abs(d0 - 1.0) > kTol)
        return false;
    for (std::size_t r = 0; r < prod.rows; ++r)
        for (std::size_t c = 0; c < prod.cols; ++c)
            if (std::abs(prod(r, c) - (r == c ? d0 : num::Complex{})) > kTol)
                return false;
    return true;
}

std::optional<double> diagonalAngle(const ir::Gate& g) {
    if (g.opaque || !g.controls.empty() || g.targets.size() != 1)
        return std::nullopt;
    constexpr double pi = std::numbers::pi;
    const double sign = g.adjoint ? -1.0 : 1.0;
    if (!g.custom) {
        if (g.name == "rz" || g.name == "p" || g.name == "phase" || g.name == "u1")
            return sign * g.params[0];
        if (g.name == "z")
            return pi;
        if (g.name == "s")
            return sign * pi / 2;
        if (g.name == "sdg")
            return -sign * pi / 2;
        if (g.name == "t")
            return sign * pi / 4;
        if (g.name == "tdg")
            return -sign * pi / 4;
        if (g.name == "id")
            return 0.0;
    }
    if ((wireAction(g, 0) & kActsZ) == 0)
        return std::nullopt;
    auto m = ir::baseMatrixOf(g);
    if (!m)
        return std::nullopt;
    return std::arg((*m)(1, 1)) - std::arg((*m)(0, 0));
}

} // namespace qlab::compiler
