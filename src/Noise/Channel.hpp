#pragma once
// Spec 08 §1 — the channel interface. A channel object owns its device parameters (T1, ζ, p, …);
// the Context supplies what varies per attachment (duration, per-shot detuning). Factories below
// cover the whole catalogue of spec 08 §2–§3.
#include "Noise/Kraus.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::noise {

class IChannel {
  public:
    virtual ~IChannel() = default;
    virtual std::string_view id() const = 0;  // catalogue id, e.g. "thermal_relaxation"
    virtual std::uint32_t arity() const = 0;  // number of targets m
    virtual std::uint32_t levels() const = 0; // site dimension d (3 only for `leakage`)
    virtual Result<Kraus> kraus(const Context& ctx) const = 0;
    // Pauli-twirled probabilities for the stabilizer backend (spec 08 §7.3). `exact` tells whether
    // the twirl changed the channel. nullopt: no twirl exists (d = 3) or the parameters are
    // invalid.
    virtual std::optional<PauliTwirl> twirled(const Context& ctx) const;
    // Collapse operators generating the same dynamics on a d = 2 site, where such a generator
    // exists (spec 08 §7.4); empty for gate-error, coherent and classical channels.
    virtual std::vector<LindbladTerm> lindblad() const = 0;
    // Parameters for the inspector and logs, e.g. "T1=90.99us T2=80.47us p_th=0.0073".
    virtual std::string describe() const = 0;
};
using ChannelPtr = std::shared_ptr<const IChannel>;

// Channel ids (spec 08 §2–§3). `disable` lists in Overrides use the class ids further below.
namespace id {
inline constexpr std::string_view BitFlip = "bit_flip", PhaseFlip = "phase_flip",
                                  BitPhaseFlip = "bit_phase_flip", Pauli = "pauli",
                                  Depolarizing1q = "depolarizing_1q",
                                  Depolarizing2q = "depolarizing_2q",
                                  DepolarizingNq = "depolarizing_nq",
                                  AmplitudeDamping = "amplitude_damping",
                                  PhaseDamping = "phase_damping",
                                  ThermalRelaxation = "thermal_relaxation",
                                  OverRotation = "over_rotation", DetuningPhase = "detuning_phase",
                                  ZzCrosstalk = "zz_crosstalk", Leakage = "leakage",
                                  ResetError = "reset_error",
                                  MeasurementDephasing = "measurement_dephasing",
                                  DetuningDrift = "detuning_drift",
                                  ThermalPreparation = "thermal_preparation", Readout = "readout";
} // namespace id

// ---- context-free channels: the Kraus set is fixed at construction.
Result<ChannelPtr> fixedChannel(std::string_view channelId, Result<Kraus> kraus,
                                std::string description = {});
Result<ChannelPtr> bitFlipChannel(double p);
Result<ChannelPtr> phaseFlipChannel(double p);
Result<ChannelPtr> bitPhaseFlipChannel(double p);
Result<ChannelPtr> pauliChannel(double px, double py, double pz);
Result<ChannelPtr> depolarizingChannel(std::uint32_t nQubits,
                                       double p); // id by arity: _1q, _2q, _nq
Result<ChannelPtr> overRotationChannel(std::string_view axis, double epsilonRad);
Result<ChannelPtr> leakageChannel(double pLeak, double pSeep);
Result<ChannelPtr> resetErrorChannel(double p);
Result<ChannelPtr>
thermalPreparationChannel(double pThermal); // SPAM: bit flip at shot start (T04 §10.3)

// ---- duration-dependent channels: evaluated at Context::durationS.
Result<ChannelPtr> thermalRelaxationChannel(double t1S, double t2S,
                                            double pThermal); // rejects T2 > 2T1
Result<ChannelPtr> amplitudeDampingChannel(double t1S, double pThermal = 0.0);
Result<ChannelPtr> phaseDampingChannel(double tPhiS);
Result<ChannelPtr> zzCrosstalkChannel(double zetaHz);
Result<ChannelPtr> measurementDephasingChannel(double t2S, double scale);
// R_Z(2π (fixedHz + Context::detuningHz) · duration): the per-shot drift sample comes through the
// context.
Result<ChannelPtr> detuningPhaseChannel(double fixedDetuningHz = 0.0);
// Shot-averaged drift over one uninterrupted window (spec 08 §2.6, DensityMatrix form).
Result<ChannelPtr> gaussianDephasingChannel(double sigmaHz);
// id `detuning_drift`: the quasi-static drift in whichever form the backend needs (spec 08 §2.6).
// Context::perShotDrift selects the coherent per-shot R_Z(2π δf t) (unravelling backends, so that
// an echo refocuses it) over the exact Gaussian-averaged phase damping (DensityMatrix/Lindblad).
Result<ChannelPtr> driftChannel(double sigmaHz);

// Collapse operators of thermal relaxation on one d-level site (spec 08 (7.2), T04 (3.3), (4.1)):
//   L↓ = √((1−p_th)/T1)·a, L↑ = √(p_th/T1)·a†, Lφ = √(γφ/2)·Z (d = 2) or √(2γφ)·a†a (d > 2),
// normalised so that the population relaxes with exactly the calibrated T1 and ρ01 with T2.
Result<std::vector<LindbladTerm>> thermalRelaxationLindblad(double t1S, double t2S, double pThermal,
                                                            std::uint32_t levels = 2);

} // namespace qlab::noise
