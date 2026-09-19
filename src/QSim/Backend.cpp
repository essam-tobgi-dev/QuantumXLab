// Spec 07 §1 — default IBackend behaviour (vtable anchor).
#include "QSim/Backend.hpp"

namespace qlab::qsim {

// Default: ignore the class hint and route through the generic entry points. Backends with
// fast paths (state vector, stabilizer) override this.
Status IBackend::apply(const GateOp& op) {
    if (op.controls.empty())
        return applyGate(op.matrix, op.targets);
    return applyControlled(op.matrix, op.controls, op.targets);
}

} // namespace qlab::qsim
