#pragma once
// Spec 14 §3 — helpers every pass shares. `ir::Circuit` inserts in O(n) and rebuilds its wire
// index after any mutation, so passes read the node list once, work on a plain vector, and write a
// fresh circuit with the same shape, registers and metadata.
#include "Compiler/Types.hpp"
#include "IR/Circuit.hpp"
#include <utility>
#include <vector>

namespace qlab::compiler {

// A circuit with the shape, registers and metadata of `c` and no nodes.
ir::Circuit shellLike(const ir::Circuit& c);
// Live nodes in topological order, moved out; `c` must be refilled with `setNodes` afterwards.
std::vector<ir::Node> takeNodes(ir::Circuit& c);
// Copies of the live nodes in topological order.
std::vector<ir::Node> copyNodes(const ir::Circuit& c);
// Replaces the node list of `c`, keeping shape, registers and metadata.
void setNodes(ir::Circuit& c, std::vector<ir::Node> nodes);

// Calls `fn(ir::Circuit&)` on every nested body of a control node (Branch then/else, Loop, Box)
// and stops at the first error.
template <class F> Status forEachBody(ir::Node& n, F&& fn) {
    if (auto* b = std::get_if<ir::Branch>(&n)) {
        QXL_TRY(fn(*b->thenBody));
        if (b->elseBody.present())
            QXL_TRY(fn(*b->elseBody));
    } else if (auto* l = std::get_if<ir::Loop>(&n)) {
        QXL_TRY(fn(*l->body));
    } else if (auto* bx = std::get_if<ir::Box>(&n)) {
        QXL_TRY(fn(*bx->body));
    }
    return {};
}
template <class F> void forEachBody(const ir::Node& n, F&& fn) {
    if (const auto* b = std::get_if<ir::Branch>(&n)) {
        fn(*b->thenBody);
        if (b->elseBody.present())
            fn(*b->elseBody);
    } else if (const auto* l = std::get_if<ir::Loop>(&n)) {
        fn(*l->body);
    } else if (const auto* bx = std::get_if<ir::Box>(&n)) {
        fn(*bx->body);
    }
}
inline bool isControlNode(const ir::Node& n) {
    return std::holds_alternative<ir::Branch>(n) || std::holds_alternative<ir::Loop>(n) ||
           std::holds_alternative<ir::Box>(n);
}

// Gives every nested body the shape of its parent: qubit count, bit count and the physical flag
// (`ir::verify` requires it). `ir::buildCircuit` materialises the absent `else` body of an `if` as
// an empty VIRTUAL circuit, which fails verification in a `qlab.layout physical` program.
void normalizeBodies(ir::Circuit& c);

// Library gate with `cls` filled in; the names passed by the compiler are library names, so a
// failure is a programming error and is returned as such.
Result<ir::Gate> gate(std::string_view name, std::vector<ir::Wire> targets,
                      std::vector<double> params = {}, const SourceSpan& span = {});

// Spec 14 §1 PassMetrics of a circuit. `swapCount` and `estimatedDuration` are pipeline state and
// are filled by the pass manager.
PassMetrics measureCircuit(const ir::Circuit& c);

// Wires touched by any node of the circuit, nested bodies included (ascending).
std::vector<std::uint32_t> usedWires(const ir::Circuit& c);
// True when some gate (nested bodies included) is defined only by a defcal and has no matrix.
bool hasOpaqueGate(const ir::Circuit& c);

// A list of indices stored in circuit metadata ("final_layout"); nullopt when the key is absent
// or the value is not an array of unsigned integers (metadata may come from an imported file, and
// no exception may leave the module).
std::optional<std::vector<std::uint32_t>> metaIndices(const ir::Circuit& c, std::string_view key);

// Angle reduced to (−π, π]. `turns`, when given, receives k with a = wrapped + 2πk.
double wrapAngle(double a, long* turns = nullptr);

} // namespace qlab::compiler
