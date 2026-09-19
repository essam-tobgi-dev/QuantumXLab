#pragma once
// Spec 16 §2.1, §3; T09 §5.2–§5.3 — syndrome-extraction circuits. A memory experiment is planned as
// a flat Clifford `Schedule` (what the samplers and the fault propagation run) together with its
// detector and observable definitions, and lowered to an ordinary `ir::Circuit`.
#include "IR/Circuit.hpp"
#include "QEC/Code.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace qlab::qec {

struct ExtractionOptions {
    std::uint32_t rounds = 1;             // ≥ 1; the final round is followed by the data readout
    LogicalBasis basis = LogicalBasis::Z; // Z memory (prepare |0̄⟩, read Z̄) or X memory
    bool finalDataMeasurement = true;
    // CSS codes: reset every data qubit into the +1 eigenstate of the memory-basis logical
    // operator; the first extraction round then projects onto the code space. Other codes: reset
    // and run the explicit encoder of `planEncoder` on |0⟩ (Z basis) or |+⟩ (X basis) — spec 16 §3.
    bool includeLogicalPrep = true;
    // Only without the prep: the input is already a code state (e.g. after `buildEncoder`), so
    // every check is deterministic in round 0 and gets a first-layer detector.
    bool assumeCodeStateInput = false;
};

// Gate set of the generated circuits. Two-qubit kinds act on (a = control, b = target).
enum class OpKind : std::uint8_t {
    H,
    S,
    Sdg,
    X,
    Y,
    Z,
    CX,
    CY,
    CZ,
    Swap,
    Reset,
    Measure,
    Tick,
    RoundStart
};
std::string_view opName(OpKind k);
inline constexpr std::uint32_t kNoIndex = 0xFFFFFFFFu;

struct Op {
    OpKind kind = OpKind::Tick;
    std::uint32_t a = 0;          // qubit; RoundStart: the round index
    std::uint32_t b = 0;          // second qubit of a two-qubit gate
    std::uint32_t bit = kNoIndex; // Measure: classical bit (kNoIndex = discarded)
    constexpr bool operator==(const Op&) const = default;
    constexpr bool twoQubit() const {
        return kind == OpKind::CX || kind == OpKind::CY || kind == OpKind::CZ ||
               kind == OpKind::Swap;
    }
    constexpr bool marker() const { return kind == OpKind::Tick || kind == OpKind::RoundStart; }
};

// `Tick` separates parallel moments (idle noise, T09 §5.4); `RoundStart` opens an extraction round
// (where code-capacity and phenomenological data errors act, spec 16 §4). Both lower to a barrier.
struct Schedule {
    std::uint32_t qubits = 0, bits = 0;
    std::vector<Op> ops;
};

// A parity of measurement bits that is 0 in every fault-free run (T09 (5.2)). Layer t < rounds
// compares round t with round t − 1 (layer 0: the round-0 outcome alone, when the prepared state
// fixes it); layer `rounds` closes the lattice with the check's value read from the data readout.
struct Detector {
    std::uint32_t check = 0;
    std::uint32_t layer = 0;
    CheckType type = CheckType::Z;
    std::vector<std::uint32_t> bits;
};

struct MemoryExperiment {
    std::string codeId;
    ExtractionOptions options;
    std::uint32_t nData = 0,
                  nAncilla = 0; // qubit q < nData is data; nData + j measures generator j
    Schedule schedule;
    std::vector<char> dataBasis; // per data qubit 'X' | 'Y' | 'Z': prep and readout basis
    std::vector<Detector> detectors;
    // One per logical qubit: data-readout bits whose parity is the logical outcome (0 without
    // faults).
    std::vector<std::vector<std::uint32_t>> observables;
    std::vector<Coord2> qubitCoords; // placement hint for every circuit qubit

    // Bit layout of spec 16 §3: `bit[nAncilla · rounds + nData]`.
    std::uint32_t syndromeBit(std::uint32_t round, std::uint32_t check) const {
        return round * nAncilla + check;
    }
    std::uint32_t dataBit(std::uint32_t q) const { return options.rounds * nAncilla + q; }
    std::uint32_t layers() const {
        return options.rounds + (options.finalDataMeasurement ? 1u : 0u);
    }
    // Index into `detectors`, or kNoIndex when the lattice has no detector there.
    std::uint32_t detectorAt(std::uint32_t check, std::uint32_t layer) const;
    // Detection events / logical outcomes of one measurement record (or of its flips).
    std::vector<std::uint8_t> detectionEvents(std::span<const std::uint8_t> bits) const;
    std::vector<std::uint8_t> observableValues(std::span<const std::uint8_t> bits) const;

    std::vector<std::uint32_t>
        detectorIndex; // [layer · nAncilla + check] → detector, kNoIndex if none
};

// Schedule + detectors + observables for `rounds` rounds of syndrome extraction (spec 16 §3).
// SurfaceRotated codes use the four parallel CNOT layers of spec 16 §2.1 (layer taken from the
// geometry, so weight-2 boundary checks idle in the layers of their missing neighbours); every
// other code measures its generators one ancilla after the other in the ancilla's `order`.
Result<MemoryExperiment> planMemoryExperiment(const StabilizerCode& code,
                                              const ExtractionOptions& options);

// The schedule as an `ir::Circuit` on virtual qubits: registers `data[n]`, `anc[n − k]` and
// `c[nAncilla · rounds + nData]`, `meta()["interactionHint"]` with the intended coordinates and
// `meta()["qec"]` with the bit layout (spec 16 §3). Passes `ir::verify` and exports with `toQasm`.
Result<ir::Circuit> toCircuit(const MemoryExperiment& experiment);
Result<ir::Circuit> buildMemoryExperiment(const StabilizerCode& code,
                                          const ExtractionOptions& options);

// One extraction round alone (no prep, no readout): the body of `surface_d3_cycle` (spec 16 §3).
Result<ir::Circuit> buildSyndromeRound(const StabilizerCode& code);

// Lowers a Clifford `ir::Circuit` (id, x, y, z, h, s, sdg, cx, cy, cz, swap, measure, reset,
// barrier) back to a schedule; every all-wire barrier becomes a `Tick`.
Result<Schedule> compileClifford(const ir::Circuit& circuit);

} // namespace qlab::qec
