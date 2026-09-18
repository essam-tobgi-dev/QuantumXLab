#pragma once
// Spec 14 §7 — initial layout: trivial, dense subgraph, VF2 exact embedding, noise-aware search.
// Program qubits are placed on DATA qubits only.
#include "Compiler/Coupling.hpp"
#include "Compiler/Types.hpp"
#include "IR/Circuit.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace qlab::compiler {

// The program's interaction multigraph I: one vertex per program qubit, edge weight = number of
// two-qubit gates on the pair (bodies of Branch/Loop/Box included).
struct InteractionGraph {
    struct Edge { std::uint32_t a, b, weight; };                 // a < b
    std::uint32_t qubits = 0;
    std::vector<Edge> edges;                                     // sorted by (a, b)
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> adj;   // per qubit: (neighbour, weight)
    std::vector<std::uint8_t> used;                              // touched by any node
    std::vector<std::uint8_t> measured;
    std::uint32_t depth = 0;                                     // `Circuit::depth()`, for the T1 term
    std::size_t degree(std::uint32_t v) const { return adj[v].size(); }
};
InteractionGraph interactionGraph(const ir::Circuit& c);

// v_i → the i-th data qubit.
Result<Layout> trivialLayout(std::uint32_t programQubits, const CouplingGraph& g);
// The connected k-subgraph with the most internal edges (greedy expansion from the best-connected
// qubit, 20 seeded restarts), mapped by BFS order against the interaction graph.
Result<Layout> denseLayout(const InteractionGraph& ig, const CouplingGraph& g, std::uint64_t seed);

struct Vf2Result {
    std::optional<Layout> layout;     // best embedding found (by `layoutScore`), if any
    std::uint64_t states = 0;         // candidate pairs tried
    std::uint32_t embeddings = 0;     // complete embeddings seen
    bool budgetExceeded = false;      // QL4060 when no embedding was found either
};
// Exact subgraph isomorphism (VF2) of the interaction graph into the coupling graph: every
// interacting pair lands on a coupled pair, so routing inserts no swap.
Vf2Result vf2Layout(const InteractionGraph& ig, const CouplingGraph& g, std::uint64_t stateBudget = 1'000'000,
                    std::uint32_t maxEmbeddings = 256);

// Spec 14 §7 score (lower is better):
//   Σ_{(a,b)∈I} w_ab·c(π a, π b) + Σ_{a measured} −ln F_ro(π a) + Σ_{a used} t_circ / T1(π a)
// with c = −ln F_2q on an edge and 3× the most reliable path cost otherwise (one swap ≈ 3 cx per
// hop). Without calibration every edge costs 1, which makes the score a swap-distance estimate.
double layoutScore(const Layout& l, const InteractionGraph& ig, const CouplingGraph& g);
// Hill climbing over exchanges of two assignments (a program qubit with a neighbouring qubit or
// with another program qubit): at most `iterations` improving steps.
Layout refineLayout(Layout start, const InteractionGraph& ig, const CouplingGraph& g, std::uint64_t seed,
                    std::uint32_t iterations = 200);

struct LayoutChoice {
    Layout layout;
    LayoutPolicy policy = LayoutPolicy::Trivial;   // what produced it (Dense when VF2 fell back)
    double score = 0.0;
    Vf2Result vf2;                                  // filled for Vf2 / NoiseAware
};
// Applies a policy. A VF2 search that runs out of budget without an embedding falls back to the
// dense layout and reports QL4060 through `diagnostics`.
Result<LayoutChoice> chooseLayout(const ir::Circuit& c, const CouplingGraph& g, LayoutPolicy policy, std::uint64_t seed,
                                  std::uint64_t vf2StateBudget, std::vector<lang::Diagnostic>* diagnostics);

// A layout is valid when it is injective and maps every program qubit to a data qubit.
Status validateLayout(const Layout& l, std::uint32_t programQubits, const CouplingGraph& g);

} // namespace qlab::compiler
