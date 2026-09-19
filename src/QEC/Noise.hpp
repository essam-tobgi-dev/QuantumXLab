#pragma once
// Spec 16 §4, T09 §5.4 — noise injection for QEC experiments. Every channel here is a Pauli
// channel, so a shot is the fault-free Clifford circuit plus a list of sampled Pauli faults; the
// same list drives the stabilizer backend and the Pauli-frame propagation.
//
// Convention: "depolarizing with p" means total error probability p — each of X, Y, Z with p/3,
// each of the 15 two-qubit Paulis with p/15 (spec 16 §4 code-capacity row, T09 §5.4). The Kraus
// parameter of spec 08 §3 (`depolarizing_1q`, weights p₀₈/4) is p₀₈ = 4p/3, and 16p/15 for two
// qubits.
#include "Core/Random.hpp"
#include "QEC/Extraction.hpp"
#include <cstdint>
#include <vector>

namespace qlab::qec {

struct NoiseParams {
    NoiseSetting setting = NoiseSetting::CircuitLevel;
    double p = 0.0; // physical error rate
    // Phenomenological syndrome-bit flip probability; negative = p (spec 16 §4 default q = p).
    double q = -1.0;
    // Data error of the code-capacity and phenomenological settings (circuit level is
    // depolarizing).
    DataErrorKind dataError = DataErrorKind::Depolarizing;
    // Circuit level only: depolarizing probability of a qubit that idles through a moment (the span
    // between two schedule markers), T09 §5.4. 0 keeps the spec 16 §4 table, which has no idle
    // term.
    double pIdle = 0.0;

    double measurementFlip() const { return q < 0.0 ? p : q; }
};

// p₀₈ of spec 08 §3 for a total error probability p on `qubits` ∈ {1, 2}: p · 4ⁿ/(4ⁿ − 1).
double krausDepolarizingParameter(double p, std::uint32_t qubits);

enum class SiteKind : std::uint8_t { Pauli1, Depolarize2, RecordFlip };

// A place where the noise model can act: after `schedule.ops[afterOp]`.
struct NoiseSite {
    std::uint32_t afterOp = 0;
    SiteKind kind = SiteKind::Pauli1;
    std::uint32_t a = 0, b = 0;          // qubits; RecordFlip: a = classical bit
    double px = 0.0, py = 0.0, pz = 0.0; // Pauli1: X, Y, Z. Depolarize2 / RecordFlip: px = total p
    double total() const { return kind == SiteKind::Pauli1 ? px + py + pz : px; }
};

struct NoisePlan {
    NoiseParams params;
    std::vector<NoiseSite> sites; // ordered by `afterOp`
};

// One elementary fault: a Pauli on one or two qubits after an operation, or a flipped record bit.
struct FaultEvent {
    std::uint32_t afterOp = 0;
    std::uint32_t qubitA = kNoIndex, qubitB = kNoIndex;
    char pauliA = 'I', pauliB = 'I';
    std::uint32_t flipBit = kNoIndex;
    double probability = 0.0; // filled by `enumerateFaults`
    std::uint32_t site = 0;   // index into NoisePlan::sites; faults of one site exclude each other
};

// Noise sites of a planned experiment (spec 16 §4):
//  CodeCapacity      data error on every data qubit once, at the start of round 0; nothing else.
//  Phenomenological  data error at the start of every round + each syndrome bit flipped with q.
//  CircuitLevel      1q gate → depolarizing p; 2q gate → two-qubit depolarizing p; reset → X with
//                    p; measurement → record flipped with p; optional idle depolarizing `pIdle`.
Result<NoisePlan> planNoise(const MemoryExperiment& experiment, const NoiseParams& params);
// Circuit-level sites of an arbitrary schedule (e.g. `compileClifford` of a routed circuit).
Result<NoisePlan> planCircuitNoise(const Schedule& schedule, const NoiseParams& params);

// Samples one shot: exactly one `rng.uniform()` per site, in site order, so a seed fixes the fault
// list independently of the engine that replays it. `out` is cleared first.
void drawFaults(const NoisePlan& plan, core::Random& rng, std::vector<FaultEvent>& out);

// Every elementary fault of the plan with its probability (3 per Pauli1 site with all letters
// enabled, 15 per Depolarize2 site, 1 per RecordFlip site): the fault set of spec 16 §5.1.
std::vector<FaultEvent> enumerateFaults(const NoisePlan& plan);

} // namespace qlab::qec
