// Spec 25 §2 — gate table oracles: unitarity, hand-written matrices, T02 identities, inverse and
// power rules, controlled forms. Matrices are little-endian: operand 0 is the least significant.
#include <catch2/catch_test_macros.hpp>
#include "IR/Gates.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Matrix.hpp"
#include "Numerics/Tensor.hpp"
#include <cmath>
#include <numbers>

using namespace qlab;
using namespace qlab::ir;
using num::Complex;
using num::Matrix;

namespace {
constexpr double kPi = std::numbers::pi;
const Complex I_{0, 1};
const double r2 = 1.0 / std::sqrt(2.0);

Matrix gate(std::string_view name, std::vector<double> p = {}) {
    auto m = gates::matrix(name, p);
    INFO("gate " << name);
    REQUIRE(m.has_value());
    return *m;
}
Matrix rows(std::initializer_list<std::initializer_list<Complex>> r) { return Matrix::fromRows(r); }
bool near(const Matrix& a, const Matrix& b, double tol = 1e-12) { return num::approxEqual(a, b, tol); }
Matrix mul(const Matrix& a, const Matrix& b) { return num::matmul(a, b); }
// The inverse the library itself would emit: a named inverse, else the adjoint.
Matrix inverseMatrix(std::string_view name, const std::vector<double>& p) {
    if (auto rw = gates::inverseOf(name, p)) return gate(rw->name, rw->params);
    return num::adjoint(gate(name, p));
}
} // namespace

TEST_CASE("every library gate is unitary to 1e-12 and U * inverse(U) = I") {
    const std::vector<std::vector<double>> paramSets = {{0.7, -1.3, 2.9, 0.4}, {kPi, kPi / 2, -kPi / 4, 1.0},
                                                        {0.0, 0.0, 0.0, 0.0}};
    for (const auto& d : gates::all()) {
        if (d.name == "unitary") continue;
        for (const auto& ps : paramSets) {
            std::vector<double> p(ps.begin(), ps.begin() + d.nParams);
            Matrix m = gate(d.name, p);
            INFO("gate " << d.name << " p0=" << (p.empty() ? 0.0 : p[0]));
            REQUIRE(m.rows == (std::size_t{1} << d.nQubits));
            CHECK(num::isUnitary(m.view(), 1e-12));
            CHECK(near(mul(m, inverseMatrix(d.name, p)), Matrix::identity(m.rows)));
        }
    }
    // Only the exchange-type natives lack a named inverse; they carry the adjoint flag instead.
    for (const auto& d : gates::all()) {
        std::vector<double> p(static_cast<std::size_t>(d.nParams), 0.3);
        const bool named = gates::inverseOf(d.name, p).has_value();
        INFO("gate " << d.name);
        CHECK(named == !(d.name == "iswap" || d.name == "siswap" || d.name == "unitary"));
    }
}

