#pragma once
// Spec 21 §1, §2.3 — reductions of a snapshot that exceed the UI-thread cost bound: reduced
// density matrices, entropies, pair mutual information and concurrence, a Schmidt spectrum, Pauli
// expectations, the top-k amplitudes of a large register and a Wigner grid. Spec 21 has the run
// deliver them through `RunSnapshot::request(Reduction)`; with Viz below Runtime (SPEC_DEVIATIONS)
// each view states what it needs as a `ReductionRequest`, the App merges the requests of the open
// views and runs `computeReductions` on the job system, and the result comes back in a later
// `ViewInput`. Until then a view shows its previous result with a "stale" indicator.
#include "Core/Error.hpp"
#include "Data/Fidelity.hpp"
#include "QSim/Types.hpp"
#include "Viz/Math/Amplitudes.hpp"
#include "Viz/Math/Reduced.hpp"
#include "Viz/Math/Wigner.hpp"
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace qlab::viz {

inline constexpr std::uint32_t kPairReductionMaxQubits = 20;  // spec 21 §3.8: all pairs only for n ≤ 20
inline constexpr std::uint32_t kSubsetMaxQubits = 8;          // spec 21 §3.5: city / Hinton limit

struct ReductionRequest {
    bool singles = false;                               // ρ_k, Bloch vector, purity, entropy per qubit
    bool pairs = false;                                 // S_i, S_j, S_ij, I(i:j), C_ij for all pairs
    std::vector<QubitIndex> qubits;                     // restrict singles/pairs to these (empty = all)
    std::vector<std::vector<QubitIndex>> subsets;       // reduced ρ of each subset (≤ 8 qubits, first = LSB)
    std::optional<std::vector<QubitIndex>> schmidtPartition;
    bool singleQubitPaulis = false;                     // ⟨X_k⟩, ⟨Y_k⟩, ⟨Z_k⟩ for every qubit
    std::vector<std::string> pauliStrings;              // user strings, MSB-first ("XZIY")
    std::size_t topAmplitudes = 0;                      // k of the top-k selection (0 = none)
    std::optional<math::WignerOptions> wigner;          // grid for `modeState`
    std::shared_ptr<const num::Matrix> modeState;       // oscillator ρ in the Fock basis (spec 21 §3.17)

    bool empty() const;
    // Union of two requests (the App merges what every open view asks for).
    void merge(const ReductionRequest& other);
};

struct SingleReduction {
    QubitIndex qubit{0};
    num::Matrix rho;                 // `levels` × `levels`
    math::BlochVector bloch;         // from the computational block; leakage shortens it
    double purity = 1.0;             // Tr ρ_k²
    double entropyBits = 0.0;        // S(ρ_k)
    double leakage = 0.0;            // population outside {|0⟩, |1⟩}
};

struct PairReduction {
    QubitIndex i{0}, j{0};           // i < j
    math::PairMeasures measures;
};

struct PauliValue {
    std::string label;               // row label ("Z0") or the MSB-first string
    std::string pauli;               // full-width MSB-first string, the key views look rows up by
    double value = 0.0;
    bool userAdded = false;
};

struct Reductions {
    // The snapshot these belong to: a view compares them with its current snapshot (stale flag).
    std::uint64_t gateIndex = 0;
    double simTimePs = 0.0;
    std::uint32_t nQubits = 0, levels = 2;
    data::FidelityClass cls = data::FidelityClass::Exact;

    std::vector<SingleReduction> singles;
    std::vector<PairReduction> pairs;
    std::vector<qsim::ReducedState> subsets;     // qubit-level (computational block), in request order
    std::optional<math::SchmidtSpectrum> schmidt;
    std::vector<PauliValue> paulis;
    std::optional<math::AmplitudeSelection> topAmplitudes;
    std::optional<math::WignerGrid> wigner;
    std::vector<std::string> notes;              // why a requested item is absent ("Schmidt spectrum needs a pure state")

    const SingleReduction* single(QubitIndex q) const;
    const PairReduction* pair(QubitIndex a, QubitIndex b) const;      // either order
    const qsim::ReducedState* subset(std::span<const QubitIndex> qubits) const;
    const PauliValue* pauli(std::string_view fullWidthLabel) const;
};

// Runs every requested reduction on the snapshot's state (amplitudes, else density matrix, else
// the reduced states the backend attached). Meant for a worker thread: `stop` is polled between
// items and cancellation returns ErrorCode::Cancelled. A request the state cannot serve (Schmidt
// spectrum of a mixed state, pairs above 20 qubits, anything on a tableau-only snapshot) is
// reported in `notes`, not as an error; an invalid request (qubit out of range) is an error.
Result<Reductions> computeReductions(const qsim::Snapshot& snapshot, const ReductionRequest& request,
                                     std::stop_token stop = {});

} // namespace qlab::viz
