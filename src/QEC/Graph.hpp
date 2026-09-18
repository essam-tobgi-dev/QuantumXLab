#pragma once
// Spec 16 §5.1, T09 §5.3 — the decoding graph of a matching-type code. Vertices are detectors
// (check, layer); an edge is a fault class that fires its two end detectors (space, time, diagonal
// and hook edges) or one detector and the virtual boundary.
#include "QEC/Noise.hpp"
#include <cstdint>
#include <utility>
#include <vector>

namespace qlab::qec {

inline constexpr std::uint32_t kBoundary = 0xFFFFFFFFu;

struct GraphEdge {
    std::uint32_t a = 0, b = kBoundary;   // detector indices, a < b; b = kBoundary for a boundary edge
    double probability = 0.0;             // of an odd number of the edge's faults (0 = unit-weight graph)
    double weight = 1.0;                  // ln((1 − p)/p), T09 §6.2; 1 in a unit-weight graph
    // Pauli on the data qubits equivalent to the edge's faults: what the decoder applies when it
    // selects the edge. Empty for measurement (time-like) faults.
    std::vector<std::pair<std::uint32_t, char>> correction;
    std::uint64_t observableMask = 0;     // bit l: the faults flip the readout of logical qubit l
    bool boundary() const { return b == kBoundary; }
};

struct DecodingGraph {
    std::uint32_t nData = 0;
    std::uint32_t detectors = 0;
    std::vector<GraphEdge> edges;
    std::vector<std::vector<std::uint32_t>> incident;   // detector → edge indices

    // Notes of the detector-error-model construction (spec 16 §5.1, Model class).
    std::uint32_t faults = 0;                 // elementary faults propagated
    std::uint32_t hyperedgesDecomposed = 0;   // faults firing > 2 detectors, split into known edges
    std::uint32_t hyperedgesDropped = 0;      // … with no decomposition: ignored by the decoder
    std::uint32_t ambiguousEdges = 0;         // same detectors, different logical action (distance ≤ 2)
    double undetectedLogicalProbability = 0.0; // faults flipping a logical readout with no detector
    FidelityClass cls = FidelityClass::Exact;

    std::uint32_t findEdge(std::uint32_t a, std::uint32_t b = kBoundary) const;   // kNoIndex if absent
    // Weights from probabilities (or all 1 when `unitWeights`) and incidence lists.
    void finalize(bool unitWeights);
};

// Code-capacity graph read off the code itself (spec 16 §5.1, unit weights): detector j is
// generator j, and every single-qubit X and Z error is an edge between the (at most two)
// generators it anticommutes with. Fails with err::NotMatchable for codes that are not matchable.
Result<DecodingGraph> buildCodeCapacityGraph(const StabilizerCode& code);

// Detector error model of an experiment under a noise plan (spec 16 §5.1): every elementary fault
// is propagated through the schedule and recorded by the detectors it fires. For a CSS code the X
// and Z checks form separate components, so a fault is split by check type; the logical action
// travels with the component of the readout basis. Faults firing more than two detectors of one
// component are decomposed into existing edges.
Result<DecodingGraph> buildDecodingGraph(const MemoryExperiment& experiment, const NoisePlan& noise);

} // namespace qlab::qec
