// Spec 14 §4.3; T02 §2.3, §3 — the decomposition rule table. Operand k of a step is operand k of
// the source gate (for controlled gates the control comes first, as in OpenQASM). Every rule is
// exact with its `phase` and is checked against `ir::gates::matrix` by tests/Compiler.
#include "Compiler/Rules.hpp"
#include <numbers>

namespace qlab::compiler {
namespace {
constexpr double kPi = std::numbers::pi;

Affine K(double v) {
    return Affine{v, {}};
}
// coeff · p[k] + c0
Affine P(std::size_t k, double coeff = 1.0, double c0 = 0.0) {
    Affine a;
    a.c0 = c0;
    a.c[k] = coeff;
    return a;
}
Affine L(double c0, double p0, double p1, double p2, double p3) {
    return Affine{c0, {p0, p1, p2, p3}};
}

RuleStep g(std::string_view gate, std::vector<std::uint8_t> ops, std::vector<Affine> params = {}) {
    return RuleStep{gate, std::move(ops), std::move(params)};
}

std::vector<Rule> build() {
    std::vector<Rule> r;
    const Affine none{};
    // ---- generic rules: single-qubit gates + cx (T02 §2.3, §3) ------------------------------
    r.push_back({"CX", "cx", {g("cx", {0, 1})}, none, "T02 (2.1)"});
    r.push_back({"cz", "cx", {g("h", {1}), g("cx", {0, 1}), g("h", {1})}, none, "T02 §2.3"});
    // CY = (I⊗S) CX (I⊗S†) in matrix order: sdg acts first.
    r.push_back({"cy", "cx", {g("sdg", {1}), g("cx", {0, 1}), g("s", {1})}, none, "T02 §4"});
    // H = Ry(−π/4) X Ry(π/4): ry(π/4) acts first.
    r.push_back({"ch",
                 "cx",
                 {g("ry", {1}, {K(kPi / 4)}), g("cx", {0, 1}), g("ry", {1}, {K(-kPi / 4)})},
                 none,
                 "T02 §4"});
    r.push_back(
        {"swap", "cx", {g("cx", {0, 1}), g("cx", {1, 0}), g("cx", {0, 1})}, none, "T02 §4"});
    r.push_back({"cp",
                 "cx",
                 {g("p", {0}, {P(0, 0.5)}), g("cx", {0, 1}), g("p", {1}, {P(0, -0.5)}),
                  g("cx", {0, 1}), g("p", {1}, {P(0, 0.5)})},
                 none,
                 "T02 §3.1"});
    r.push_back(
        {"crz",
         "cx",
         {g("rz", {1}, {P(0, 0.5)}), g("cx", {0, 1}), g("rz", {1}, {P(0, -0.5)}), g("cx", {0, 1})},
         none,
         "T02 (3.3)"});
    r.push_back(
        {"cry",
         "cx",
         {g("ry", {1}, {P(0, 0.5)}), g("cx", {0, 1}), g("ry", {1}, {P(0, -0.5)}), g("cx", {0, 1})},
         none,
         "T02 (3.3)"});
    r.push_back({"crx",
                 "cx",
                 {g("h", {1}), g("rz", {1}, {P(0, 0.5)}), g("cx", {0, 1}),
                  g("rz", {1}, {P(0, -0.5)}), g("cx", {0, 1}), g("h", {1})},
                 none,
                 "T02 §3.1"});
    // cu(θ,φ,λ,γ): C = Rz((λ−φ)/2), B = Ry(−θ/2) Rz(−(φ+λ)/2), A = Rz(φ) Ry(θ/2), P(γ + (φ+λ)/2)_c.
    r.push_back(
        {"cu",
         "cx",
         {g("rz", {1}, {L(0, 0, -0.5, 0.5, 0)}), g("cx", {0, 1}),
          g("rz", {1}, {L(0, 0, -0.5, -0.5, 0)}), g("ry", {1}, {P(0, -0.5)}), g("cx", {0, 1}),
          g("ry", {1}, {P(0, 0.5)}), g("rz", {1}, {P(1)}), g("p", {0}, {L(0, 0, 0.5, 0.5, 1)})},
         none,
         "T02 (3.1)-(3.2)"});
    r.push_back(
        {"rzz", "cx", {g("cx", {0, 1}), g("rz", {1}, {P(0)}), g("cx", {0, 1})}, none, "T02 §2.2"});
    r.push_back({"rxx",
                 "cx",
                 {g("h", {0}), g("h", {1}), g("cx", {0, 1}), g("rz", {1}, {P(0)}), g("cx", {0, 1}),
                  g("h", {0}), g("h", {1})},
                 none,
                 "spec 14 §4.3"});
    r.push_back({"ms", "cx", {g("rxx", {0, 1}, {P(0)})}, none, "T06 §6"});
    r.push_back({"ryy",
                 "cx",
                 {g("rx", {0}, {K(kPi / 2)}), g("rx", {1}, {K(kPi / 2)}), g("cx", {0, 1}),
                  g("rz", {1}, {P(0)}), g("cx", {0, 1}), g("rx", {0}, {K(-kPi / 2)}),
                  g("rx", {1}, {K(-kPi / 2)})},
                 none,
                 "T02 §2.2"});
    // CX = e^{−iπ/4} (S_c ⊗ sx_t) · ECR · X_c, hence ECR = e^{+iπ/4} (S†_c ⊗ sx†_t) · CX · X_c.
    r.push_back({"ecr",
                 "cx",
                 {g("x", {0}), g("cx", {0, 1}), g("sdg", {0}), g("sxdg", {1})},
                 K(kPi / 4),
                 "T02 §2.3"});
    r.push_back({"iswap",
                 "cx",
                 {g("rxx", {0, 1}, {K(-kPi / 2)}), g("ryy", {0, 1}, {K(-kPi / 2)})},
                 none,
                 "T02 §2.2"});
    r.push_back({"siswap",
                 "cx",
                 {g("rxx", {0, 1}, {K(-kPi / 4)}), g("ryy", {0, 1}, {K(-kPi / 4)})},
                 none,
                 "T02 §2.2"});
    r.push_back(
        {"fsim",
         "cx",
         {g("rxx", {0, 1}, {P(0)}), g("ryy", {0, 1}, {P(0)}), g("cp", {0, 1}, {P(1, -1.0)})},
         none,
         "T02 §2.2"});
    // Toffoli with 6 cx and T-count 7 (T02 §3.2); operands a, b controls, t target.
    r.push_back({"ccx",
                 "cx",
                 {g("h", {2}), g("cx", {1, 2}), g("tdg", {2}), g("cx", {0, 2}), g("t", {2}),
                  g("cx", {1, 2}), g("tdg", {2}), g("cx", {0, 2}), g("t", {1}), g("t", {2}),
                  g("h", {2}), g("cx", {0, 1}), g("t", {0}), g("tdg", {1}), g("cx", {0, 1})},
                 none,
                 "T02 §3.2"});
    r.push_back(
        {"cswap", "cx", {g("cx", {2, 1}), g("ccx", {0, 1, 2}), g("cx", {2, 1})}, none, "T02 §3.3"});

    // ---- cx in each native entangler (T02 §2.3) ---------------------------------------------
    r.push_back({"cx", "cz", {g("h", {1}), g("cz", {0, 1}), g("h", {1})}, none, "T02 §2.3"});
    r.push_back({"cx",
                 "ecr",
                 {g("x", {0}), g("ecr", {0, 1}), g("s", {0}), g("sx", {1})},
                 K(-kPi / 4),
                 "T02 §2.3"});
    r.push_back(
        {"cx",
         "rxx",
         {g("ry", {0}, {K(kPi / 2)}), g("rxx", {0, 1}, {K(kPi / 2)}), g("rx", {0}, {K(-kPi / 2)}),
          g("rx", {1}, {K(-kPi / 2)}), g("ry", {0}, {K(-kPi / 2)})},
         K(-kPi / 4),
         "T02 §2.3, T06 §6.3"});
    // √iSWAP · X_a · √iSWAP · X_a = exp(iπ/4 XX) = rxx(−π/2); rxx(π/2) = −i X_a X_b rxx(−π/2).
    r.push_back({"cx",
                 "siswap",
                 {g("ry", {0}, {K(kPi / 2)}), g("x", {0}), g("siswap", {0, 1}), g("x", {0}),
                  g("siswap", {0, 1}), g("x", {0}), g("x", {1}), g("rx", {0}, {K(-kPi / 2)}),
                  g("rx", {1}, {K(-kPi / 2)}), g("ry", {0}, {K(-kPi / 2)})},
                 K(-3 * kPi / 4),
                 "T02 §2.3"});
    r.push_back({"iswap", "siswap", {g("siswap", {0, 1}), g("siswap", {0, 1})}, none, "T02 §2.2"});

    // ---- one Mølmer–Sørensen gate per ZZ/YY-type interaction on ions (T06 §6) -----------------
    r.push_back({"rzz",
                 "rxx",
                 {g("h", {0}), g("h", {1}), g("rxx", {0, 1}, {P(0)}), g("h", {0}), g("h", {1})},
                 none,
                 "T02 §4"});
    r.push_back({"ryy",
                 "rxx",
                 {g("sdg", {0}), g("sdg", {1}), g("rxx", {0, 1}, {P(0)}), g("s", {0}), g("s", {1})},
                 none,
                 "T02 §4"});
    // cp(λ) = e^{iλ/4} · rzz(−λ/2) · (Rz(λ/2) ⊗ Rz(λ/2))  (T02 §2.2)
    r.push_back(
        {"cp",
         "rxx",
         {g("rz", {0}, {P(0, 0.5)}), g("rz", {1}, {P(0, 0.5)}), g("rzz", {0, 1}, {P(0, -0.5)})},
         P(0, 0.25),
         "T02 §2.2"});
    r.push_back(
        {"cz",
         "rxx",
         {g("rz", {0}, {K(kPi / 2)}), g("rz", {1}, {K(kPi / 2)}), g("rzz", {0, 1}, {K(-kPi / 2)})},
         K(kPi / 4),
         "T02 §2.2"});
    // crz(θ) = exp(−iθ/4 Z_t) · exp(+iθ/4 Z_c Z_t)
    r.push_back({"crz",
                 "rxx",
                 {g("rz", {1}, {P(0, 0.5)}), g("rzz", {0, 1}, {P(0, -0.5)})},
                 none,
                 "T02 (3.3)"});
    return r;
}
} // namespace

const std::vector<Rule>& decompositionRules() {
    static const std::vector<Rule> table = build();
    return table;
}

} // namespace qlab::compiler
