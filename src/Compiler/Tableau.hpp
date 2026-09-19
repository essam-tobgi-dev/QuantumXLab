#pragma once
// Spec 14 §10, T02 §5.1 — Clifford circuits as their conjugation action on the Pauli generators.
// A Clifford unitary is fixed, up to a global phase, by the 2n signed Pauli strings U X_q U† and
// U Z_q U†; comparing them is the canonical, exact equivalence test for Clifford circuits of any
// width. Gates are recognised as Clifford from their matrix (no per-gate rule table), so every
// library gate, angle, modifier and explicit matrix is covered.
#include "IR/Circuit.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace qlab::compiler {

// i^phase · Π_q X_q^{x_q} Z_q^{z_q}  (Y = i·X·Z has x = z = 1 and contributes phase 1).
struct PauliString {
    std::vector<std::uint8_t> x, z;
    std::uint8_t phase = 0;
    bool operator==(const PauliString&) const = default;
    std::string text() const; // "+XIZY", most significant qubit first
};

class CliffordTableau {
  public:
    explicit CliffordTableau(std::uint32_t qubits);
    std::uint32_t qubits() const { return n_; }
    const PauliString& imageX(std::uint32_t q) const { return rows_[q]; }
    const PauliString& imageZ(std::uint32_t q) const { return rows_[n_ + q]; }

    // Conjugates every row by the gate. Returns false, leaving the tableau unchanged, when the gate
    // is not Clifford (to 1e-9), has no matrix, or acts on more than three wires.
    bool apply(const ir::Gate& g);

  private:
    struct LocalPauli {
        std::uint8_t x = 0, z = 0, phase = 0;
    }; // bit j = wire j of the gate
    struct GateTable {
        std::vector<LocalPauli> imageX, imageZ;
    }; // per local wire
    const GateTable* tableFor(const ir::Gate& g);

    std::uint32_t n_;
    std::vector<PauliString> rows_;
    std::map<std::string, std::optional<GateTable>> cache_;
};

// Tableau of a circuit made of Clifford gates only (barriers and delays are ignored; Box bodies are
// entered). nullopt when any other node or a non-Clifford gate is present.
std::optional<CliffordTableau> cliffordTableauOf(const ir::Circuit& c);

} // namespace qlab::compiler
