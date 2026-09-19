// Spec 08 §2.2–2.3, §2.6, §3 — amplitude damping, phase damping and their composition (T04 §3–§4).
#include "Noise/Catalogue.hpp"
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

namespace qlab::noise::channels {
using num::Matrix;
namespace {
Status checkProbability(double p, std::string_view name) {
    if (!(p >= 0.0 && p <= 1.0))
        return fail(err::InvalidParameter, std::format("{} = {} is outside [0, 1]", name, p));
    return {};
}
Status checkDuration(double tS) {
    if (!(tS >= 0.0) || !std::isfinite(tS))
        return fail(err::InvalidParameter,
                    std::format("duration {} s must be finite and >= 0", tS));
    return {};
}
Status checkLifetime(double tS, std::string_view name) { // +inf = process absent
    if (!(tS > 0.0))
        return fail(err::InvalidParameter, std::format("{} = {} s must be positive", name, tS));
    return {};
}
// (2.2) with s = √(1−γ) supplied separately so the duration form keeps e^{−t/2T1} to full
// precision.
Result<Kraus> gad(double gamma, double s, double pTh) {
    const double a = std::sqrt(1.0 - pTh), b = std::sqrt(pTh), g = std::sqrt(gamma);
    Matrix k0(2, 2), k1(2, 2), k2(2, 2), k3(2, 2);
    k0(0, 0) = a;
    k0(1, 1) = a * s;
    k1(0, 1) = a * g; // decay |1⟩ → |0⟩
    k2(0, 0) = b * s;
    k2(1, 1) = b;
    k3(1, 0) = b * g; // thermal excitation |0⟩ → |1⟩
    std::vector<Matrix> ops{std::move(k0), std::move(k1), std::move(k2), std::move(k3)};
    return Kraus::make(std::move(ops), 1);
}
// (2.3) with c = √(1−λ), the factor multiplying the coherences.
Result<Kraus> pd(double lambda, double c) {
    Matrix k0(2, 2), k1(2, 2);
    k0(0, 0) = 1.0;
    k0(1, 1) = c;
    k1(1, 1) = std::sqrt(lambda);
    std::vector<Matrix> ops{std::move(k0), std::move(k1)};
    return Kraus::make(std::move(ops), 1);
}
} // namespace

Result<Kraus> amplitudeDamping(double gamma) {
    return generalizedAmplitudeDamping(gamma, 0.0);
}

Result<Kraus> generalizedAmplitudeDamping(double gamma, double pThermal) {
    QXL_TRY(checkProbability(gamma, "amplitude_damping gamma"));
    QXL_TRY(checkProbability(pThermal, "amplitude_damping p_th"));
    return gad(gamma, std::sqrt(1.0 - gamma), pThermal);
}

Result<Kraus> amplitudeDampingOver(double t1S, double tS, double pThermal) {
    QXL_TRY(checkLifetime(t1S, "T1"));
    QXL_TRY(checkDuration(tS));
    QXL_TRY(checkProbability(pThermal, "amplitude_damping p_th"));
    const double x = tS / t1S; // γ = 1 − e^{−t/T1}
    return gad(-std::expm1(-x), std::exp(-0.5 * x), pThermal);
}

double thermalPopulationFromPhotons(double n) {
    return n > 0.0 ? n / (1.0 + 2.0 * n) : 0.0;
}
double photonsFromThermalPopulation(double p) {
    if (p <= 0.0)
        return 0.0;
    if (p >= 0.5)
        return std::numeric_limits<double>::infinity();
    return p / (1.0 - 2.0 * p);
}

Result<Kraus> phaseDamping(double lambda) {
    QXL_TRY(checkProbability(lambda, "phase_damping lambda"));
    return pd(lambda, std::sqrt(1.0 - lambda));
}

Result<Kraus> phaseDampingOver(double tPhiS, double tS) {
    QXL_TRY(checkLifetime(tPhiS, "T_phi"));
    QXL_TRY(checkDuration(tS));
    const double x = tS / tPhiS; // T04 (4.5): λ = 1 − e^{−2t/Tφ}
    return pd(-std::expm1(-2.0 * x), std::exp(-x));
}

Result<double> pureDephasingTime(double t1S, double t2S) {
    QXL_TRY(checkLifetime(t1S, "T1"));
    QXL_TRY(checkLifetime(t2S, "T2"));
    if (t2S > 2.0 * t1S * (1.0 + 1e-9))
        return fail(err::Unphysical,
                    std::format("T2 = {} s exceeds 2 T1 = {} s (T04 (4.4))", t2S, 2.0 * t1S));
    const double rate = 1.0 / t2S - 0.5 / t1S; // (2.4)
    if (rate <= 1e-12 / t2S)
        return std::numeric_limits<double>::infinity();
    return 1.0 / rate;
}

Result<Kraus> thermalRelaxation(double t1S, double t2S, double tS, double pThermal) {
    QXL_TRY_ASSIGN(const double tPhi, pureDephasingTime(t1S, t2S));
    QXL_TRY_ASSIGN(Kraus damping, amplitudeDampingOver(t1S, tS, pThermal));
    QXL_TRY_ASSIGN(Kraus dephasing, phaseDampingOver(tPhi, tS));
    return compose(damping, dephasing); // (2.2) then (2.3), spec 08 §2.3
}

double thermalRelaxationInfidelity(double t1S, double t2S, double tS) {
    // T04 (4.6): 1 − F = [2(1 − e^{−t/T2}) + (1 − e^{−t/T1})]/6, written with expm1 for t ≪ T1.
    return (-2.0 * std::expm1(-tS / t2S) - std::expm1(-tS / t1S)) / 6.0;
}

Result<Kraus> measurementDephasing(double readoutDurationS, double t2S, double scale) {
    QXL_TRY(checkDuration(readoutDurationS));
    QXL_TRY(checkLifetime(t2S, "T2"));
    QXL_TRY(checkProbability(scale, "readout_crosstalk_dephasing"));
    // spec 08 §3 with the (2.3) convention: at scale 1 the neighbour's coherence falls by
    // e^{−t_ro/T2}.
    return phaseDamping(scale * -std::expm1(-2.0 * readoutDurationS / t2S));
}

Result<Kraus> gaussianDephasing(double sigmaHz, double tS) {
    if (!(sigmaHz >= 0.0) || !std::isfinite(sigmaHz))
        return fail(err::InvalidParameter,
                    std::format("detuning sigma {} Hz must be finite and >= 0", sigmaHz));
    QXL_TRY(checkDuration(tS));
    // ⟨e^{i 2π δf t}⟩ = e^{−2π²σ²t²} for δf ~ N(0, σ) (T04 (8.2)); λ = 1 − (that factor)².
    const double x = 2.0 * std::numbers::pi * std::numbers::pi * sigmaHz * sigmaHz * tS * tS;
    return pd(-std::expm1(-2.0 * x), std::exp(-x));
}

} // namespace qlab::noise::channels