TEST_CASE("single-qubit gates match hand-written matrices") {
    CHECK(near(gate("x"), rows({{0, 1}, {1, 0}})));
    CHECK(near(gate("y"), rows({{0, -I_}, {I_, 0}})));
    CHECK(near(gate("z"), rows({{1, 0}, {0, -1}})));
    CHECK(near(gate("h"), rows({{r2, r2}, {r2, -r2}})));
    CHECK(near(gate("s"), rows({{1, 0}, {0, I_}})));
    CHECK(near(gate("t"), rows({{1, 0}, {0, Complex(r2, r2)}})));
    CHECK(near(gate("sx"), rows({{Complex(0.5, 0.5), Complex(0.5, -0.5)}, {Complex(0.5, -0.5), Complex(0.5, 0.5)}})));
    // Rotations at θ ∈ {0, π/2, π, 2π}; R(2π) = −I (T02 §1.3).
    const Matrix id = Matrix::identity(2), minusId = rows({{-1, 0}, {0, -1}});
    for (const char* r : {"rx", "ry", "rz"}) {
        CHECK(near(gate(r, {0.0}), id));
        CHECK(near(gate(r, {2 * kPi}), minusId));
    }
    CHECK(near(gate("rx", {kPi / 2}), rows({{r2, -I_ * r2}, {-I_ * r2, r2}})));
    CHECK(near(gate("rx", {kPi}), rows({{0, -I_}, {-I_, 0}})));
    CHECK(near(gate("ry", {kPi / 2}), rows({{r2, -r2}, {r2, r2}})));
    CHECK(near(gate("ry", {kPi}), rows({{0, -1}, {1, 0}})));
    CHECK(near(gate("rz", {kPi / 2}), rows({{Complex(r2, -r2), 0}, {0, Complex(r2, r2)}})));
    CHECK(near(gate("rz", {kPi}), rows({{-I_, 0}, {0, I_}})));
    // U(θ,φ,λ) of T02 (1.1) and the stdgates identities built on it.
    const double th = 0.9, ph = -0.4, la = 1.7;
    CHECK(near(gate("U", {th, ph, la}),
               rows({{std::cos(th / 2), -std::polar(1.0, la) * std::sin(th / 2)},
                     {std::polar(1.0, ph) * std::sin(th / 2), std::polar(1.0, ph + la) * std::cos(th / 2)}})));
    CHECK(near(gate("U", {kPi / 2, 0, kPi}), gate("h")));
    CHECK(near(gate("U", {kPi, 0, kPi}), gate("x")));
    CHECK(near(gate("U", {kPi, kPi / 2, kPi / 2}), gate("y")));
    // Legacy u2/u3 keep their OpenQASM 2 meaning, stdgates.inc `gphase(-(φ+λ)/2); U(…)` (spec 13 §4):
    // u3 = Rz(φ) Ry(θ) Rz(λ) = e^{-i(φ+λ)/2} U — the phase is observable under `ctrl @`.
    CHECK(near(gate("u3", {th, ph, la}), mul(gate("rz", {ph}), mul(gate("ry", {th}), gate("rz", {la})))));
    CHECK(near(gate("u2", {ph, la}), mul(gate("rz", {ph}), mul(gate("ry", {kPi / 2}), gate("rz", {la})))));
    CHECK(near(gate("u1", {la}), gate("p", {la})));
}

TEST_CASE("two- and three-qubit gates match hand-written little-endian matrices") {
    // cx q0, q1 (control q0) maps index 1 -> 3 (spec 25 §2, T02 (2.1)).
    const Matrix cx = gate("cx");
    CHECK(near(cx, rows({{1, 0, 0, 0}, {0, 0, 0, 1}, {0, 0, 1, 0}, {0, 1, 0, 0}})));
    CHECK(cx(3, 1) == Complex(1, 0));
    CHECK(near(gate("cz"), rows({{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, -1}})));
    CHECK(near(gate("cy"), rows({{1, 0, 0, 0}, {0, 0, 0, -I_}, {0, 0, 1, 0}, {0, I_, 0, 0}})));
    CHECK(near(gate("swap"), rows({{1, 0, 0, 0}, {0, 0, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}})));
    CHECK(near(gate("iswap"), rows({{1, 0, 0, 0}, {0, 0, I_, 0}, {0, I_, 0, 0}, {0, 0, 0, 1}})));
    CHECK(near(gate("siswap"), rows({{1, 0, 0, 0}, {0, r2, I_ * r2, 0}, {0, I_ * r2, r2, 0}, {0, 0, 0, 1}})));
    CHECK(near(gate("ecr"), rows({{0, r2, 0, I_ * r2}, {r2, 0, -I_ * r2, 0}, {0, I_ * r2, 0, r2}, {-I_ * r2, 0, r2, 0}})));
    const double t = 0.8, c = std::cos(t / 2), s = std::sin(t / 2);
    CHECK(near(gate("rzz", {t}), rows({{std::polar(1.0, -t / 2), 0, 0, 0}, {0, std::polar(1.0, t / 2), 0, 0},
                                       {0, 0, std::polar(1.0, t / 2), 0}, {0, 0, 0, std::polar(1.0, -t / 2)}})));
    const Matrix xx = rows({{c, 0, 0, -I_ * s}, {0, c, -I_ * s, 0}, {0, -I_ * s, c, 0}, {-I_ * s, 0, 0, c}});
    CHECK(near(gate("rxx", {t}), xx));
    CHECK(near(gate("ms", {t}), xx));
    Matrix ccx = Matrix::identity(8);
    ccx(3, 3) = 0; ccx(7, 7) = 0; ccx(3, 7) = 1; ccx(7, 3) = 1;   // controls q0, q1; target q2
    CHECK(near(gate("ccx"), ccx));
    Matrix cswap = Matrix::identity(8);
    cswap(3, 3) = 0; cswap(5, 5) = 0; cswap(3, 5) = 1; cswap(5, 3) = 1; // control q0 swaps q1, q2
    CHECK(near(gate("cswap"), cswap));
}

