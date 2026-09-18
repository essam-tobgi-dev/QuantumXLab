#pragma once
// Spec 16 §8 — visualization data as plain structs (no drawing here): the 2D code lattice with
// roles, CNOT-order arrows, syndrome, matched pairs and correction; the space-time syndrome lattice
// of a decoded shot; and the binding of roles to physical qubits after layout.
#include "Core/StrongType.hpp"
#include "QEC/Experiment.hpp"
#include <optional>
#include <span>
#include <vector>

namespace qlab::qec {

enum class QubitRole : std::uint8_t { Data, AncillaX, AncillaZ, AncillaMixed };
std::string_view roleName(QubitRole r);

struct RoleEntry {
    std::uint32_t circuitQubit = 0;        // wire of the generated circuit: data first, then ancillas
    QubitRole role = QubitRole::Data;
    Coord2 coord;
    std::uint32_t check = kNoIndex;        // generator measured by this ancilla
    std::optional<QubitIndex> physical;    // set by `bindToChip` (spec 17 §7 role colouring)
};

// One two-qubit gate of a check, drawn as an arrow from control to target.
struct GateArrow {
    std::uint32_t check = 0, dataQubit = 0;
    std::uint32_t step = 0;                // position in the check's order; the CNOT layer 0..3 for surface codes
    char letter = 'Z';                     // generator letter on the data qubit
    bool ancillaControls = false;          // X and mixed checks: ancilla → data; Z checks: data → ancilla
    Coord2 from, to;
};

struct CheckPair {
    std::uint32_t a = 0, b = kBoundary;    // generator indices; b = kBoundary
};

struct CodeView {
    std::string codeId;
    std::uint32_t n = 0, checks = 0, distance = 0;
    std::vector<RoleEntry> qubits;                      // n data entries, then one per ancilla
    std::vector<GateArrow> arrows;
    std::vector<std::vector<std::uint32_t>> logicalX, logicalZ;   // supports, one per logical qubit
    // Filled by `showSyndrome` / `showShot`:
    std::vector<std::uint8_t> syndrome;                 // current syndrome bit per ancilla
    std::vector<CheckPair> matched;                     // the decoder's matched pairs, projected on the code
    std::vector<char> correction;                       // Pauli letter per data qubit ('I' = none)
    FidelityClass cls = FidelityClass::Exact;
};

// Static part of the view: roles, coordinates, gate arrows, logical supports.
CodeView makeCodeView(const StabilizerCode& code);
// Code-capacity picture: a syndrome and the correction a decoder returned for it (detector = check).
void showSyndrome(CodeView& view, std::span<const std::uint8_t> syndrome, const Correction& correction);
// One round of a decoded shot: the syndrome bits of `round` (raw record on the Stabilizer engine,
// flips on the PauliFrame engine), all matched pairs and the total correction.
void showShot(CodeView& view, const MemoryExperiment& experiment, const ShotResult& shot, std::uint32_t round);
// Attaches physical qubits from a layout (`ir::Circuit::layout()`, virtual wire → physical index).
Status bindToChip(CodeView& view, std::span<const std::uint32_t> virtualToPhysical);

struct LatticePoint {
    std::uint32_t detector = 0, check = 0, layer = 0;
    CheckType type = CheckType::Z;
    Coord2 coord;                                       // of the check's ancilla; the layer is the third axis
    bool fired = false;
};
struct LatticeEdge {
    std::uint32_t a = 0, b = kBoundary;                 // indices into `points`; b = kBoundary
};
struct SpaceTimeLattice {
    std::uint32_t checks = 0, layers = 0;
    std::vector<LatticePoint> points;                   // one per detector of the experiment
    std::vector<LatticeEdge> matched;
    std::uint32_t events = 0;
    FidelityClass cls = FidelityClass::Statistical;     // a sampled shot
};
SpaceTimeLattice makeSpaceTimeLattice(const StabilizerCode& code, const MemoryExperiment& experiment, const ShotResult& shot);

} // namespace qlab::qec
