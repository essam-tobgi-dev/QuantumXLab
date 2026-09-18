#pragma once
// Spec 16 §1–§6 — shared vocabulary of qlab::qec: error codes, code families, check types, memory
// basis, noise settings and the Wilson interval used for every Statistical figure of this module.
#include "Core/Error.hpp"
#include "Core/Fidelity.hpp"
#include <cstdint>
#include <optional>
#include <string_view>

namespace qlab::qec {

using qlab::FidelityClass; // spec 00 §5 (Core vocabulary)

// Error codes owned by this module (ErrorCode::Qec_ block, spec 04 §2).
namespace err {
inline constexpr ErrorCode BadPauli = ErrorCode::Qec_ + 1;          // letter, length or phase
inline constexpr ErrorCode BadJson = ErrorCode::Qec_ + 2;           // message names the field path
inline constexpr ErrorCode NotCommuting = ErrorCode::Qec_ + 3;      // two generators anticommute
inline constexpr ErrorCode NotIndependent = ErrorCode::Qec_ + 4;    // rank over F2 below n − k
inline constexpr ErrorCode BadLogical = ErrorCode::Qec_ + 5;        // logical operator relations
inline constexpr ErrorCode BadDistance = ErrorCode::Qec_ + 6;       // declared d ≠ exhaustive search
inline constexpr ErrorCode BadLayout = ErrorCode::Qec_ + 7;         // ancilla/order/coordinates
inline constexpr ErrorCode BadOptions = ErrorCode::Qec_ + 8;        // rounds, probabilities, samples
inline constexpr ErrorCode NotClifford = ErrorCode::Qec_ + 9;       // circuit outside the QEC gate set
inline constexpr ErrorCode NotMatchable = ErrorCode::Qec_ + 10;     // a fault flips > 2 checks of a type
inline constexpr ErrorCode BadSyndrome = ErrorCode::Qec_ + 11;      // lattice does not fit the decoder
inline constexpr ErrorCode TooLarge = ErrorCode::Qec_ + 12;         // lookup table / exhaustive search
inline constexpr ErrorCode NoGate = ErrorCode::Qec_ + 13;           // code has no such transversal gate
inline constexpr ErrorCode AboveThreshold = ErrorCode::Qec_ + 14;   // p ≥ p_th in the estimator
inline constexpr ErrorCode DecodeFailed = ErrorCode::Qec_ + 15;     // correction leaves a syndrome
} // namespace err

struct Coord2 {
    double x = 0.0, y = 0.0;
    constexpr auto operator<=>(const Coord2&) const = default;
};

enum class CodeFamily : std::uint8_t { Repetition, Shor, Steane, FiveQubit, SurfaceRotated, Other };
std::string_view familyName(CodeFamily f);

// Pauli type of a stabilizer check. `Mixed` is the asset's "M": the generator mixes X, Y and Z
// letters (five-qubit code) and is measured with controlled-Pauli gates from its ancilla.
enum class CheckType : std::uint8_t { X, Z, Mixed };
std::string_view checkTypeName(CheckType t);

// Memory basis (spec 16 §3): Z prepares the +1 eigenstate of every logical Z and reads it back.
enum class LogicalBasis : std::uint8_t { Z, X };
std::string_view basisName(LogicalBasis b);

// Spec 16 §4 noise settings.
enum class NoiseSetting : std::uint8_t { CodeCapacity, Phenomenological, CircuitLevel };
std::string_view noiseSettingName(NoiseSetting s);   // "code_capacity" | "phenomenological" | "circuit_level"
std::optional<NoiseSetting> noiseSettingFromName(std::string_view name);   // `pragma qlab.noise` (spec 16 §4)

// Data-qubit error of the code-capacity and phenomenological settings. `Depolarizing` is the spec
// 16 §4 channel (X, Y, Z with p/3 each). `BitFlip` / `PhaseFlip` apply X / Z with probability p:
// the independent-error model behind the 10.3 % threshold reference and the majority-vote oracle
// of spec 16 §9.
enum class DataErrorKind : std::uint8_t { Depolarizing, BitFlip, PhaseFlip };

// Wilson score interval of k failures in n trials (same formula as data::wilson, spec 22 §2;
// qxl_qec does not link qxl_data). z = 1.959964 is the 95 % interval of spec 16 §6.
struct Interval { double lo = 0.0, hi = 1.0, center = 0.5; };
inline constexpr double kZ95 = 1.959963984540054;
Interval wilsonInterval(std::uint64_t k, std::uint64_t n, double z = kZ95);

} // namespace qlab::qec
