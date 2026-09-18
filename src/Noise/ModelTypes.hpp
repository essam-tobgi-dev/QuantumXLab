#pragma once
// Spec 08 §4 — value types of the noise model: channels attached to an operation, the effective
// calibrated parameters (after overrides, SI units), user overrides (§4.2) and build options (§4.1).
#include "Core/StrongType.hpp"
#include "Noise/Channel.hpp"
#include "Noise/Readout.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::noise {

// One channel bound to its targets and evaluation context (spec 08 §4). `qubits[k]` is target k of
// the channel (targets[0] = least significant index of its Kraus operators).
struct AttachedChannel {
    ChannelPtr channel;
    Placement placement = Placement::After;
    std::vector<QubitIndex> qubits;
    Context context; // duration of the window the channel acts over
};

// Effective per-qubit parameters in SI units. A lifetime of +inf means the process is absent
// (an uncalibrated coupler slot, spec 09 §2).
struct QubitNoise {
    double t1S = 0.0, t2S = 0.0, t2StarS = 0.0;
    double frequencyHz = 0.0;
    double pThermal = 0.0;            // max(pThermalCalibration, pThermalLine), spec 08 §2.2
    double pThermalCalibration = 0.0; // calibration `thermal_population`
    double pThermalLine = 0.0;        // n̄/(1 + 2n̄) of the drive line (spec 11 §6)
    double resetError = 0.0;
    Assignment2 readoutAssignment{{{1.0, 0.0}, {0.0, 1.0}}};
    double readoutDurationS = 0.0;
    double readoutDephasing = 0.0; // `readout_crosstalk_dephasing` scale, 0 = off
    double driftSigmaHz = 0.0;     // σ_f from T2* by (2.5); 0 unless T2* < T2
};

// One native gate on one target tuple with the spec 08 §4.1 decomposition of its error.
struct GateNoise {
    std::string gate;
    std::vector<std::uint32_t> qubits; // calibration order ("0-1" → {0, 1})
    double errorR = 0.0;               // average infidelity r = 1 − F_avg
    double durationS = 0.0;
    double coherentFraction = 0.0;
    double leakage = 0.0, seepage = 0.0; // p_L, p_S per gate (d = 3 only)
    double pTotal = 0.0;                 // d r/(d − 1), T10 (1.5)
    double pRelaxation = 0.0;            // depolarizing equivalent of thermal_relaxation over durationS
    double pCoherent = 0.0;              // depolarizing equivalent of the over-rotation
    double depolarizing = 0.0;           // max(0, pTotal − pRelaxation − pCoherent)
    double overRotationRad = 0.0;
    bool clamped = false; // the subtraction went negative: warning emitted, depolarizing = 0
};

struct EdgeNoise {
    std::uint32_t a = 0, b = 0;
    double zzHz = 0.0; // static ZZ rate ζ in Hz (spec 08 §2.4)
};

// A correlated readout group (spec 08 §3): k ≤ 4 qubits on one feedline, 2^k × 2^k row-stochastic
// matrix; null keeps the tensor product of the single-qubit matrices.
struct ReadoutGroup {
    std::vector<std::uint32_t> qubits; // local index little-endian over this order
    std::optional<num::RealMatrix> assignment;
};

// User overrides and per-class switches (spec 08 §4.2). Scales multiply the calibrated values;
// replacements are absolute, in document units, and win over scales. Replacement fields:
//   qubit: t1_us t2_us t2_star_us p_thermal reset_error readout_e01 readout_e10 readout_duration_ns
//          readout_crosstalk_dephasing gate_error_1q duration_1q_ns coherent_fraction_1q leakage_1q
//   edge ("a-b", either order): zz_hz gate_error_2q duration_ns coherent_fraction_2q leakage_2q
struct Overrides {
    double scaleT1 = 1.0, scaleT2 = 1.0, scaleGateError = 1.0, scaleReadoutError = 1.0;
    std::vector<std::string> disable; // channel class ids (noise::id)
    std::map<std::uint32_t, std::map<std::string, double>> replaceQubit;
    std::map<std::string, std::map<std::string, double>> replaceEdge;

    bool disabled(std::string_view channelId) const;
    void setEnabled(std::string_view channelId, bool on);
    bool operator==(const Overrides&) const = default;
};

// Inputs of `fromCalibration` beyond the calibration file (spec 08 §4.1).
struct NoiseOptions {
    // Noise photon number arriving along each qubit's drive line in the active cryo model
    // (index = qubit, missing entries = 0); p_th = max(calibration, n̄/(1 + 2n̄)).
    std::vector<double> linePhotonNumbers;
    std::uint32_t levels = 2; // 3 enables the leakage channel (spec 08 §2.5)
    Overrides overrides;
};

} // namespace qlab::noise
