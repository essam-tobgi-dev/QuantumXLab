#pragma once
// Spec 21 §4 — text of readouts: 4 significant figures with units, kets in little-endian
// convention (|q_{n-1} … q_0⟩, qubit 0 rightmost; README), complex numbers in both forms.
#include "Numerics/Types.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace qlab::viz::math {

// `value` with `sig` significant figures ("0.7071", "1.234e-05", "0" for |v| < 1e-300).
std::string formatSig(double value, int sig = 4);
// formatSig plus a unit separated by a thin space: "12.5 ns". An empty unit adds nothing.
std::string formatWithUnit(double value, std::string_view unit, int sig = 4);
// Seconds with an auto-selected SI prefix (spec 19 §5.1): "320 ns", "1.5 µs", "0 s".
std::string formatTime(double seconds, int sig = 4);
// Hertz with an auto-selected SI prefix: "4.812 GHz".
std::string formatFrequency(double hertz, int sig = 4);
// Probability or error rate as a plain number, or percent when `percent` is set.
std::string formatProbability(double p, bool percent = false);

// x + iy ("0.5 - 0.5i") and r·e^{iφ} ("0.7071 e^{i(-π/4)}").
std::string formatCartesian(num::Complex a, int sig = 4);
std::string formatPolar(num::Complex a, int sig = 4);

// Binary label of a basis index over `nbits` qubits, most-significant qubit first so that qubit 0
// is the rightmost character: bitString(1, 3) = "001".
std::string bitString(std::uint64_t index, std::size_t nbits);
// "|001⟩" (or "|001>" when `ascii`), or the decimal form "|1⟩" when `decimal`.
std::string ketLabel(std::uint64_t index, std::size_t nbits, bool decimal = false, bool ascii = false);
std::string braLabel(std::uint64_t index, std::size_t nbits, bool decimal = false, bool ascii = false);

} // namespace qlab::viz::math
