#pragma once
// Spec 14 §8 — SABRE internals: the dependency DAG of one circuit level and the swap search.
#include "Compiler/Coupling.hpp"
#include "Compiler/Route.hpp"
#include "Core/Random.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace qlab::compiler::detail {

// One node of a circuit level. Edges follow the wire order and the classical-bit order (any two
// nodes touching the same bit stay ordered), as in spec 14 §3.
struct DagNode {
    std::vector<std::uint32_t> wires;     // program qubits; a barrier/delay without operands lists none
    bool twoQubit = false;                // a gate on exactly two wires: needs a coupled pair
    std::vector<std::uint32_t> succ, pred;
};
struct Dag {
    std::vector<DagNode> nodes;           // index = position in the circuit's topological order
};
Dag buildDag(const ir::Circuit& c);

// Full bijection logical ↔ physical over all device qubits. Logical indices below the program's
// qubit count are program qubits; the rest are the unused device qubits in ascending order.
struct FullLayout {
    std::vector<std::uint32_t> l2p, p2l;
    static FullLayout from(const Layout& program, const CouplingGraph& g);
    Layout program(std::uint32_t programQubits) const;
    void swapPhysical(std::uint32_t p, std::uint32_t q);
};

class Sabre {
public:
    using OnNode = std::function<Status(std::uint32_t dagIndex, const FullLayout&)>;
    using OnSwap = std::function<Status(std::uint32_t p, std::uint32_t q, std::uint32_t enabledDagIndex)>;

    Sabre(const CouplingGraph& g, const RouteOptions& o, std::uint64_t seed);
    // Runs over the DAG (its `pred` edges when `reversed`) from `layout`, which it updates.
    // Callbacks may be empty (layout-refinement passes). Returns the number of swaps.
    Result<std::uint32_t> run(const Dag& dag, bool reversed, FullLayout& layout, const OnNode& onNode,
                              const OnSwap& onSwap, std::stop_token stop);

private:
    double dist(std::uint32_t p, std::uint32_t q) const { return dist_[p * n_ + q]; }

    const CouplingGraph& g_;
    RouteOptions o_;
    core::Random rng_;
    std::uint32_t n_;
    std::vector<double> dist_;
};

} // namespace qlab::compiler::detail
