#pragma once
// Spec 16 §4, §6; spec 07 §4 — executing a QEC schedule with injected Pauli faults.
//  TableauSampler  the reference engine: the full circuit on qsim::StabilizerBackend, faults applied
//                  as Pauli frame updates of the tableau, real measurement records.
//  FrameSampler    Pauli-frame propagation of the faults alone: which record bits flip relative to
//                  the fault-free run. Detectors and observables are parities that are fixed in the
//                  fault-free run, so their values from the two engines agree shot by shot.
#include "Core/Random.hpp"
#include "QEC/Noise.hpp"
#include "QSim/Stabilizer.hpp"
#include <span>
#include <vector>

namespace qlab::qec {

// Faults must be ordered by `afterOp` and address qubits and bits of the schedule.
Status validateFaults(std::span<const FaultEvent> faults, const Schedule& schedule);

class TableauSampler {
public:
    static Result<TableauSampler> create(const Schedule& schedule);

    // One shot with exactly these faults (ordered by `afterOp`). `measurementRng` draws the random
    // measurement and reset outcomes; `bits` receives the record (`schedule.bits` entries).
    Status run(std::span<const FaultEvent> faults, core::Random& measurementRng, std::vector<std::uint8_t>& bits) const;
    // The same shot, leaving the final state in `state` (for correction and readback in tests).
    Status run(std::span<const FaultEvent> faults, core::Random& measurementRng, std::vector<std::uint8_t>& bits,
               qsim::StabilizerBackend& state) const;
    const Schedule& schedule() const { return schedule_; }

private:
    Schedule schedule_;
    qsim::StabilizerBackend pristine_;   // |0…0⟩, copied per shot (allocation logs once)
};

class FrameSampler {
public:
    explicit FrameSampler(const Schedule& schedule);

    // Record bits flipped by these faults (ordered by `afterOp`); `flips` gets `schedule.bits` entries.
    Status run(std::span<const FaultEvent> faults, std::vector<std::uint8_t>& flips);
    // Single Pauli letter on one qubit after `afterOp`, propagated to the end: the elementary
    // signatures the detector error model is assembled from (spec 16 §5.1).
    void propagate(std::uint32_t afterOp, std::uint32_t qubit, char pauli, std::vector<std::uint32_t>& flippedBits);
    const Schedule& schedule() const { return schedule_; }

private:
    void inject(const FaultEvent& f, std::vector<std::uint8_t>* flips);
    void step(const Op& op, std::vector<std::uint8_t>* flips, std::vector<std::uint32_t>* flipped);
    Schedule schedule_;
    std::vector<std::uint8_t> x_, z_;   // frame: X and Z components per qubit
};

} // namespace qlab::qec
