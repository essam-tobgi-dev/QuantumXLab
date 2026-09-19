#pragma once
// Reading quantum states out of a RunView snapshot (qsim::Snapshot): the probes of spec 12 §12 and
// the VNA's live mode go through these helpers, never to a backend. Little-endian: qubit 0 is the
// least significant site; every site has `snapshot.levels` levels.
#include "Instruments/Types.hpp"
#include "QSim/Types.hpp"
#include <span>
#include <vector>

namespace qlab::instr {

// Reduced density matrix of `qubits` (any order; the result is ordered by ascending qubit, the
// lowest index least significant). Taken from `snapshot.reduced` when the runtime already supplies
// that subsystem, else by partial trace of the density matrix or of the amplitudes.
// Errors: BadInput (no state data, qubit out of range, size mismatch).
Result<num::Matrix> reducedDensity(const qsim::Snapshot& snapshot,
                                   std::span<const std::uint32_t> qubits);
// Level populations of one site, size `snapshot.levels`.
Result<std::vector<double>> levelPopulations(const qsim::Snapshot& snapshot, std::uint32_t qubit);
// The computational 2×2 block of a single-site state (not renormalised: leakage shortens the Bloch
// vector).
num::Matrix qubitBlock(const num::Matrix& site);
// Full density matrix of the snapshot (|ψ⟩⟨ψ| for amplitudes). Errors: BadInput when neither exists
// or the register is too large to expand (more than 12 sites).
Result<num::Matrix> fullDensity(const qsim::Snapshot& snapshot);

} // namespace qlab::instr
