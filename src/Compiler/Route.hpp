#pragma once
// Spec 14 §8 — SABRE routing (Li, Ding, Xie 2019) with lookahead, decay and bidirectional layout
// refinement. Input: a virtual circuit whose gates act on at most two qubits and an initial
// layout; output: the same program on physical wires with `swap` gates inserted so that every
// two-qubit gate acts on a coupled pair of DATA qubits.
#include "Compiler/Coupling.hpp"
#include "Compiler/Types.hpp"
#include "IR/Circuit.hpp"
#include <stop_token>

namespace qlab::compiler {

struct RouteOptions {
    std::uint64_t seed = 0x51AB5EEDull;   // tie-breaks among equally good swaps
    std::uint32_t trials = 1;             // seeds tried; fewest swaps wins, then smallest depth (level 2: 4)
    bool bidirectional = true;            // forward → reverse → forward; the last pass is the output
    bool noiseAware = false;              // D = reliability distance instead of hop count
    std::uint32_t lookahead = 20;         // |E|, the extended set
    double lookaheadWeight = 0.5;         // W
    double decay = 0.001;                 // δ per recent use of a qubit
    std::uint32_t decayReset = 5;         // swaps after which the decay counters are cleared
};

struct RoutingResult {
    ir::Circuit circuit;                  // physical: qubitCount = device qubits, no qubit registers
    Layout initialLayout, finalLayout;     // program qubit → physical qubit before / after the circuit
    std::uint32_t swaps = 0;              // inserted at the top level and inside bodies
};

// Routes `c` (virtual wires). Bodies of Branch/Loop/Box nodes are routed from the layout at the
// node and their swaps are undone at the end of the body, so every arm leaves the layout as it
// found it. Inserted swaps carry the span of the gate they enable. The result's metadata holds
// "layout" (`Circuit::layout()`), "final_layout" and "swap_count".
Result<RoutingResult> route(const ir::Circuit& c, const Layout& initial, const CouplingGraph& g,
                            const RouteOptions& options = {}, std::stop_token stop = {});

// Applies a layout without routing (`qlab.routing none`): wires are renamed and every two-qubit
// gate must already sit on a coupled pair, else QL4030.
Result<RoutingResult> applyLayout(const ir::Circuit& c, const Layout& layout, const CouplingGraph& g);

// QL4030 unless every gate on two wires acts on a coupled pair of data qubits (bodies included).
// Gates on three or more wires are rejected as not decomposed.
Status checkCoupling(const ir::Circuit& physical, const CouplingGraph& g);

// A program written on physical qubits (`pragma qlab.layout physical`) widened to the device's
// qubit count; its layout is the identity on the qubits it uses.
Result<RoutingResult> adoptPhysical(const ir::Circuit& physical, const CouplingGraph& g);

} // namespace qlab::compiler
