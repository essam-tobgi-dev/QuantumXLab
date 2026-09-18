#pragma once
// Spec 15 §1, §4, §10 — runtime vocabulary: identities, run options, the classical layout of a
// result, per-shot memory, snapshots and the QL5xxx error block.
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include "Data/Fidelity.hpp"
#include "Data/Histogram.hpp"
#include "IR/Build.hpp"
#include "IR/Types.hpp"
#include "Lang/Diagnostics.hpp"
#include "QSim/Types.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::runtime {

using ProgramId = core::Strong<std::uint32_t, struct ProgramIdTag>;
using CompileHandle = core::Strong<std::uint32_t, struct CompileHandleTag>;
using RunHandle = core::Strong<std::uint32_t, struct RunHandleTag>;
using RunId = core::Strong<std::uint64_t, struct RunIdTag>;

// Spec 15 §1. `Auto` applies the §2 table; anything else is pinned and never falls back.
enum class BackendChoice : std::uint8_t { Auto, StateVector, DensityMatrix, Stabilizer, Lindblad };
std::string_view backendChoiceName(BackendChoice c);

enum class NoiseSource : std::uint8_t { Ideal, Calibrated, Custom };
std::string_view noiseSourceName(NoiseSource n);

// Spec 15 §3.6. `Layer` is the default; `Gate` is used automatically at n ≤ 20 and `Barrier` above.
enum class SnapshotCadence : std::uint8_t { None, Gate, Layer, Barrier, End };
std::string_view snapshotCadenceName(SnapshotCadence c);
// The cadence actually used for `n` simulated qubits: per gate or layer up to 20 qubits, per
// barrier above (a full state copy per gate dominates the run there).
SnapshotCadence resolveCadence(SnapshotCadence c, std::uint32_t n);

// Error codes owned by this module. Offsets below 128 mirror the QL5xxx catalogue
// (`lang::Diagnostics::codeFor("QL5011") == Runtime_ + 11`).
namespace err {
inline constexpr ErrorCode UnboundInput = ErrorCode::Runtime_ + 1;      // QL5001
inline constexpr ErrorCode LindbladCap = ErrorCode::Runtime_ + 10;      // QL5010
inline constexpr ErrorCode NoBackend = ErrorCode::Runtime_ + 11;        // QL5011
inline constexpr ErrorCode MemoryBudget = ErrorCode::Runtime_ + 12;     // QL5012
inline constexpr ErrorCode LoopBound = ErrorCode::Runtime_ + 20;        // QL5020 (warning)
inline constexpr ErrorCode SweepTooLarge = ErrorCode::Runtime_ + 30;    // QL5030
inline constexpr ErrorCode ShotsOutOfRange = ErrorCode::Runtime_ + 40;  // QL5040
inline constexpr ErrorCode BadNoiseFile = ErrorCode::Runtime_ + 50;     // QL5050
inline constexpr ErrorCode Cancelled = ErrorCode::Runtime_ + 60;        // QL5060 (warning)
// Conditions without a catalogue id.
inline constexpr ErrorCode NoDevice = ErrorCode::Runtime_ + 100;
inline constexpr ErrorCode NotCompiled = ErrorCode::Runtime_ + 101;
inline constexpr ErrorCode UnknownHandle = ErrorCode::Runtime_ + 102;
inline constexpr ErrorCode Unsupported = ErrorCode::Runtime_ + 103;
} // namespace err

// Spec 15 §10 / common rule 8: every runtime diagnostic comes from the catalogue.
lang::Diagnostic diagnostic(std::string_view id, SourceSpan span = {});
template <class... Args> lang::Diagnostic diagnostic(std::string_view id, SourceSpan span, Args&&... args) {
    return lang::Diagnostics::make(id, std::move(span), std::forward<Args>(args)...);
}
// The same, as an `Error` carrying the id (for `Result<T>` returns).
Error error(const lang::Diagnostic& d);
// A finding with no catalogue id: run notes that are not one of the QL5xxx conditions of §10.
lang::Diagnostic note(std::string message, lang::Severity severity = lang::Severity::Info);

