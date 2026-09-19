#pragma once
// Spec 14 §4 — decomposition to a native gate set: single-qubit synthesis (§4.2), the rule table
// (§4.3), controlled gates by the ABC construction, multi-controlled gates by Gray code, negative
// controls by X conjugation, and reversal of cx/ecr against a directed coupler.
#include "Compiler/Rules.hpp"
#include "Compiler/Target.hpp"
#include "IR/Circuit.hpp"
#include <stop_token>
#include <vector>

namespace qlab::compiler {

// Spec 14 §4.3: the Gray-code construction is exponential in the control count (QL4050 above).
inline constexpr std::size_t kMaxControls = 8;

// Lowers every gate of `c`, nested Branch/Loop/Box bodies included, to the native set of `target`.
// Global phases are dropped (they are tracked exactly only where a gate is controlled). Defcal-only
// gates pass through. On a physical circuit a cx/ecr against a directed coupler is reversed with
// Hadamards (T02 §4). Errors carry the diagnostic id and the span of the offending gate:
// QL4050 (more than 8 controls), QL4070 (no rule reaches the native set).
Status decompose(ir::Circuit& c, const Target& target, std::stop_token stop = {});

// One gate, appended to `out` in TIME order. `physical` says whether the wires are device qubits.
Status decomposeGate(const ir::Gate& g, const Target& target, bool physical,
                     std::vector<ir::Gate>& out);

// Exact expansion of an uncontrolled multi-qubit library gate into single-qubit gates and cx by
// the generic rules: gate = e^{i·phase} · (product of the returned gates).
Result<RuleExpansion> expandToCx(const ir::Gate& g);

// The inverse of a gate sequence: reversed, each gate replaced by its named inverse
// (`ir::gates::inverseOf`) or marked `adjoint`.
std::vector<ir::Gate> invertSequence(std::vector<ir::Gate> seq);

} // namespace qlab::compiler