TEST_CASE("T02 identities hold for the library matrices") {
    const Matrix x = gate("x"), y = gate("y"), z = gate("z"), h = gate("h"), s = gate("s"), sdg = gate("sdg");
    CHECK(near(mul(h, mul(z, h)), x));
    CHECK(near(mul(s, mul(x, sdg)), y));
    CHECK(near(mul(gate("t"), gate("t")), s));
    CHECK(near(mul(gate("sx"), gate("sx")), x));
    // CX = (I ⊗ H) CZ (I ⊗ H) with the target as the more significant operand.
    const Matrix hT = num::kron(h, Matrix::identity(2));
    CHECK(near(mul(hT, mul(gate("cz"), hT)), gate("cx")));
    // ECR = RZX(−π/4) · X_c · RZX(π/4), RZX(θ) = exp(−iθ/2 X_t Z_c), control = operand 0.
    const Matrix xz = num::kron(x, z);
    auto rzx = [&](double a) { return num::add(num::scale(Matrix::identity(4), std::cos(a / 2)), num::scale(xz, -I_ * std::sin(a / 2))); };
    const Matrix xc = num::kron(Matrix::identity(2), x);
    CHECK(near(mul(rzx(-kPi / 4), mul(xc, rzx(kPi / 4))), gate("ecr")));
}

TEST_CASE("named powers, controlled forms, and fractional unitary powers") {
    auto named = [](std::string_view g, double k) { auto r = gates::namedPower(g, {}, k); REQUIRE(r.has_value()); return r->name; };
    CHECK(named("s", 2) == "z");
    CHECK(named("t", 2) == "s");
    CHECK(named("s", -1) == "sdg");
    CHECK(named("t", 8).empty());
    CHECK(named("sx", 2) == "x");
    CHECK(named("sx", -1) == "sxdg");
    CHECK(named("sx", 4).empty());
    CHECK(named("h", 3) == "h");
    CHECK(named("cx", 2).empty());
    CHECK_FALSE(gates::namedPower("iswap", {}, 2).has_value());

    auto split = gates::splitControlled("ccx", {});
    REQUIRE(split.has_value());
    CHECK((split->base.name == "x" && split->controls == 2));
    CHECK(gates::joinControlled("x", {}, 1)->name == "cx");
    CHECK(gates::joinControlled("gphase", std::vector<double>{0.1}, 1) == std::nullopt);
    auto cu = gates::joinControlled("U", std::vector<double>{0.1, 0.2, 0.3}, 1);
    REQUIRE(cu.has_value());
    CHECK((cu->name == "cu" && cu->params.size() == 4 && cu->params[3] == 0.0));
    CHECK_FALSE(gates::splitControlled("cu", std::vector<double>{0.1, 0.2, 0.3, 0.4}).has_value());

    // controlled(): controls sit above the targets; a negative control selects |0⟩.
    const Matrix x = gate("x");
    const std::uint8_t neg[] = {1};
    CHECK(near(gates::controlled(x, 1, {}), rows({{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}, {0, 0, 1, 0}})));
    CHECK(near(gates::controlled(x, 1, neg), rows({{0, 1, 0, 0}, {1, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}})));

    // Principal branch, eigenphases in (−π, π]: S^½ = T, Z^½ = S, X^½ = SX (−1 ↦ +i).
    auto power = [](const Matrix& u, double k) { auto r = gates::unitaryPower(u, k); REQUIRE(r.has_value()); return *r; };
    CHECK(near(power(gate("s"), 0.5), gate("t"), 1e-10));
    CHECK(near(power(gate("z"), 0.5), gate("s"), 1e-10));
    CHECK(near(power(x, 0.5), gate("sx"), 1e-10));
    const Matrix u = gate("U", {0.9, -0.4, 1.7});
    CHECK(near(power(u, 3), mul(u, mul(u, u))));
    CHECK(near(power(u, -2), num::adjoint(mul(u, u))));
    const Matrix third = power(gate("iswap"), 1.0 / 3.0);
    CHECK(near(mul(third, mul(third, third)), gate("iswap"), 1e-10));
}