// ---------------------------------------------------------------- classical memory
struct RegisterInfo {
    std::string name;
    std::uint32_t first = 0, size = 1;
    ir::RegKind kind = ir::RegKind::Bit;
    bool scalar = false;
    bool output = false; // declared `output` in the program (spec 13 §3)
};

// Register names, widths and bit order of a result's classical memory (spec 15 §4). Bit `first + i`
// of a register is its LITTLE-ENDIAN position i; a bitstring is printed most significant flat bit
// first, so `bit[3] c` prints as c[2]c[1]c[0] (README).
struct ClassicalLayout {
    std::vector<RegisterInfo> registers;
    std::uint32_t bits = 0;

    std::string key(std::span<const std::uint8_t> shotBits) const;       // MSB-first over all bits
    std::uint64_t value(std::span<const std::uint8_t> shotBits, const RegisterInfo& r) const;
    const RegisterInfo* find(std::string_view name) const;
};

// One shot: the classical registers as written by this shot, plus the program's `output` values.
struct ShotRecord {
    std::vector<std::uint8_t> bits;   // flat classical bit space, index = bit
    std::vector<double> outputs;      // parallel to the layout's `output` registers
    bool truncated = false;           // a runtime loop hit `maxIterations` (QL5020)
    bool operator==(const ShotRecord&) const = default;
};

// Per register, per bit: P(bit = 1) with its binomial error (spec 15 §4).
struct Marginal {
    std::string register_;
    std::uint32_t bit = 0;      // index inside the register
    std::uint32_t qubit = 0;    // physical qubit that was measured into it (kNoQubit if unknown)
    double p1 = 0.0, stderr_ = 0.0;
    data::Interval interval{0.0, 0.0, 0.0};
    static constexpr std::uint32_t kNoQubit = 0xFFFFFFFFu;
};

// ⟨P⟩ from counts with σ = √((1 − ⟨P⟩²)/N), or an exact value read from the state (Simulator-only).
struct Expectation {
    std::string observable;                 // "Z0", "Z0Z1", "out"
    double value = 0.0, stderr_ = 0.0;
    data::FidelityClass cls = data::FidelityClass::Statistical;
    std::optional<double> exact;            // from the final state where available (Simulator-only)
};

// A measurement of the compiled circuit: which simulated qubit landed in which classical bit.
struct MeasuredBit {
    std::uint32_t qubit = 0;     // index in `RunResult::qubits` (simulator index)
    std::uint32_t physical = 0;  // device qubit
    ir::ClassicalBit bit{};
};

// Spec 12 §8 / 13 §7: a probe the program declares (`pragma qlab.probe`) or the run dialog binds.
// `qubits` are DEVICE qubit indices; empty means every qubit the program uses.
struct ProbeRequest {
    std::string kind;                  // state | bloch | entanglement | density
    std::vector<std::uint32_t> qubits;
    bool atBarriers = false;
};

// Spec 12 §8 probe declared by `pragma qlab.probe` / bound to a run; evaluated on the worker.
struct ProbeValue {
    std::string kind;                  // state | bloch | entanglement | density
    std::vector<std::uint32_t> qubits; // simulator indices
    std::vector<double> values;        // bloch: x,y,z; entanglement: S; density: flattened ρ
    data::FidelityClass cls = data::FidelityClass::Exact;
};

// Immutable state handed to the views at a gate boundary (spec 15 §3.6). The state is shared, never
// copied per consumer; the App adapts this into `instr::RunView` / `viz::ViewInput`.
struct RunSnapshot {
    double timeS = 0.0;                 // schedule time of the boundary (pulse playhead)
    std::uint64_t gateIndex = 0;        // index into `topologicalOrder()` of the compiled circuit
    std::uint32_t layerIndex = 0;
    ir::NodeId node = ir::kNoNode;
    std::uint32_t shot = 0;
    std::shared_ptr<const qsim::Snapshot> state;
    std::vector<std::int8_t> measuredBits; // per simulated qubit: outcome so far, −1 = not measured
    std::vector<ProbeValue> probes;
};

} // namespace qlab::runtime
