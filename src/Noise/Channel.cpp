// Spec 08 §1–§3 — IChannel implementations. One generic class binds an id, a Kraus builder and the
// optional Lindblad generator; the factories below fix the builder per catalogue entry.
#include "Noise/Channel.hpp"
#include "Noise/Catalogue.hpp"
#include <cmath>
#include <format>
#include <functional>
#include <numbers>

namespace qlab::noise {
using num::Matrix;
namespace {
class BuiltChannel final : public IChannel {
public:
    using Builder = std::function<Result<Kraus>(const Context&)>;
    BuiltChannel(std::string_view cid, std::uint32_t arity, std::uint32_t levels, Builder build,
                 std::vector<LindbladTerm> terms, std::string description)
        : id_(cid), arity_(arity), levels_(levels), build_(std::move(build)), terms_(std::move(terms)),
          description_(std::move(description)) {}
    std::string_view id() const override { return id_; }
    std::uint32_t arity() const override { return arity_; }
    std::uint32_t levels() const override { return levels_; }
    Result<Kraus> kraus(const Context& ctx) const override { return build_(ctx); }
    std::vector<LindbladTerm> lindblad() const override { return terms_; }
    std::string describe() const override { return description_; }
private:
    std::string id_;
    std::uint32_t arity_, levels_;
    Builder build_;
    std::vector<LindbladTerm> terms_;
    std::string description_;
};

// Probes the builder once so that invalid parameters fail at construction, not at first use.
Result<ChannelPtr> built(std::string_view cid, std::uint32_t arity, BuiltChannel::Builder build,
                         std::vector<LindbladTerm> terms, std::string description) {
    QXL_TRY(build(Context{}));
    return ChannelPtr(std::make_shared<BuiltChannel>(cid, arity, 2u, std::move(build), std::move(terms), std::move(description)));
}
} // namespace

std::optional<PauliTwirl> IChannel::twirled(const Context& ctx) const {
    auto k = kraus(ctx);
    if (!k) return std::nullopt;
    auto t = pauliTwirl(*k);
    if (!t) return std::nullopt;
    return std::move(*t);
}

Result<ChannelPtr> fixedChannel(std::string_view channelId, Result<Kraus> kraus, std::string description) {
    if (!kraus) return std::unexpected(std::move(kraus.error()));
    const std::uint32_t arity = kraus->arity, levels = kraus->levels;
    auto shared = std::make_shared<const Kraus>(std::move(*kraus));
    return ChannelPtr(std::make_shared<BuiltChannel>(
        channelId, arity, levels, [shared](const Context&) -> Result<Kraus> { return *shared; },
        std::vector<LindbladTerm>{}, std::move(description)));
}

Result<ChannelPtr> bitFlipChannel(double p) { return fixedChannel(id::BitFlip, channels::bitFlip(p), std::format("p={:.6g}", p)); }
Result<ChannelPtr> phaseFlipChannel(double p) { return fixedChannel(id::PhaseFlip, channels::phaseFlip(p), std::format("p={:.6g}", p)); }
Result<ChannelPtr> bitPhaseFlipChannel(double p) { return fixedChannel(id::BitPhaseFlip, channels::bitPhaseFlip(p), std::format("p={:.6g}", p)); }
Result<ChannelPtr> pauliChannel(double px, double py, double pz) {
    return fixedChannel(id::Pauli, channels::pauli(px, py, pz), std::format("px={:.6g} py={:.6g} pz={:.6g}", px, py, pz));
}
Result<ChannelPtr> depolarizingChannel(std::uint32_t n, double p) {
    const std::string_view cid = n == 1 ? id::Depolarizing1q : n == 2 ? id::Depolarizing2q : id::DepolarizingNq;
    return fixedChannel(cid, channels::depolarizingNq(n, p), std::format("n={} p={:.6g}", n, p));
}
Result<ChannelPtr> overRotationChannel(std::string_view axis, double eps) {
    return fixedChannel(id::OverRotation, channels::overRotation(axis, eps), std::format("axis={} epsilon={:.6g}rad", axis, eps));
}
Result<ChannelPtr> leakageChannel(double pLeak, double pSeep) {
    return fixedChannel(id::Leakage, channels::leakage(pLeak, pSeep), std::format("p_L={:.6g} p_S={:.6g}", pLeak, pSeep));
}
Result<ChannelPtr> resetErrorChannel(double p) { return fixedChannel(id::ResetError, channels::resetError(p), std::format("p={:.6g}", p)); }
Result<ChannelPtr> thermalPreparationChannel(double pTh) {
    return fixedChannel(id::ThermalPreparation, channels::bitFlip(pTh), std::format("p_th={:.6g}", pTh));
}

Result<std::vector<LindbladTerm>> thermalRelaxationLindblad(double t1S, double t2S, double pTh, std::uint32_t levels) {
    if (levels < 2) return fail(err::BadDimensions, "a site needs at least 2 levels");
    if (!(pTh >= 0.0 && pTh <= 1.0)) return fail(err::InvalidParameter, std::format("p_th = {} is outside [0, 1]", pTh));
    QXL_TRY_ASSIGN(const double tPhi, channels::pureDephasingTime(t1S, t2S));
    std::vector<LindbladTerm> terms;
    Matrix lower(levels, levels); // a|n⟩ = √n |n−1⟩; σ⁻ = |0⟩⟨1| for d = 2
    for (std::uint32_t n = 1; n < levels; ++n) lower(n - 1, n) = std::sqrt(static_cast<double>(n));
    const double gamma1 = std::isfinite(t1S) ? 1.0 / t1S : 0.0;
    if (gamma1 > 0.0) {
        // γ↓ + γ↑ = 1/T1 and γ↑/(γ↓+γ↑) = p_th (T04 (3.3)): the same fixed point and rate as (2.2).
        terms.push_back({"T1", num::scale(lower, std::sqrt(gamma1 * (1.0 - pTh)))});
        if (pTh > 0.0) terms.push_back({"Tth", num::scale(num::adjoint(lower), std::sqrt(gamma1 * pTh))});
    }
    if (std::isfinite(tPhi)) {
        const double gphi = 1.0 / tPhi;
        Matrix op(levels, levels);
        if (levels == 2) { // √(γφ/2)·Z, T04 (4.1)
            op(0, 0) = std::sqrt(0.5 * gphi);
            op(1, 1) = -std::sqrt(0.5 * gphi);
        } else {           // √(2γφ)·a†a, spec 08 (7.2); identical on the qubit subspace
            for (std::uint32_t n = 0; n < levels; ++n) op(n, n) = std::sqrt(2.0 * gphi) * static_cast<double>(n);
        }
        terms.push_back({"Tphi", std::move(op)});
    }
    return terms;
}

Result<ChannelPtr> thermalRelaxationChannel(double t1S, double t2S, double pTh) {
    QXL_TRY_ASSIGN(auto terms, thermalRelaxationLindblad(t1S, t2S, pTh, 2));
    return built(id::ThermalRelaxation, 1,
                 [=](const Context& c) { return channels::thermalRelaxation(t1S, t2S, c.durationS, pTh); }, std::move(terms),
                 std::format("T1={:.6g}us T2={:.6g}us p_th={:.6g}", t1S * 1e6, t2S * 1e6, pTh));
}
Result<ChannelPtr> amplitudeDampingChannel(double t1S, double pTh) {
    QXL_TRY_ASSIGN(auto terms, thermalRelaxationLindblad(t1S, 2.0 * t1S, pTh, 2)); // T2 = 2T1: no Tφ term
    return built(id::AmplitudeDamping, 1, [=](const Context& c) { return channels::amplitudeDampingOver(t1S, c.durationS, pTh); },
                 std::move(terms), std::format("T1={:.6g}us p_th={:.6g}", t1S * 1e6, pTh));
}
Result<ChannelPtr> phaseDampingChannel(double tPhiS) {
    std::vector<LindbladTerm> terms;
    if (std::isfinite(tPhiS) && tPhiS > 0.0) {
        Matrix z(2, 2);
        z(0, 0) = std::sqrt(0.5 / tPhiS);
        z(1, 1) = -std::sqrt(0.5 / tPhiS);
        terms.push_back({"Tphi", std::move(z)});
    }
    return built(id::PhaseDamping, 1, [=](const Context& c) { return channels::phaseDampingOver(tPhiS, c.durationS); },
                 std::move(terms), std::format("T_phi={:.6g}us", tPhiS * 1e6));
}
Result<ChannelPtr> zzCrosstalkChannel(double zetaHz) {
    return built(id::ZzCrosstalk, 2, [=](const Context& c) { return channels::zzCrosstalk(zetaHz, c.durationS); }, {},
                 std::format("zeta={:.6g}Hz", zetaHz));
}
Result<ChannelPtr> measurementDephasingChannel(double t2S, double scale) {
    return built(id::MeasurementDephasing, 1, [=](const Context& c) { return channels::measurementDephasing(c.durationS, t2S, scale); },
                 {}, std::format("T2={:.6g}us scale={:.6g}", t2S * 1e6, scale));
}
Result<ChannelPtr> detuningPhaseChannel(double fixedHz) {
    return built(id::DetuningPhase, 1, [=](const Context& c) { return channels::detuningPhase(fixedHz + c.detuningHz, c.durationS); },
                 {}, std::format("fixed={:.6g}Hz + per-shot drift", fixedHz));
}
Result<ChannelPtr> gaussianDephasingChannel(double sigmaHz) {
    return built(id::DetuningDrift, 1, [=](const Context& c) { return channels::gaussianDephasing(sigmaHz, c.durationS); }, {},
                 std::format("sigma_f={:.6g}Hz", sigmaHz));
}

Result<ChannelPtr> driftChannel(double sigmaHz) {
    // spec 08 §2.6: the same physical process in two representations; the backend picks one.
    auto build = [=](const Context& c) -> Result<Kraus> {
        if (c.perShotDrift) return channels::detuningPhase(c.detuningHz, c.durationS);
        return channels::gaussianDephasing(sigmaHz, c.durationS);
    };
    return built(id::DetuningDrift, 1, std::move(build), {}, std::format("sigma_f={:.6g}Hz (T2*={:.6g}us)", sigmaHz,
                 sigmaHz > 0.0 ? 1e6 * std::numbers::sqrt2 / (2.0 * std::numbers::pi * sigmaHz) : 0.0));
}

} // namespace qlab::noise
