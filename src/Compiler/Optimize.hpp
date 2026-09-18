#pragma once
// Spec 14 §5 — circuit optimization: inverse cancellation (§5.1), single-qubit fusion and
// resynthesis (§5.2), commutation through cx-like gates (§5.3), virtual-Z bookkeeping (§5.4) and
// diagonal merging (§5.6). Every rewrite preserves the unitary up to a global phase.
#include "Compiler/Euler.hpp"
#include "Compiler/Target.hpp"
#include "IR/Circuit.hpp"
#include <stop_token>

namespace qlab::compiler {

struct OptimizeOptions {
    bool cancel = true;       // §5.1 + §5.6, looking through commuting gates when `commute` is set
    bool fuse = true;         // §5.2
    bool commute = true;      // §5.3
    std::uint32_t maxIterations = 50;   // sweeps repeat to a fixpoint; this only bounds the loop
    // Largest |θ| a merged two-qubit rotation may take (ions: one MS pulse covers π/2, T06 §6);
    // 0 = unbounded. The `Target` overload fills it in.
    double maxEntanglerAngle = 0.0;
};

struct OptimizeStats {
    std::uint32_t iterations = 0;       // sweeps over the top-level circuit
    std::uint32_t gatesBefore = 0, gatesAfter = 0;
};

// Optimizes `c` and the bodies of its Branch/Loop/Box nodes; control nodes, measurements, resets,
// barriers and delays are never crossed. Single-qubit runs are resynthesised in `basis` and
// replaced only when that lowers (pulses, gates), so a second run changes nothing (spec 25 §4).
Result<OptimizeStats> optimize(ir::Circuit& c, Basis1q basis, const OptimizeOptions& options = {},
                               std::stop_token stop = {});
inline Result<OptimizeStats> optimize(ir::Circuit& c, const Target& target, OptimizeOptions options = {},
                                      std::stop_token stop = {}) {
    options.maxEntanglerAngle = target.maxEntanglerAngle;
    return optimize(c, target.basis, options, stop);
}

// Spec 14 §5.4 — virtual Z. IR nodes carry no per-node metadata, so the `rz` gates stay in the
// circuit as zero-duration frame changes (PulseLower turns each into `shift_phase`, export prints
// them) and the pass records the frame phase every other gate sees in `c.meta()["virtual_z"]`:
//   { "frame_changes": n, "final_phase": {"<wire>": φ},
//     "phases": { "node": [i, …], "wire": [q, …], "phase": [φ, …] } }      (parallel arrays)
// with i the position in `topologicalOrder()` and φ ∈ (−π, π] the accumulated rz angle on wire q
// before node i. Only top-level gates that see a non-zero phase are listed.
struct VirtualZInfo {
    std::uint32_t frameChanges = 0;          // rz gates, nested bodies included
    std::vector<double> finalPhase;          // per wire
};
Result<VirtualZInfo> virtualZ(ir::Circuit& c);

} // namespace qlab::compiler
