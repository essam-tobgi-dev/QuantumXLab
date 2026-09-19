#pragma once
// Spec 14 §2 — AST → IR lowering (`Build`, the first pass of the standard pipeline).
#include "IR/Circuit.hpp"
#include "Lang/Sema.hpp"
#include <map>
#include <string>

namespace qlab::ir {

// Values bound to `input` declarations before the build (spec 14 §2 "Input binding"). Integers and
// booleans are given by their numeric value; durations in picoseconds. An input that is neither in
// the map nor given a default in the program is QL4011 at the point it is needed.
using ParamMap = std::map<std::string, double>;

struct BuildOptions {
    std::uint32_t loopUnrollBound = 65536; // CompileContext::loopUnrollBound (spec 14 §1)
    std::uint32_t maxInlineDepth = 64;     // guard; Sema already rejects recursion (spec 13 §3)
};

// Lowers an analysed program: loops unrolled, `def`s inlined, user gate bodies expanded, modifiers
// resolved into `controls`/`adjoint`/parameter rewrites, `if` lowered to `Branch`, registers mapped
// to wires in little-endian order, and the statement's SourceSpan kept on every node.
Result<Circuit> buildCircuit(const lang::Program& p, const ParamMap& inputs = {},
                             const BuildOptions& opts = {});

} // namespace qlab::ir
