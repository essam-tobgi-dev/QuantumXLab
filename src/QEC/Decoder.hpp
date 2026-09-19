#pragma once
// Spec 16 §5, T09 §6 — decoders: the common interface, the lookup table for small codes and the
// union-find decoder (Delfosse–Nickerson), which is the default.
#include "QEC/Detail.hpp"
#include "QEC/Graph.hpp"
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace qlab::qec {

// Detection events handed to a decoder: events[i] belongs to detector i of the decoder's own
// detector list — generator i for the lookup table and the code-capacity graph, detector i of the
// `MemoryExperiment` for a graph built by `buildDecodingGraph`.
struct SyndromeLattice {
    std::vector<std::uint8_t> events;
    static SyndromeLattice fromSyndrome(std::span<const std::uint8_t> syndrome);
    // Detection events of one measurement record (or of its flips) of the experiment.
    static SyndromeLattice fromRecord(const MemoryExperiment& experiment,
                                      std::span<const std::uint8_t> bits);
    // Net syndrome per generator: XOR of a generator's events over all layers. It equals the final
    // syndrome when measurements are perfect, which is what a lookup table can decode.
    static SyndromeLattice foldLayers(const MemoryExperiment& experiment,
                                      std::span<const std::uint8_t> events);
};

struct MatchedEdge {
    std::uint32_t a = 0, b = kBoundary; // detectors joined by the decoder; b = kBoundary
};

struct Correction {
    PauliString pauli;                // on the data qubits
    std::uint64_t observableMask = 0; // logical readouts the selected edges flip (graph decoders)
    std::vector<MatchedEdge> edges;   // matched pairs for the lattice views (spec 16 §8)
};

class IDecoder {
  public:
    virtual ~IDecoder() = default;
    virtual Result<Correction> decode(const SyndromeLattice& lattice) = 0;
    virtual std::string_view name() const = 0;
    virtual std::unique_ptr<IDecoder> clone() const = 0; // one instance per worker thread
};

// T09 §6.1: minimum-weight correction for every syndrome, O(1) per decode. A CSS code gets one
// table per check type (X errors from the Z checks, Z errors from the X checks: 2^{m_Z} + 2^{m_X}
// entries instead of 2^{n−k}); any other code one joint table over all 3^w letter assignments. Ties
// go to the first error in enumeration order (increasing weight, then increasing support as an
// integer).
class LookupDecoder final : public IDecoder {
  public:
    // Fails with err::TooLarge beyond 62 qubits or 20 generators per table.
    static Result<LookupDecoder> create(const StabilizerCode& code);
    Result<Correction> decode(const SyndromeLattice& lattice) override; // one event per generator
    std::string_view name() const override { return "lookup"; }
    std::unique_ptr<IDecoder> clone() const override {
        return std::make_unique<LookupDecoder>(*this);
    }
    std::size_t tableEntries() const;
    std::uint32_t maxCorrectionWeight() const { return maxWeight_; }

  private:
    struct Table {
        std::vector<std::uint32_t> generators; // generator indices forming the table index bits
        std::vector<detail::Mask> entries;
    };
    std::uint32_t n_ = 0, checks_ = 0, maxWeight_ = 0;
    std::vector<Table> tables_;
};

struct UnionFindOptions {
    // Spec 16 §5.2 weighted growth: an edge of weight w is 2L half-steps long, with
    // L = round(resolution · w / w_min) ≥ 1 (lengths are then divided by their common factor, so a
    // unit-weight graph grows one edge per two half-steps). false grows every edge as length 1.
    bool weighted = true;
    // resolution = 1 is "integer units of the minimum weight": every ratio in [1, 1.5) becomes 1
    // and [1.5, 2.5) becomes 2. Measured on surface_rot_5, circuit level, p = 1e-3, 2·10^5 shots:
    // p_L = 3.6e-4 at resolution 1, 1.2e-4 unweighted, 4.5e-5 at resolution 4 (no gain beyond).
    std::uint32_t resolution = 4;
};

// Spec 16 §5.2, T09 §6.3: clusters grow around detection events by half-edges, merge on contact,
// stop when even or touching the boundary; a spanning forest of each cluster (seeded at the
// boundary when the cluster is odd, inside it when even) is then peeled from the leaves to give
// the correction. Every returned correction reproduces the detection events it was given.
class UnionFindDecoder final : public IDecoder {
  public:
    explicit UnionFindDecoder(DecodingGraph graph, UnionFindOptions options = {});
    Result<Correction> decode(const SyndromeLattice& lattice) override;
    std::string_view name() const override { return "union_find"; }
    std::unique_ptr<IDecoder> clone() const override {
        return std::make_unique<UnionFindDecoder>(*this);
    }
    const DecodingGraph& graph() const { return graph_; }
    std::uint32_t lastGrowthRounds() const { return growthRounds_; }

  private:
    std::uint32_t find(std::uint32_t v);
    void join(std::uint32_t u, std::uint32_t w);
    void enter(std::uint32_t v);
    Status grow();
    Status peel(Correction& out);
    void reset();
    std::uint32_t farEnd(std::uint32_t edge, std::uint32_t from) const;

    DecodingGraph graph_;
    UnionFindOptions options_;
    std::uint32_t real_ = 0; // detectors; vertex real_ + i is the far end of boundary edge i
    std::vector<std::uint32_t>
        virtualOf_; // edge → its virtual boundary vertex (kNoIndex for inner edges)
    std::vector<std::uint32_t> virtualEdge_;       // virtual vertex − real_ → its boundary edge
    std::vector<std::uint32_t> capacity_, growth_; // per edge, in half-steps
    std::vector<std::uint32_t> parent_, size_;
    std::vector<std::uint8_t> inCluster_, fired_, odd_, atBoundary_, visited_;
    std::vector<std::vector<std::uint32_t>> frontier_;
    std::vector<std::uint32_t> active_, firedList_, touchedVertices_, touchedEdges_, newlyFull_,
        keep_;
    std::vector<std::uint32_t> treeEdge_, treeParent_, order_;
    std::uint32_t growthRounds_ = 0;
};

// "lookup" → LookupDecoder; "union_find" → UnionFindDecoder on the code-capacity graph of the code.
Result<std::unique_ptr<IDecoder>> makeCodeCapacityDecoder(std::string_view name,
                                                          const StabilizerCode& code);

} // namespace qlab::qec
