#pragma once
// Spec 14 §3 — IR type vocabulary: wires, classical bits, durations, node ids, error codes.
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace qlab::ir {

// Program-level qubit, before layout (spec 02 §4). A Circuit is either entirely virtual or
// entirely physical; `Circuit::isPhysical()` says which, and `Layout` records the map in meta().
using VirtualQubit = core::Strong<std::uint32_t, struct VirtualQubitTag>;

// A wire is an index into the circuit's qubit space. Its interpretation (VirtualQubit or
// QubitIndex) comes from the owning circuit, never from the wire itself.
struct Wire {
    std::uint32_t index = 0;
    constexpr Wire() = default;
    constexpr explicit Wire(std::uint32_t i) : index(i) {}
    constexpr Wire(VirtualQubit q) : index(q.get()) {} // NOLINT(google-explicit-constructor)
    constexpr Wire(QubitIndex q) : index(q.get()) {}   // NOLINT(google-explicit-constructor)
    constexpr auto operator<=>(const Wire&) const = default;
};

// A classical bit, addressed by its index in the circuit's flat bit space.
struct ClassicalBit {
    std::uint32_t index = 0;
    constexpr ClassicalBit() = default;
    constexpr explicit ClassicalBit(std::uint32_t i) : index(i) {}
    constexpr auto operator<=>(const ClassicalBit&) const = default;
};
// `Measure::bit` of a measurement whose outcome is discarded (`measure q;`, spec 13 §3 QL3030).
inline constexpr ClassicalBit kNoBit{0xFFFFFFFFu};

// A contiguous span of the flat bit space. Bit `first + i` is bit i of the register, and i is the
// LITTLE-ENDIAN position: the register read as an integer is Σ_i bit(first+i) · 2^i (README).
struct CregRef {
    std::uint32_t first = 0;
    std::uint32_t size = 1;
    constexpr auto operator<=>(const CregRef&) const = default;
    constexpr ClassicalBit bit(std::uint32_t i) const { return ClassicalBit{first + i}; }
};

// A program duration: integer picoseconds (spec 10 §2) plus a count of device sample periods.
// `dt` is not a physical time until a device is selected, so it stays symbolic here and the
// compiler resolves it (spec 13 §3; QL4010 when no device is selected).
struct Duration {
    Picoseconds ps{0};
    std::int64_t dt = 0;
    constexpr bool symbolic() const { return dt != 0; }
    constexpr bool zero() const { return ps.get() == 0 && dt == 0; }
    constexpr Picoseconds resolve(Picoseconds dtPeriod) const {
        return Picoseconds{ps.get() + dt * dtPeriod.get()};
    }
    constexpr auto operator<=>(const Duration&) const = default;
};

// Declared registers, kept so that QASM emission can restore the original names. `scalar` marks
// `qubit q;` / `bit c;` (operand text `q`, not `q[0]`).
struct QubitRegister {
    std::string name;
    std::uint32_t first = 0;
    std::uint32_t size = 1;
    bool scalar = false;
};
// Classical storage is a flat bit space; a register's kind says how its bits are read as a value
// (Int is two's complement). Only Bit registers are declared by the program text; the builder
// allocates the other kinds for classical variables that depend on measurement results.
enum class RegKind : std::uint8_t { Bit, Bool, Int, Uint };
struct BitRegister {
    std::string name;
    std::uint32_t first = 0;
    std::uint32_t size = 1;
    RegKind kind = RegKind::Bit;
    bool scalar = false;
};

// Stable identity of a node inside one circuit. Never reused after erase.
using NodeId = core::Strong<std::uint32_t, struct NodeIdTag>;
inline constexpr NodeId kNoNode{0xFFFFFFFFu};

// Error codes owned by this module. Spec 04 §2 assigns one block per module and the IR is the
// front half of the compiler, so it shares ErrorCode::Compiler_. Offsets 1..127 mirror the QL4xxx
// diagnostics (`lang::Diagnostics::codeFor("QL4020") == Compiler_ + 20`); the IR's structural
// codes start at 128 so the two never collide.
namespace err {
inline constexpr ErrorCode UnboundInput = ErrorCode::Compiler_ + 11; // QL4011
inline constexpr ErrorCode UnrollBound = ErrorCode::Compiler_ + 20;  // QL4020
inline constexpr ErrorCode BadWire = ErrorCode::Compiler_ + 128;
inline constexpr ErrorCode BadNode = ErrorCode::Compiler_ + 129;
inline constexpr ErrorCode NotUnitary = ErrorCode::Compiler_ + 130;
inline constexpr ErrorCode UnknownGate = ErrorCode::Compiler_ + 131;
inline constexpr ErrorCode BadArity = ErrorCode::Compiler_ + 132;
inline constexpr ErrorCode TooLarge = ErrorCode::Compiler_ + 133;
inline constexpr ErrorCode NotPure = ErrorCode::Compiler_ + 134;     // measure/reset/branch present
inline constexpr ErrorCode NotConstant = ErrorCode::Compiler_ + 135; // value needed at build time
inline constexpr ErrorCode Unsupported = ErrorCode::Compiler_ + 136;
inline constexpr ErrorCode DanglingWire = ErrorCode::Compiler_ + 137;
inline constexpr ErrorCode BadBit = ErrorCode::Compiler_ + 138;     // classical bit / register
inline constexpr ErrorCode BadProgram = ErrorCode::Compiler_ + 139; // frontend reported errors
inline constexpr ErrorCode BadInput = ErrorCode::Compiler_ + 140;   // ParamMap entry rejected
} // namespace err

} // namespace qlab::ir

template <> struct std::hash<qlab::ir::Wire> {
    std::size_t operator()(qlab::ir::Wire w) const noexcept {
        return std::hash<std::uint32_t>{}(w.index);
    }
};
template <> struct std::hash<qlab::ir::ClassicalBit> {
    std::size_t operator()(qlab::ir::ClassicalBit b) const noexcept {
        return std::hash<std::uint32_t>{}(b.index);
    }
};
