// Spec 08 §2.4–2.5 — coherent errors (one unitary Kraus operator) and qutrit leakage; T04 §9.
#include "Noise/Catalogue.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::noise::channels {
using num::Complex;
using num::Matrix;

Result<Kraus> overRotation(std::string_view axis, double epsilonRad) {
    if (!std::isfinite(epsilonRad)) return fail(err::InvalidParameter, "over_rotation angle must be finite");
    QXL_TRY_ASSIGN(const std::uint32_t index, pauliStringIndex(axis));
    if (index == 0) return fail(err::InvalidParameter, "over_rotation axis must contain a non-identity Pauli");
    const auto arity = static_cast<std::uint32_t>(axis.size());
    const Matrix P = pauliStringMatrix(index, arity);
    // exp(−i ε P/2) = cos(ε/2) I − i sin(ε/2) P for P² = I.
    Matrix u = num::scale(Matrix::identity(P.rows), Complex(std::cos(0.5 * epsilonRad), 0.0));
    u += num::scale(P, Complex(0.0, -std::sin(0.5 * epsilonRad)));
    return Kraus::fromUnitary(std::move(u), arity);
}

Result<Kraus> detuningPhase(double detuningHz, double tS) {
    if (!std::isfinite(detuningHz) || !std::isfinite(tS) || tS < 0.0)
        return fail(err::InvalidParameter, "detuning_phase needs a finite detuning and a duration >= 0");
    const double theta = 2.0 * std::numbers::pi * detuningHz * tS;
    Matrix u(2, 2);
    u(0, 0) = std::polar(1.0, -0.5 * theta);
    u(1, 1) = std::polar(1.0, 0.5 * theta);
    return Kraus::fromUnitary(std::move(u), 1);
}

Result<Kraus> zzCrosstalk(double zetaHz, double tS) {
    if (!std::isfinite(zetaHz) || !std::isfinite(tS) || tS < 0.0)
        return fail(err::InvalidParameter, "zz_crosstalk needs a finite zeta and a duration >= 0");
    const double quarter = 0.25 * 2.0 * std::numbers::pi * zetaHz * tS; // θ/4 with θ = 2πζt
    Matrix u(4, 4);
    for (std::size_t i = 0; i < 4; ++i) {
        const double zz = ((i & 1u) ^ ((i >> 1) & 1u)) ? -1.0 : 1.0; // eigenvalue of Z⊗Z on |q1 q0⟩
        u(i, i) = std::polar(1.0, -quarter * zz);
    }
    return Kraus::fromUnitary(std::move(u), 2);
}

double overRotationAngleForInfidelity(double rCoherent, std::uint32_t nQubits) {
    if (!(rCoherent > 0.0)) return 0.0;
    const double d = std::ldexp(1.0, static_cast<int>(nQubits));
    const double s2 = std::min(1.0, rCoherent * (d + 1.0) / d); // sin²(ε/2)
    return 2.0 * std::asin(std::sqrt(s2));
}

Result<Kraus> leakage(double pLeak, double pSeep) {
    if (!(pLeak >= 0.0 && pLeak <= 1.0) || !(pSeep >= 0.0 && pSeep <= 1.0))
        return fail(err::InvalidParameter, std::format("leakage probabilities ({}, {}) must lie in [0, 1]", pLeak, pSeep));
    Matrix k0(3, 3), k1(3, 3), k2(3, 3);
    k0(0, 0) = 1.0;
    k0(1, 1) = std::sqrt(1.0 - pLeak);
    k0(2, 2) = std::sqrt(1.0 - pSeep);
    k1(2, 1) = std::sqrt(pLeak); // |2⟩⟨1|
    k2(1, 2) = std::sqrt(pSeep); // |1⟩⟨2|
    std::vector<Matrix> ops{std::move(k0), std::move(k1), std::move(k2)};
    return Kraus::make(std::move(ops), 1, 3);
}

} // namespace qlab::noise::channels
