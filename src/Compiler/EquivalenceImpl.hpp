#pragma once
// Spec 14 §10 — a circuit precompiled for repeated simulation (matrices and renamed wires resolved
// once), and its execution with planned measurement outcomes. Internal to the Compiler module.
#include "Compiler/StateSim.hpp"
#include "IR/Circuit.hpp"
#include <cstdint>
#include <vector>

namespace qlab::compiler::detail {

struct SimOp {
    enum class Kind : std::uint8_t { Gate, Measure, Reset, Classical, Branch, Loop, Block };
    Kind kind = Kind::Gate;
    num::Matrix matrix;               // Gate: base matrix on `wires`
    std::vector<std::uint32_t> wires; // Gate targets (state wires); Measure/Reset: wires[0]
    std::size_t controlMask = 0, controlPattern = 0;
    ir::ClassicalBit bit = ir::kNoBit;            // Measure
    const ir::ClassicalOp* assign = nullptr;      // Classical (points into the source circuit)
    const ir::ClassicalExpr* condition = nullptr; // Branch / Loop
    std::uint32_t maxIterations = 0;              // Loop
    std::vector<SimOp> first, second; // Branch: then / else; Loop and Block: body in `first`
};

struct SimProgram {
    std::vector<SimOp> ops;
    std::uint64_t gates = 0; // gate applications of one straight-line execution
    bool measures = false;   // any measure, reset or classical control
};

// `wireMap[w]` is the state wire of circuit wire w (0xFFFFFFFF = not simulated).
Result<SimProgram> compileForSimulation(const ir::Circuit& c,
                                        const std::vector<std::uint32_t>& wireMap);

// One execution. Outcomes of measurements into a bit come from `planSeed` (the same function of
// (bit, occurrence) in every circuit); `probability` accumulates the probability of the outcomes.
struct SimRun {
    std::vector<std::uint8_t> bits;
    double probability = 1.0;
};
Result<SimRun> execute(const SimProgram& p, StateVec& state, std::uint32_t clbits,
                       std::uint64_t planSeed);

} // namespace qlab::compiler::detail
