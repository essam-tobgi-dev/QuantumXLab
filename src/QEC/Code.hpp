#pragma once
// Spec 16 §1–§2 — stabilizer codes as data: definition, JSON assets (`Assets/QEC/<id>.json`),
// load-time validation, exhaustive distance, and generators for the two scalable families.
#include "Core/Json.hpp"
#include "QEC/Pauli.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace qlab::qec {

// One measurement ancilla per generator (spec 16 §1). `order` lists the generator's data qubits in
// the order their two-qubit gates run; for the rotated surface code it is the Tomita–Svore order
// of spec 16 §2.1 restricted to the neighbours that exist.
struct AncillaSpec {
    CheckType type = CheckType::Z;
    Coord2 coord;
    std::vector<std::uint32_t> order;
};

struct StabilizerCode {
    std::string id;                               // "steane_7" (Core has no interned strings)
    std::uint32_t n = 0, k = 0, d = 0;            // [[n, k, d]]
    std::vector<PauliString> stabilizers;         // n − k independent generators
    std::vector<PauliString> logicalX, logicalZ;  // k each
    std::vector<Coord2> dataLayout;               // (x, y) per data qubit
    std::vector<AncillaSpec> ancillas;            // ancillas[j] measures stabilizers[j]
    CodeFamily family = CodeFamily::Other;
    std::vector<std::string> theoryRefs;
    std::string defaultDecoder = "union_find";    // asset field "decoder": "lookup" | "union_find"
    std::string notes;

    std::uint32_t checkCount() const { return static_cast<std::uint32_t>(stabilizers.size()); }
    // Pauli type of generator j read from its letters (not from the ancilla annotation).
    CheckType checkType(std::uint32_t j) const;
    bool isCss() const;   // every generator is purely X-type or purely Z-type
    // Logical operator j of a memory basis: logicalZ for LogicalBasis::Z, logicalX for X.
    const PauliString& logical(LogicalBasis basis, std::uint32_t j = 0) const;
    // CSS code in which every single-qubit X and Z error flips at most two generators: the decoding
    // graph of spec 16 §5.1 exists (surface, repetition and Shor codes; not Steane, whose qubit 6
    // sits in three checks of each type, and not the non-CSS five-qubit code).
    bool isMatchable() const;
};

// Minimum weights of non-trivial logical operators (T09 (2.1)), found by exhaustive search.
// `dX` / `dZ` restrict the search to strings of X / Z letters; 0 means no such operator exists.
// `declared` is the figure a code file's "d" is compared with: the distance against the error type
// the code detects when all its checks share one Pauli type (the repetition codes of T09 §4.1–4.2
// are [[3,1,1]] as quantum codes but are shipped with d = 3), otherwise `distance`.
struct DistanceReport {
    std::uint32_t distance = 0, dX = 0, dZ = 0, declared = 0;
    PauliString witness;   // a logical operator of weight `declared`
};

struct VerifyOptions {
    bool checkLayout = true;
    bool checkDistance = true;
    std::uint32_t exhaustiveDistanceMaxN = 25;   // spec 16 §1: exhaustive search for n ≤ 25
};

// Exhaustive search; fails with err::TooLarge beyond 64 qubits or 2·10^8 candidate strings.
Result<DistanceReport> computeDistance(const StabilizerCode& code);

// Spec 16 §1 `Verify`: generators commute and are independent (rank n − k), logical operators
// commute with every generator, {X̄_i, Z̄_i} = 0 and [X̄_i, Z̄_j] = 0 for i ≠ j, ancilla annotations
// describe their generators (and follow spec 16 §2.1 for SurfaceRotated), and the declared distance
// equals the exhaustive one. The error names the offending generator or operator.
Status verifyCode(const StabilizerCode& code, const VerifyOptions& options = {});

// `data` object of a "qec.code" envelope → code, without validation. Missing required fields are
// reported by path ("data.layout.ancilla[3].order"); unknown fields are ignored.
Result<StabilizerCode> codeFromJson(const core::Json& data);
core::Json codeToJson(const StabilizerCode& code);

// Parse + verify. `loadShippedCode("surface_rot_3")` reads `<assets>/QEC/surface_rot_3.json`.
Result<StabilizerCode> loadCode(const std::filesystem::path& path, const VerifyOptions& options = {});
Result<StabilizerCode> loadShippedCode(std::string_view id, const VerifyOptions& options = {});
const std::vector<std::string>& shippedCodeIds();

// CNOT layer 0..3 of a rotated-surface-code data qubit seen from its ancilla (spec 16 §2.1, T09
// §5.2; north is +y): X checks run NW, NE, SW, SE and Z checks NW, SW, NE, SE. Returns −1 when the
// data qubit is not a diagonal neighbour of the ancilla or the check is Mixed.
int tomitaSvoreLayer(CheckType type, Coord2 ancilla, Coord2 data);

// `repetition_bitflip_d` / `repetition_phaseflip_d` for odd d ≥ 3 (spec 16 §2): d data qubits on a
// line, d − 1 weight-2 checks, X̄ = X⊗d and Z̄ = Z_0 (bit flip), exchanged for the phase-flip code.
Result<StabilizerCode> makeRepetitionCode(std::uint32_t d, bool phaseFlip = false);
// `surface_rot_d` for odd d ≥ 3 with the layout, boundaries, logical operators and CNOT orders of
// spec 16 §2.1. For d = 3, 5, 7 it reproduces the shipped assets exactly.
Result<StabilizerCode> makeRotatedSurfaceCode(std::uint32_t d);

} // namespace qlab::qec
