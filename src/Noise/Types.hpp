#pragma once
// Spec 08 §1, §4 — shared vocabulary of qlab::noise: error codes, placement, evaluation context,
// Pauli-twirl result and the local Lindblad term a channel can expose (spec 08 §7.3–7.4).
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include "Numerics/Types.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qlab::noise {

// Error codes owned by this module (ErrorCode::Noise_ block, spec 04 §2).
namespace err {
inline constexpr ErrorCode InvalidParameter = ErrorCode::Noise_ + 1;   // probability/time out of range
inline constexpr ErrorCode NotTracePreserving = ErrorCode::Noise_ + 2; // Σ K†K ≠ I (spec 08 §1)
inline constexpr ErrorCode BadDimensions = ErrorCode::Noise_ + 3;
inline constexpr ErrorCode Unphysical = ErrorCode::Noise_ + 4;         // T2 > 2 T1 (T04 (4.4))
inline constexpr ErrorCode NotPauli = ErrorCode::Noise_ + 5;           // stabilizer admissibility (§7.3)
inline constexpr ErrorCode UnsupportedBackend = ErrorCode::Noise_ + 6;
inline constexpr ErrorCode BadJson = ErrorCode::Noise_ + 7;            // message names the field path
inline constexpr ErrorCode UnknownTarget = ErrorCode::Noise_ + 8;      // qubit/edge/gate not in the model
inline constexpr ErrorCode BadReadout = ErrorCode::Noise_ + 9;         // assignment matrix not row-stochastic
inline constexpr ErrorCode LevelsMismatch = ErrorCode::Noise_ + 10;    // d = 3 channel on a d = 2 site
} // namespace err

// Where an attached channel acts relative to its IR operation (spec 08 §4). `During` channels act
// over the whole operation window; gate-level backends apply them after the ideal operation
// (first-order splitting), which is exact for channels that commute with the gate.
enum class Placement : std::uint8_t { Before, After, During };
std::string_view placementName(Placement p);

// What a channel is evaluated at (spec 08 §1 "Context: duration, qubit params"). Device parameters
// (T1, T2, ζ, …) live in the channel object; the context carries what changes per attachment.
struct Context {
    double durationS = 0.0;  // gate, idle or readout window
    double detuningHz = 0.0; // per-shot quasi-static detuning of the target qubit (spec 08 §2.6)
    // Set by the unravelling backends (StateVector, Trajectories, Stabilizer), which draw one
    // `detuningHz` per shot: `detuning_drift` then acts as the coherent R_Z of that shot, so an echo
    // refocuses it. Clear for DensityMatrix/Lindblad, where the shot average is taken exactly.
    bool perShotDrift = false;
};

// Probability vector over Pauli strings on `arity` qubits (spec 08 §7.3).
// Index = Σ_k code_k · 4^k with code 0,1,2,3 = I,X,Y,Z acting on targets[k] (little-endian).
struct PauliTwirl {
    std::uint32_t arity = 1;
    std::vector<double> probs;
    // True when the channel already is a Pauli channel (χ matrix diagonal), so the twirl changes
    // nothing. False marks the twirl as a Model-class approximation (spec 08 §7.3).
    bool exact = true;

    double pI() const { return probs.empty() ? 0.0 : probs[0]; }
    double pX() const { return probs.size() > 1 ? probs[1] : 0.0; } // single-qubit accessors
    double pY() const { return probs.size() > 2 ? probs[2] : 0.0; }
    double pZ() const { return probs.size() > 3 ? probs[3] : 0.0; }
};

// Collapse operator on the channel's own d^arity space with √rate folded in, units √(1/s).
struct LindbladTerm {
    std::string name; // "T1", "Tth", "Tphi"
    num::Matrix op;
};

} // namespace qlab::noise
