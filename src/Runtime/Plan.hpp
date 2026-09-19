#pragma once
// Spec 15 §2–§3 — what the runtime needs to know about a compiled circuit before it runs it:
// which physical qubits it uses (the simulator holds those, not the whole device), where the
// measurements land, the classical layout, and which execution model applies.
#include "Compiler/Pass.hpp"
#include "IR/Circuit.hpp"
#include "Runtime/Types.hpp"
#include <cstdint>
#include <vector>

namespace qlab::runtime {

// Immutable analysis of one compiled circuit. `qubits[s]` is the device qubit simulated at index s;
// `toSim[p]` is the simulator index of device qubit p, or −1.
struct ProgramPlan {
    std::vector<std::uint32_t> qubits;
    std::vector<std::int32_t> toSim;
    std::vector<MeasuredBit> measurements;     // in execution order; the last one per qubit wins
    std::vector<std::uint32_t> measuredQubits; // simulator indices, first-measurement order
    ClassicalLayout layout;

    bool terminalMeasurementOnly =
        true;                    // no Branch/Loop/ClassicalOp and every Measure last on its wire
    bool hasFeedforward = false; // Branch or Loop present
    bool hasMidCircuitMeasure = false;
    bool hasReset = false;
    bool hasLoop = false;
    bool cliffordOnly = true;      // Clifford gates + measure/reset only (spec 15 §2)
    std::uint32_t branchCount = 0; // static Branch/Loop nodes (per shot upper bound)
    std::uint64_t gateCount = 0;   // gate applications of one shot, nested bodies included
    std::uint32_t depth = 0;

    std::uint32_t nQubits() const { return static_cast<std::uint32_t>(qubits.size()); }
    // Device qubits of a node's wires mapped to simulator indices.
    std::vector<QubitIndex> sim(std::span<const ir::Wire> wires) const;
};

// Device qubits touched by any node, ascending (Barrier/Delay over "every wire" are ignored: they
// constrain time, not occupancy).
std::vector<std::uint32_t> usedQubits(const ir::Circuit& c);

// `outputs` names the program's `output` variables (spec 13 §3); their registers are flagged in the
// layout so that `ShotRecord::outputs` can be filled.
Result<ProgramPlan> planProgram(const ir::Circuit& c, std::span<const std::string> outputs = {});

// Spec 15 §6: the ASAP critical path over non-measurement operations, T12 (1.2). Falls back to
// `metrics.estimatedDuration` when the program was compiled without a schedule.
Picoseconds circuitCriticalPath(const ir::Circuit& c, const compiler::ScheduleInfo& timing);

// Idle gaps of one wire as consecutive nodes leave them, in topological order: the gap before
// `nodes[k]` on that wire. The wait before a wire's first node is not idle — the qubit is still in
// |0⟩ (spec 14 §9, 08 §4).
struct IdleGap {
    std::uint32_t wire = 0;
    std::size_t nodeIndex = 0; // index into `topologicalOrder()`
    Picoseconds length{0};
};
std::vector<IdleGap> idleGaps(const ir::Circuit& c, const compiler::ScheduleInfo& timing);

} // namespace qlab::runtime
