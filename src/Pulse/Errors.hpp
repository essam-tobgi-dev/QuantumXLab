#pragma once
// Spec 10 §10 — diagnostic identifiers raised by the schedule verifier and the pulse loader.
#include "Core/Error.hpp"
#include <string>
#include <string_view>

namespace qlab::pulse {

// Offsets inside the `ErrorCode::Pulse_` block (spec 04 §2).
inline constexpr ErrorCode kErrAlign = ErrorCode::Pulse_ + 1;     // E_PULSE_ALIGN
inline constexpr ErrorCode kErrMin = ErrorCode::Pulse_ + 2;       // E_PULSE_MIN
inline constexpr ErrorCode kErrOverlap = ErrorCode::Pulse_ + 3;   // E_PULSE_OVERLAP
inline constexpr ErrorCode kErrNoChannel = ErrorCode::Pulse_ + 4; // E_NO_CHANNEL
inline constexpr ErrorCode kErrFluxBw = ErrorCode::Pulse_ + 5;    // E_FLUX_BW
inline constexpr ErrorCode kErrWaveform = ErrorCode::Pulse_ + 6;  // invalid waveform parameters
inline constexpr ErrorCode kErrLibrary = ErrorCode::Pulse_ + 7;   // pulses.json problem
inline constexpr ErrorCode kErrNoDefcal =
    ErrorCode::Pulse_ + 8; // E_NO_DEFCAL: no calibration for (gate, qubits)
inline constexpr ErrorCode kErrSampling = ErrorCode::Pulse_ + 9; // sample rate / record problem
inline constexpr ErrorCode kErrDrive =
    ErrorCode::Pulse_ + 10; // schedule → Hamiltonian drive conversion
inline constexpr ErrorCode kErrMsClosure =
    ErrorCode::Pulse_ + 11; // MS phase-space loop cannot be closed

// A non-fatal finding from `Schedule::verify` (spec 10 §3: W_DEAD_PHASE).
struct Warning {
    std::string id;
    std::string message;
};

} // namespace qlab::pulse
