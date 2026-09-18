// Spec 08 §2.1 — Pauli channels; T04 (5.1)–(5.3), T10 (1.5).
#include "Noise/Catalogue.hpp"
#include "Numerics/Tensor.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::noise::channels {
namespace {
Status checkProbability(double p, std::string_view name) {
    if (!(p >= 0.0 && p <= 1.0)) // also rejects NaN
        return fail(err::InvalidParameter, std::format("{} = {} is outside [0, 1]", name, p));
    return {};
}
Result<Kraus> singleFlip(double p, std::uint32_t code, std::string_view name) {
    QXL_TRY(checkProbability(p, name));
    std::vector<double> w(4, 0.0);
    w[0] = 1.0 - p;
    w[code] = p;
    return Kraus::fromPauliWeights(1, w);
}
} // namespace

Result<Kraus> bitFlip(double p) { return singleFlip(p, 1, "bit_flip p"); }
Result<Kraus> phaseFlip(double p) { return singleFlip(p, 3, "phase_flip p"); }
Result<Kraus> bitPhaseFlip(double p) { return singleFlip(p, 2, "bit_phase_flip p"); }
Result<Kraus> resetError(double p) { return singleFlip(p, 1, "reset_error"); } // spec 08 §5.5

Result<Kraus> pauli(double px, double py, double pz) {
    QXL_TRY(checkProbability(px, "pauli p_x"));
    QXL_TRY(checkProbability(py, "pauli p_y"));
    QXL_TRY(checkProbability(pz, "pauli p_z"));
    const double rest = 1.0 - px - py - pz;
    if (rest < -1e-15)
        return fail(err::InvalidParameter, std::format("pauli probabilities sum to {} > 1", px + py + pz));
    const std::vector<double> w{std::max(0.0, rest), px, py, pz};
    return Kraus::fromPauliWeights(1, w);
}

Result<Kraus> depolarizingNq(std::uint32_t n, double p) {
    if (n == 0 || n > 4) return fail(err::InvalidParameter, std::format("depolarizing_nq supports 1..4 qubits, {} requested", n));
    QXL_TRY(checkProbability(p, "depolarizing p"));
    const std::size_t count = num::ipow(4, n); // d² Pauli strings
    const double each = p / static_cast<double>(count);
    std::vector<double> w(count, each);
    w[0] = 1.0 - each * static_cast<double>(count - 1); // 1 − p (d²−1)/d²
    return Kraus::fromPauliWeights(n, w);
}
Result<Kraus> depolarizing1q(double p) { return depolarizingNq(1, p); }
Result<Kraus> depolarizing2q(double p) { return depolarizingNq(2, p); }

double depolarizingFromGateError(double r, std::uint32_t n) {
    const double d = std::ldexp(1.0, static_cast<int>(n));
    return std::clamp(d / (d - 1.0) * r, 0.0, 1.0);
}
double gateErrorFromDepolarizing(double p, std::uint32_t n) {
    const double d = std::ldexp(1.0, static_cast<int>(n));
    return p * (d - 1.0) / d;
}

} // namespace qlab::noise::channels
