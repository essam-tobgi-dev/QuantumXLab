#pragma once
// Internal (spec 15 §3.5 (c)): the time-domain model of a pulse-level run — the device Hamiltonian
// of spec 09 §5.5 with the schedule's envelopes bound to its drive channels.
#include "QSim/SystemModel.hpp"
#include "Runtime/Run.hpp"

namespace qlab::runtime {

struct PulseModel {
    qsim::SystemModel model;
    std::vector<std::uint32_t> siteDims; // qubit sites first, then the ion motional mode
    std::uint32_t qubitSites = 0;        // sites that are qubits (the rest is the mode)
    std::vector<std::string> channels;   // driven Hamiltonian channels, in model order
    double durationS = 0.0;              // schedule length
    double acquireEnd = 0.0;             // end of the last acquisition window
    bool hasMode() const { return siteDims.size() > qubitSites; }
};

Result<PulseModel> buildPulseModel(const ExecutionInput& in, const ProgramPlan& plan);

// ρ restricted to the qubit sites: extra sites (the ion motional mode) are traced out, every qubit
// site is projected onto {|0⟩, |1⟩}, and the result is renormalised (the discarded weight is the
// leakage, spec 07 §5).
num::Matrix computationalRho(const num::Matrix& rho, std::span<const std::uint32_t> dims,
                             std::uint32_t qubitSites);

} // namespace qlab::runtime
