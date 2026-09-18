#pragma once
// Spec 14 §5 — the optimizer's working set: the node list of one (sub)circuit with a flat per-wire
// index, cached wire actions (Commutation.hpp) and dirty-wire tracking, so that sweeps walk wires
// without touching `ir::Circuit`, without allocating, and revisit only what changed. Internal.
#include "Compiler/Euler.hpp"
#include "IR/Circuit.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace qlab::compiler::detail {

struct WorkList {
    static constexpr std::uint32_t npos = 0xFFFFFFFFu;

    std::vector<ir::Node> nodes;                          // topological order
    std::vector<std::uint8_t> alive;
    std::vector<std::vector<ir::Node>> after;             // spliced in right after node i on flush
    std::vector<std::vector<std::uint32_t>> wireSeq;      // per wire: node indices in order
    // A rewrite depends only on the nodes of the wires it touches, so a sweep examines a node only
    // when one of its wires is `dirty`, and records the wires it changes in `touched`.
    std::vector<std::uint8_t> dirty, touched;
    std::uint32_t qubits = 0;

    WorkList(std::vector<ir::Node> list, std::uint32_t qubitCount);
    // Live nodes with the splices applied, in order. The work list is spent afterwards.
    std::vector<ir::Node> flush();

    std::span<const std::uint32_t> wiresOf(std::uint32_t i) const { return {wireFlat_.data() + begin_[i], begin_[i + 1] - begin_[i]}; }
    const ir::Gate* gateAt(std::uint32_t i) const { return alive[i] ? std::get_if<ir::Gate>(&nodes[i]) : nullptr; }
    ir::Gate* gateAt(std::uint32_t i) { return alive[i] ? std::get_if<ir::Gate>(&nodes[i]) : nullptr; }
    // Index of `wire` among the wires of node i, or npos.
    std::uint32_t slotOf(std::uint32_t i, std::uint32_t wire) const;
    // Previous live node on wire slot `slot` of node i; npos at the start of the wire and at a
    // splice point (spliced nodes are not indexed until the next pass: the wire is opaque there).
    std::uint32_t previousOn(std::uint32_t i, std::uint32_t slot) const;
    // Wire action of gate i on its slot (kActsZ / kActsX mask; 0 for anything that is not a gate).
    std::uint8_t actionOf(std::uint32_t i, std::uint32_t slot) const { return action_[begin_[i] + slot]; }
    // The wire rule of Commutation.hpp on cached actions.
    bool commutes(std::uint32_t i, std::uint32_t j) const;
    bool anyDirty(std::uint32_t i) const;
    void touch(std::uint32_t i);                          // marks the wires of node i as changed
    void kill(std::uint32_t i) { touch(i); alive[i] = 0; }

private:
    std::vector<std::uint32_t> begin_, wireFlat_, position_;
    std::vector<std::uint8_t> action_;
};

// A single-qubit gate the fusion and merge sweeps may rewrite: a plain library or matrix gate on
// one wire, no controls, not defcal-only.
bool isFusable(const ir::Gate& g);

// Cost of a run of single-qubit gates (spec 14 §5.2): pulses first, then gate count.
struct RunCost {
    std::size_t pulses = 0, gates = 0;
    bool operator<(const RunCost& o) const { return pulses != o.pulses ? pulses < o.pulses : gates < o.gates; }
};

struct MergeRules {
    Basis1q basis = Basis1q::U;
    bool lookThrough = true;          // §5.3: look past gates that commute with the candidate
    // Allow rewrites that splice new nodes (two X-type gates → one resynthesised gate). They hide
    // the wire from the rest of the pass, so they run only after the in-place rewrites settled.
    bool resynthesize = false;
    double maxEntanglerAngle = 0.0;   // largest |θ| of a merged two-qubit rotation; 0 = unbounded
};
// §5.1 + §5.3 + §5.6: removes inverse pairs and merges same-axis rotations, looking through gates
// that commute with the candidate. Returns true when something changed.
bool sweepCancelMerge(WorkList& w, const MergeRules& rules);
// §5.2: maximal runs of single-qubit gates → one resynthesised sequence when that is cheaper.
bool sweepFuse(WorkList& w, Basis1q basis);
// §5.3: moves a Z-type (X-type) single-qubit gate left through the gates it commutes with when
// the node before them is a single-qubit gate, so that the next fusion sweep sees one run.
bool sweepCommute(WorkList& w);

// How far back a sweep looks through commuting gates.
inline constexpr std::uint32_t kCommuteWindow = 64;

} // namespace qlab::compiler::detail
