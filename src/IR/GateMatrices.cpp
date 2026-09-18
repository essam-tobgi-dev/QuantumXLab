// Spec 14 §3 — gate matrices in the T02 little-endian convention (operand 0 = least significant).
#include "IR/Gates.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Eigen.hpp"
#include "Numerics/Matrix.hpp"
#include "Numerics/Solve.hpp"
#include <cmath>
#include <format>

namespace qlab::ir::gates {
namespace {
using num::Complex;
using num::Matrix;
constexpr double kPi = 3.14159265358979323846;
const Complex I_{0, 1};

Matrix m2(Complex a, Complex b, Complex c, Complex d) {
    Matrix m(2, 2);
    m(0, 0) = a; m(0, 1) = b; m(1, 0) = c; m(1, 1) = d;
    return m;
}
// Two-qubit matrix from 16 row-major entries, basis |q1 q0⟩ (operand 0 least significant).
Matrix m4(std::initializer_list<Complex> e) {
    Matrix m(4, 4);
    std::size_t i = 0;
    for (Complex c : e) { m.data[i] = c; ++i; }
    return m;
}
// Apply `u` to the target bit `t` of a 2-qubit space, conditioned on the control bit `c` == 1.
Matrix ctrl1(num::ConstMatrixView u, int controlBit, int targetBit) {
    Matrix m = Matrix::identity(4);
    const int act[2] = {(1 << controlBit), (1 << controlBit) | (1 << targetBit)};
    for (int r = 0; r < 2; ++r)
        for (int s = 0; s < 2; ++s) m(static_cast<std::size_t>(act[r]), static_cast<std::size_t>(act[s])) = u(static_cast<std::size_t>(r), static_cast<std::size_t>(s));
    return m;
}
Matrix rxM(double t) { return m2(std::cos(t / 2), -I_ * std::sin(t / 2), -I_ * std::sin(t / 2), std::cos(t / 2)); }
Matrix ryM(double t) { return m2(std::cos(t / 2), -std::sin(t / 2), std::sin(t / 2), std::cos(t / 2)); }
Matrix rzM(double t) { return m2(std::polar(1.0, -t / 2), 0, 0, std::polar(1.0, t / 2)); }
Matrix phaseM(double l) { return m2(1, 0, 0, std::polar(1.0, l)); }

Result<double> p1(std::span<const double> p, std::string_view name) {
    if (p.size() != 1) return fail(err::BadArity, std::format("gate '{}' takes 1 parameter, got {}", name, p.size()));
    return p[0];
}
} // namespace

Result<Matrix> matrix(std::string_view name, std::span<const double> params) {
    const GateDef* d = find(name);
    if (!d) return fail(err::UnknownGate, std::format("unknown gate '{}'", name));
    if (name != "unitary" && static_cast<int>(params.size()) != d->nParams)
        return fail(err::BadArity, std::format("gate '{}' takes {} parameter(s), got {}", name, d->nParams, params.size()));
    const double s2 = 1.0 / std::sqrt(2.0);

    if (name == "id") return Matrix::identity(2);
    if (name == "x") return m2(0, 1, 1, 0);
    if (name == "y") return m2(0, -I_, I_, 0);
    if (name == "z") return m2(1, 0, 0, -1);
    if (name == "h") return m2(s2, s2, s2, -s2);
    if (name == "s") return m2(1, 0, 0, I_);
    if (name == "sdg") return m2(1, 0, 0, -I_);
    if (name == "t") return m2(1, 0, 0, std::polar(1.0, kPi / 4));
    if (name == "tdg") return m2(1, 0, 0, std::polar(1.0, -kPi / 4));
    if (name == "sx") return num::scale(m2(Complex(1, 1), Complex(1, -1), Complex(1, -1), Complex(1, 1)), 0.5);
    if (name == "sxdg") return num::scale(m2(Complex(1, -1), Complex(1, 1), Complex(1, 1), Complex(1, -1)), 0.5);
    if (name == "p" || name == "phase" || name == "u1") { QXL_TRY_ASSIGN(double l, p1(params, name)); return phaseM(l); }
    if (name == "rx") { QXL_TRY_ASSIGN(double t, p1(params, name)); return rxM(t); }
    if (name == "ry") { QXL_TRY_ASSIGN(double t, p1(params, name)); return ryM(t); }
    if (name == "rz") { QXL_TRY_ASSIGN(double t, p1(params, name)); return rzM(t); }
    // Legacy u2/u3 are OpenQASM 2's Rz(φ)Ry(θ)Rz(λ): stdgates.inc defines them as
    // gphase(-(φ+λ)/2); U(…) (spec 13 §4), a phase that matters under ctrl @. U has none.
    if (name == "u2") return num::scale(u3(kPi / 2, params[0], params[1]), std::polar(1.0, -(params[0] + params[1]) / 2));
    if (name == "u3") return num::scale(u3(params[0], params[1], params[2]), std::polar(1.0, -(params[1] + params[2]) / 2));
    if (name == "U") return u3(params[0], params[1], params[2]);
    if (name == "gphase") { QXL_TRY_ASSIGN(double g, p1(params, name)); Matrix m(1, 1); m(0, 0) = std::polar(1.0, g); return m; }

    // ---- two-qubit, operands (a, b): index = b·2 + a
    if (name == "cx" || name == "CX") return m4({1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0});
    if (name == "cy") return m4({1, 0, 0, 0, 0, 0, 0, -I_, 0, 0, 1, 0, 0, I_, 0, 0});
    if (name == "cz") return m4({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1});
    if (name == "ch") return ctrl1(m2(s2, s2, s2, -s2).view(), 0, 1);
    if (name == "swap") return m4({1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1});
    if (name == "cp") { QXL_TRY_ASSIGN(double l, p1(params, name)); return m4({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, std::polar(1.0, l)}); }
    if (name == "crx") { QXL_TRY_ASSIGN(double t, p1(params, name)); return ctrl1(rxM(t).view(), 0, 1); }
    if (name == "cry") { QXL_TRY_ASSIGN(double t, p1(params, name)); return ctrl1(ryM(t).view(), 0, 1); }
    if (name == "crz") { QXL_TRY_ASSIGN(double t, p1(params, name)); return ctrl1(rzM(t).view(), 0, 1); }
    if (name == "cu") {
        Matrix u = num::scale(u3(params[0], params[1], params[2]), std::polar(1.0, params[3]));
        return ctrl1(u.view(), 0, 1);
    }
    if (name == "rxx" || name == "ms") {
        QXL_TRY_ASSIGN(double t, p1(params, name));
        const Complex c = std::cos(t / 2), s = -I_ * std::sin(t / 2);
        return m4({c, 0, 0, s, 0, c, s, 0, 0, s, c, 0, s, 0, 0, c});
    }
    if (name == "ryy") {
        QXL_TRY_ASSIGN(double t, p1(params, name));
        const Complex c = std::cos(t / 2), s = I_ * std::sin(t / 2);
        return m4({c, 0, 0, s, 0, c, -s, 0, 0, -s, c, 0, s, 0, 0, c});
    }
    if (name == "rzz") {
        QXL_TRY_ASSIGN(double t, p1(params, name));
        const Complex a = std::polar(1.0, -t / 2), b = std::polar(1.0, t / 2);
        return m4({a, 0, 0, 0, 0, b, 0, 0, 0, 0, b, 0, 0, 0, 0, a});
    }
    if (name == "ecr") // (1/√2)(IX − XY), the standard echoed-cross-resonance unitary (T05 §8)
        return num::scale(m4({0, 1, 0, I_, 1, 0, -I_, 0, 0, I_, 0, 1, -I_, 0, 1, 0}), s2);
    if (name == "iswap") return m4({1, 0, 0, 0, 0, 0, I_, 0, 0, I_, 0, 0, 0, 0, 0, 1});
    if (name == "siswap")
        return m4({1, 0, 0, 0, 0, s2, I_ * s2, 0, 0, I_ * s2, s2, 0, 0, 0, 0, 1});
    if (name == "fsim") {
        const Complex c = std::cos(params[0]), s = -I_ * std::sin(params[0]);
        return m4({1, 0, 0, 0, 0, c, s, 0, 0, s, c, 0, 0, 0, 0, std::polar(1.0, -params[1])});
    }

    // ---- three-qubit, operands (a, b, c): index = c·4 + b·2 + a
    if (name == "ccx") { Matrix m = Matrix::identity(8); m(3, 3) = 0; m(7, 7) = 0; m(7, 3) = 1; m(3, 7) = 1; return m; }
    if (name == "cswap") { Matrix m = Matrix::identity(8); m(3, 3) = 0; m(5, 5) = 0; m(5, 3) = 1; m(3, 5) = 1; return m; }
    if (name == "unitary") return fail(err::Unsupported, "'unitary' carries its matrix on the node");
    return fail(err::UnknownGate, std::format("no matrix for gate '{}'", name));
}

Matrix controlled(num::ConstMatrixView u, std::size_t nCtrl, std::span<const std::uint8_t> negative) {
    const std::size_t ut = u.rows;
    const std::size_t dim = ut << nCtrl;
    Matrix m = Matrix::identity(dim);
    std::size_t pattern = 0;
    for (std::size_t j = 0; j < nCtrl; ++j)
        if (j >= negative.size() || negative[j] == 0) pattern |= (std::size_t{1} << j);
    const std::size_t base = pattern * ut; // control bits sit above the target bits
    for (std::size_t r = 0; r < ut; ++r)
        for (std::size_t s = 0; s < ut; ++s) m(base + r, base + s) = u(r, s);
    return m;
}

Result<Matrix> unitaryPower(num::ConstMatrixView u, double k) {
    if (!u.square()) return fail(err::NotUnitary, "matrix power needs a square matrix");
    if (!num::isUnitary(u, 1e-9)) return fail(err::NotUnitary, "pow() applies only to unitary gates");
    const std::size_t n = u.rows;
    const double ki = std::nearbyint(k);
    if (std::abs(k - ki) < 1e-12 && std::abs(ki) <= 16) { // exact path for integer exponents
        Matrix acc = Matrix::identity(n);
        Matrix step = std::abs(ki) == 0 ? Matrix::identity(n) : Matrix(u);
        if (ki < 0) step = num::adjoint(u);
        for (int i = 0; i < static_cast<int>(std::abs(ki)); ++i) acc = num::matmul(step, acc);
        return acc;
    }
    // Shift the spectrum off −1 so that I + U' is invertible, then Cayley-transform U' = e^{iφ}U to
    // the Hermitian W = i(I − U')(I + U')^{-1}. An eigenvalue e^{iα'} of U' becomes w = tan(α'/2),
    // and W shares U's eigenvectors (spec 14 §4.3, Gray-code roots V = U^{1/2^{n-1}}).
    Matrix shifted;
    double phi = 0;
    bool ok = false;
    for (double trial : {0.0, 0.37, 0.91, 1.53, 2.21, 2.9}) {
        Matrix cand = num::scale(u, std::polar(1.0, trial));
        Matrix sum = num::add(Matrix::identity(n).view(), cand.view());
        if (std::abs(num::determinant(sum.view())) > 1e-3) { shifted = std::move(cand); phi = trial; ok = true; break; }
    }
    if (!ok) return fail(err::Unsupported, "cannot take a fractional power of this unitary");
    Matrix ip = num::add(Matrix::identity(n).view(), shifted.view());
    Matrix im = num::sub(Matrix::identity(n).view(), shifted.view());
    QXL_TRY_ASSIGN(Matrix ipInv, num::inverse(ip.view()));
    Matrix w = num::scale(num::matmul(im, ipInv), I_);
    QXL_TRY_ASSIGN(auto eig, num::eigh(w.view()));
    // Eigenphases of U itself, unwrapped into the principal interval (−π, π] so that the branch
    // does not depend on which shift was needed (−1 ↦ +π, hence X^½ = SX).
    Matrix diag(n, n);
    for (std::size_t j = 0; j < n; ++j) {
        double alpha = 2.0 * std::atan(eig.values[j]) - phi;
        while (alpha <= -kPi + 1e-12) alpha += 2 * kPi;
        while (alpha > kPi + 1e-12) alpha -= 2 * kPi;
        diag(j, j) = std::polar(1.0, k * alpha);
    }
    Matrix vd = num::matmul(eig.vectors, diag);
    return num::matmul(vd, num::adjoint(eig.vectors.view()));
}

} // namespace qlab::ir::gates
