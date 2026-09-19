#pragma once
// Spec 08 §7 — how each simulation backend applies an attached channel and the readout map.
//   DensityMatrix  Σ_k K_k ρ K_k† at the placement (class Exact with respect to the model).
//   StateVector    stochastic unravelling: one K_k per application with p_k = ‖K_k ψ‖², then
//                  renormalised; Pauli channels draw from their weights without a probability pass
//                  (Statistical).
//   Stabilizer     a Pauli string drawn from the channel's twirl; a non-Pauli channel needs
//                  `twirlNonPauli` and marks the run Model (§7.3).
//   Lindblad, Trajectories  refused: noise enters through NoiseModel::lindbladOperators (§7.4).
#include "Core/Fidelity.hpp"
#include "Noise/ModelTypes.hpp"
#include "QSim/Backend.hpp"
#include <span>
#include <string>
#include <vector>

namespace qlab::noise {

struct ApplyOptions {
    // Run setting `twirl_non_pauli` (default off): lets the stabilizer backend replace non-Pauli
    // channels by their Pauli twirl, which changes the model (class Model).
    bool twirlNonPauli = false;
    // Quasi-static detuning of every qubit for the current shot in Hz (index = qubit), e.g. from
    // NoiseModel::drawShotDetunings or one Gauss–Hermite node. When set, `detuning_drift` acts as
    // the coherent R_Z(2π δf t) of that shot on the StateVector and DensityMatrix backends, so an
    // echo refocuses it. When empty it acts as the shot-averaged dephasing channel, which is exact
    // for one uninterrupted window only (spec 08 §2.6). The stabilizer always uses the averaged
    // form.
    std::span<const double> shotDetuningHz;
};

// Accumulated over the applications of one run or shot.
struct ApplyReport {
    FidelityClass cls = FidelityClass::Exact;
    std::size_t applied = 0;          // channels that acted on the state
    std::size_t skipped = 0;          // identity channels (zero duration, zero strength)
    std::vector<std::string> twirled; // ids of non-Pauli channels applied as their Pauli twirl
};

Status applyChannel(qsim::IBackend& backend, const AttachedChannel& channel, core::Random& rng,
                    const ApplyOptions& options, ApplyReport& report);
Status applyChannels(qsim::IBackend& backend, std::span<const AttachedChannel> channels,
                     core::Random& rng, const ApplyOptions& options, ApplyReport& report);

// Terminal readout with assignment errors, bits in `readout.qubits()` order. DensityMatrix and
// Lindblad apply Mᵀ to the exact distribution before sampling (§7.1, §7.4); the other backends
// sample and then flip each shot's bits classically (§7.2–7.3).
Result<qsim::Counts> sampleWithReadout(const qsim::IBackend& backend, const ReadoutModel& readout,
                                       std::uint64_t shots, core::Random& rng);
// Mid-circuit measurement: projective collapse by the backend, then the classical assignment error.
Result<qsim::Outcome> measureWithReadout(qsim::IBackend& backend, const ReadoutModel& readout,
                                         core::Random& rng);

} // namespace qlab::noise
