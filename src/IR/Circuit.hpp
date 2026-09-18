#pragma once
// Spec 14 §3 — the circuit DAG. Nodes are ordered per wire; edges are implied by that order.
#include "Core/Json.hpp"
#include "IR/Node.hpp"
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace qlab::ir {

class Circuit {
public:
    Circuit() = default;

    // ---- shape
    std::uint32_t qubitCount() const { return qubits_; }
    std::uint32_t clbitCount() const { return clbits_; }
    void setQubitCount(std::uint32_t n);
    void setClbitCount(std::uint32_t n) { clbits_ = n; invalidate(); }
    bool isPhysical() const { return physical_; }
    void setPhysical(bool p) { physical_ = p; }

    // Registers in declaration order. Qubit register element i is wire `first + i`; bit register
    // element i is flat bit `first + i` (little-endian, README). Nested circuits carry none: the
    // top-level circuit's registers name the bits of every nested Branch/Loop/Box body.
    const std::vector<QubitRegister>& qubitRegisters() const { return qregs_; }
    const std::vector<BitRegister>& bitRegisters() const { return cregs_; }
    void addQubitRegister(QubitRegister r) { qregs_.push_back(std::move(r)); }
    void addBitRegister(BitRegister r) { cregs_.push_back(std::move(r)); }
    // Operand text of a wire / bit in its register: `q[1]`, or `q` for a scalar register.
    std::string wireName(Wire w) const;
    std::string bitName(ClassicalBit b) const;

    // ---- nodes
    NodeId add(Node n);                       // append; returns a stable id
    NodeId insertBefore(NodeId at, Node n);
    NodeId insertAfter(NodeId at, Node n);
    void erase(NodeId id);                    // id is never reused
    bool alive(NodeId id) const;
    std::size_t nodeCount() const;
    const Node& node(NodeId id) const;
    Node& node(NodeId id);                    // invalidates caches
    // Live nodes in a deterministic topological order: insertion order, which Build and every
    // mutation keep consistent with each wire's order (spec 14 §3 stable order).
    std::span<const NodeId> topologicalOrder() const;
    // Nodes touching a wire (a Barrier or Delay with no wires touches every wire), in topological order.
    std::span<const NodeId> onWire(Wire w) const;
    std::span<const NodeId> onClassicalBit(ClassicalBit b) const;

    // ---- analysis (spec 14 §1 PassMetrics, T02 §7)
    // ASAP moments over wires and classical bits; durations are ignored. Every live node appears
    // in exactly one layer; a node touching no wire or bit (gphase) sits in layer 0.
    std::vector<std::vector<NodeId>> layers() const;
    // Number of ASAP moments holding an operation on a qubit (gate, measure, reset, or a control
    // node with quantum content). Barriers, delays and global-phase gates neither count nor
    // constrain; classical nodes forward bit dependencies without adding a moment.
    std::size_t depth() const;
    std::size_t size() const;                           // quantum operations (gates+measure+reset)
    std::size_t twoQubitCount() const;                  // gates on exactly two wires (controls included)
    std::size_t tCount() const;
    std::map<std::string, std::size_t> gateCounts() const;   // gates by name, other nodes by kind
    bool hasMeasurement() const;
    bool hasClassicalControl() const;                   // Branch, Loop or ClassicalOp present
    bool isPureUnitary() const;                         // no measure/reset/branch/loop/classical op

    // ---- transforms
    Result<Circuit> inverse() const;                    // reversed, every gate inverted
    // Same shape and the same node sequence with equal payloads (parameters and matrices within
    // `tol`). Register names and source spans are not compared.
    bool structurallyEqual(const Circuit& o, double tol = 1e-12) const;

    // ---- metadata (layout, dt, device, pass trace, pragmas, calibrations — spec 14 §3)
    core::Json& meta() { return meta_; }
    const core::Json& meta() const { return meta_; }
    void setLayout(std::vector<std::uint32_t> virtualToPhysical);
    std::vector<std::uint32_t> layout() const;

private:
    void invalidate() const;
    void rebuild() const;

    std::vector<Node> nodes_;
    std::vector<bool> alive_;
    std::vector<NodeId> order_;
    std::uint32_t qubits_ = 0, clbits_ = 0;
    bool physical_ = false;
    std::vector<QubitRegister> qregs_;
    std::vector<BitRegister> cregs_;
    core::Json meta_ = core::Json::object();

    mutable bool dirty_ = true;
    mutable std::vector<NodeId> topo_;
    mutable std::vector<std::vector<NodeId>> byWire_;
    mutable std::vector<std::vector<NodeId>> byBit_;
};

// ---- whole-circuit operations
// Structural check (spec 14 §1.1 `Verify`): wires inside the circuit and distinct within a node,
// controls disjoint from targets, library arity and parameter counts, custom matrices unitary to
// num::tol::kUnitaryTol, measurement and classical bits inside declared registers, branch and loop
// conditions reading declared registers, nested bodies shaped like their parent.
Status verify(const Circuit& c);
// Full 2^n × 2^n unitary (little-endian) of a circuit on n = max(qubitCount, nQubits) ≤ 12 qubits.
// Errors on measure, reset, branch, loop, classical ops, and gates without a matrix (defcal-only).
Result<num::Matrix> toUnitary(const Circuit& c, std::uint32_t nQubits = 0);
// Deterministic text: shape, registers, then one line per node with flat wire/bit indices.
// Spans and metadata are not printed, so a toQasm round trip reproduces the dump exactly.
std::string dump(const Circuit& c);
// OpenQASM 3 that `lang::parseProgram` accepts and `buildCircuit` rebuilds to the same `dump`:
// stdgates include, `pragma qlab.*` lines from meta, calibrations, declarations in flat-bit order,
// then the body with modifiers spelled out. Fails for nodes OpenQASM cannot express (custom
// matrices, gates outside `lang::StdGates` other than defcal-only gates, non-finite parameters).
Result<std::string> toQasm(const Circuit& c);

} // namespace qlab::ir
