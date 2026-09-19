#pragma once
// Where the state-based views take their data from, in one place so that every view applies the
// same size limits of spec 21 §2.3 / §3: amplitudes in full up to 20 qubits (else the run's top-k),
// density matrices in full up to 8 qubits (else the reduced ρ of the view's qubit subset).
#include "Viz/Math/Amplitudes.hpp"
#include "Viz/ViewInput.hpp"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::viz {

struct DensitySource {
    std::optional<num::Matrix> rho; // 2^k × 2^k, qubit level (computational block)
    std::vector<QubitIndex> qubits; // the k qubits it describes; qubits[0] is the least significant
    bool reduced = false;           // a subset of the register
    bool needsReduction = false;    // the view must ask the run for `qubits` (not available yet)
    std::string note;               // why nothing can be shown ("pick at most 8 qubits")
};
// `subset` empty = the whole register.
DensitySource densityFor(const ViewInput& in, std::span<const QubitIndex> subset);

struct AmplitudeSource {
    std::optional<math::AmplitudeSelection> selection;
    bool fromRun = false; // the run's top-k (register above 20 qubits)
    std::string note;
};
AmplitudeSource amplitudesFor(const ViewInput& in, const math::AmplitudeFilter& filter);

// ρ_k of one qubit with its Bloch vector, purity, entropy and leakage, taken from — in order — the
// run's reductions, the reduced states the backend attached to the snapshot, or the state itself
// for registers of at most `inlineLimit` qubits (the O(2^n) scan of spec 21 §2.1). Shared by every
// view that shows a single-qubit state (Bloch spheres §3.1, reduced-state cards §3.10).
std::optional<SingleReduction> singleReductionFor(const ViewInput& in, QubitIndex q,
                                                  std::uint32_t inlineLimit);

// Little-endian ket label over a qubit subset, e.g. "|q3 q0> = |10>" is written by the views as
// the plain bit string; this gives the axis caption "q3 q0" (most significant first).
std::string subsetCaption(std::span<const QubitIndex> qubits);

} // namespace qlab::viz
