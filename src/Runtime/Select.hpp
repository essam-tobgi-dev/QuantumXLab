#pragma once
// Spec 15 §2 — the `auto` backend policy. The table is expressed through `qsim::selectBackend`, so
// the capability and memory rules of spec 07 §7 stay in one place; this adds the run-level
// conditions (Clifford only above 28 qubits, density matrix only from 256 shots) and turns a
// refusal into QL5010 / QL5011.
#include "QSim/Backend.hpp"
#include "Runtime/Plan.hpp"
#include "Runtime/Types.hpp"

namespace qlab::runtime {

struct BackendRequest {
    const ProgramPlan* plan = nullptr;
    BackendChoice choice = BackendChoice::Auto;
    bool hasNoise = false;
    bool pauliNoiseOnly = false;
    bool pulseLevel = false;
    std::uint32_t levels = 2;
    std::uint32_t shots = 1024;
};

// The selected backend with the reason shown in the run header. An explicit choice that cannot run
// the program is a hard error, never a silent fallback (spec 15 §2).
Result<qsim::Selection> chooseBackend(const BackendRequest& req);

// Spec 15 §2 stabilizer rule: Clifford + measurement/reset above this many qubits.
inline constexpr std::uint32_t kStabilizerThreshold = 28;
// Spec 15 §2 density-matrix rule: one exact evolution pays off from this many shots.
inline constexpr std::uint32_t kDensityMatrixShots = 256;
inline constexpr std::uint32_t kDensityMatrixQubits = 12;

} // namespace qlab::runtime
