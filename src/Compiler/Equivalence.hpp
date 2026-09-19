#pragma once
// Spec 14 §10 — equivalence of a compiled circuit with its source, up to a global phase and up to
// the recorded qubit mapping. Never modifies a circuit.
//
// Mapping: the compiled circuit's metadata holds `layout` (program qubit → physical qubit at the
// start, `Circuit::layout()`) and `final_layout` (the same at the end, after routing swaps).
// Program qubit v enters on wire layout[v] and is read back on wire final_layout[v]; every other
// wire the compiled circuit touches is a routing ancilla that starts in |0⟩ and must end in |0⟩
// (a swap network only moves it around). Without the metadata both maps are the identity.
#include "Core/Error.hpp"
#include "IR/Circuit.hpp"
#include <cstdint>
#include <string>

namespace qlab::compiler {

enum class EquivalenceMethod : std::uint8_t {
    Auto,    // Clifford when both circuits are; else Unitary within its caps; else RandomStates
    Unitary, // all 2^k basis inputs: |tr(U_a† U_b)| / 2^k ≥ 1 − ε (global-phase invariant)
    RandomStates, // Haar-random inputs: |⟨ψ|U_a† U_b|ψ⟩| ≥ 1 − ε for every one
    Clifford,     // exact comparison of the Pauli conjugation tableaux, any width
    Skipped,      // reported when nothing could be checked (defcal-only gates, caps exceeded)
};
std::string_view equivalenceMethodName(EquivalenceMethod m);

struct EquivalenceOptions {
    EquivalenceMethod method = EquivalenceMethod::Auto;
    double tolerance = 1e-9;             // ε of spec 14 §10
    std::uint32_t maxUnitaryQubits = 10; // wires simulated by the Unitary method
    std::uint32_t maxStateQubits =
        24;                     // wires simulated by RandomStates (2^24 amplitudes = 256 MiB)
    std::uint32_t states = 32;  // random inputs
    std::uint32_t branches = 2; // outcome assignments tried per input when the circuit measures
    std::uint64_t seed = 0xEC01A11Eull;
    // Upper bound on amplitude updates (gates × 2^wires × inputs). 0 = unbounded. Auto trades the
    // Unitary method for fewer random states to stay inside it and reports Skipped if it cannot.
    std::uint64_t workLimit = 0;
};

struct EquivalenceReport {
    bool equivalent = false;
    EquivalenceMethod method = EquivalenceMethod::Skipped;
    std::uint32_t wires = 0; // wires simulated for the compiled circuit
    std::uint32_t inputs =
        0; // basis states, random states × branches, or Pauli generators compared
    double worstOverlap = 1.0; // min over inputs of |⟨ref|compiled⟩|, or |tr|/2^k
    std::string detail;        // why it failed or was skipped
};

// Circuits with measurement, reset or classical control are compared operationally on random
// inputs: each measurement into a bit takes a planned outcome (the same in both circuits; flipped
// when that outcome is impossible), a discarded measurement or reset takes its more probable
// outcome, branches and loops follow the classical memory; final states, final classical memory
// and the probability of the outcome string must agree (spec 14 §10 "per-branch equivalence").
// The check assumes that both circuits list their measurements, resets and classical nodes in the
// same order, which every pass of this module preserves (the router chains them, SabreDag.cpp).
Result<EquivalenceReport> checkEquivalence(const ir::Circuit& reference,
                                           const ir::Circuit& compiled,
                                           const EquivalenceOptions& options = {});

} // namespace qlab::compiler
