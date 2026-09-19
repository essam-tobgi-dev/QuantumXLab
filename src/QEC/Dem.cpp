// Spec 16 §5.1 — detector error model: every elementary fault of a noise plan is propagated through
// the schedule (Pauli frame, linear in the fault's X/Z components) and filed under the detectors it
// fires. Graph-like classes become edges; larger ones are decomposed into existing edges.
#include "QEC/Graph.hpp"
#include "QEC/Sampler.hpp"
#include <algorithm>
#include <iterator>
#include <map>
#include <unordered_map>

namespace qlab::qec {
namespace {

// What a fault does to the record: detectors fired, logical readouts flipped, data readouts
// flipped.
struct Signature {
    std::vector<std::uint32_t> detectors, dataFlips; // sorted
    std::uint64_t observables = 0;
};

std::vector<std::uint32_t> symmetricDifference(const std::vector<std::uint32_t>& a,
                                               const std::vector<std::uint32_t>& b) {
    std::vector<std::uint32_t> out;
    std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    return out;
}

void combine(Signature& into, const Signature& other) {
    into.detectors = symmetricDifference(into.detectors, other.detectors);
    into.dataFlips = symmetricDifference(into.dataFlips, other.dataFlips);
    into.observables ^= other.observables;
}

double xorProbability(double p, double q) {
    return p * (1.0 - q) + q * (1.0 - p);
}

struct EdgeClass {
    double probability = 0.0;
    // Probability mass and representative data flips per logical action.
    std::map<std::uint64_t, std::pair<double, std::vector<std::uint32_t>>> actions;
};
using EdgeKey = std::pair<std::uint32_t, std::uint32_t>;

class ModelBuilder {
  public:
    ModelBuilder(const MemoryExperiment& ex, const NoisePlan& noise)
        : ex_(ex), noise_(noise), frame_(ex.schedule) {
        bitDetectors_.assign(ex.schedule.bits, {});
        bitObservables_.assign(ex.schedule.bits, 0);
        for (std::uint32_t d = 0; d < ex.detectors.size(); ++d)
            for (std::uint32_t b : ex.detectors[d].bits)
                bitDetectors_[b].push_back(d);
        for (std::uint32_t l = 0; l < ex.observables.size() && l < 64; ++l)
            for (std::uint32_t b : ex.observables[l])
                bitObservables_[b] ^= std::uint64_t{1} << l;
        // The logical action travels with the checks the transversal readout closes; X and Z
        // components separate only when every check is X- or Z-type and the readout is uniform.
        const bool allZ =
            std::all_of(ex.dataBasis.begin(), ex.dataBasis.end(), [](char c) { return c == 'Z'; });
        const bool allX =
            std::all_of(ex.dataBasis.begin(), ex.dataBasis.end(), [](char c) { return c == 'X'; });
        readoutType_ = allZ ? CheckType::Z : (allX ? CheckType::X : CheckType::Mixed);
        split_ = readoutType_ != CheckType::Mixed &&
                 std::none_of(ex.detectors.begin(), ex.detectors.end(),
                              [](const Detector& d) { return d.type == CheckType::Mixed; });
    }

    DecodingGraph build() {
        DecodingGraph g;
        g.nData = ex_.nData;
        g.detectors = static_cast<std::uint32_t>(ex_.detectors.size());
        std::vector<std::pair<Signature, double>> hyper;
        std::uint32_t site = kNoIndex;
        for (const FaultEvent& f : enumerateFaults(noise_)) {
            if (f.site != site)
                mergeSite();
            site = f.site;
            ++g.faults;
            const Signature s = signatureOf(f);
            for (Signature& part : partsOf(s)) {
                if (part.detectors.empty()) {
                    if (part.observables != 0)
                        g.undetectedLogicalProbability =
                            xorProbability(g.undetectedLogicalProbability, f.probability);
                } else if (part.detectors.size() <= 2) {
                    file(part, f.probability);
                } else {
                    hyper.emplace_back(std::move(part), f.probability);
                }
            }
        }
        mergeSite();
        for (const auto& [part, p] : hyper)
            decompose(part, p, g);
        for (auto& [key, cls] : classes_) {
            GraphEdge e;
            e.a = key.first;
            e.b = key.second;
            e.probability = cls.probability;
            const auto best = std::max_element(
                cls.actions.begin(), cls.actions.end(),
                [](const auto& l, const auto& r) { return l.second.first < r.second.first; });
            if (best != cls.actions.end()) {
                e.observableMask = best->first;
                for (std::uint32_t q : best->second.second)
                    e.correction.emplace_back(q, ex_.dataBasis[q] == 'X' ? 'Z' : 'X');
            }
            if (cls.actions.size() > 1)
                ++g.ambiguousEdges;
            g.edges.push_back(std::move(e));
        }
        const bool exact =
            g.hyperedgesDecomposed == 0 && g.hyperedgesDropped == 0 && g.ambiguousEdges == 0;
        g.cls = exact ? FidelityClass::Exact : FidelityClass::Model; // spec 16 §5.1 note
        g.finalize(false);
        return g;
    }

  private:
    Signature ofBits(const std::vector<std::uint32_t>& bits) const {
        Signature s;
        std::vector<std::uint32_t> detectors;
        for (std::uint32_t b : bits) {
            detectors.insert(detectors.end(), bitDetectors_[b].begin(), bitDetectors_[b].end());
            s.observables ^= bitObservables_[b];
            if (ex_.options.finalDataMeasurement && b >= ex_.dataBit(0))
                s.dataFlips.push_back(b - ex_.dataBit(0));
        }
        std::sort(detectors.begin(), detectors.end());
        for (std::size_t i = 0; i < detectors.size();) { // keep odd multiplicities
            std::size_t j = i;
            while (j < detectors.size() && detectors[j] == detectors[i])
                ++j;
            if ((j - i) % 2 == 1)
                s.detectors.push_back(detectors[i]);
            i = j;
        }
        std::sort(s.dataFlips.begin(), s.dataFlips.end());
        return s;
    }

    // Signature of a single X or Z after an operation, cached: two-qubit and Y faults are XORs.
    const Signature& elementary(std::uint32_t afterOp, std::uint32_t qubit, bool zLetter) {
        const std::uint64_t key =
            (std::uint64_t(afterOp) << 32) | (std::uint64_t(qubit) << 1) | (zLetter ? 1u : 0u);
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            frame_.propagate(afterOp, qubit, zLetter ? 'Z' : 'X', scratch_);
            it = cache_.emplace(key, ofBits(scratch_)).first;
        }
        return it->second;
    }

    Signature signatureOf(const FaultEvent& f) {
        Signature s;
        auto add = [&](std::uint32_t q, char letter) {
            if (q == kNoIndex)
                return;
            if (letter == 'X' || letter == 'Y')
                combine(s, elementary(f.afterOp, q, false));
            if (letter == 'Z' || letter == 'Y')
                combine(s, elementary(f.afterOp, q, true));
        };
        add(f.qubitA, f.pauliA);
        add(f.qubitB, f.pauliB);
        if (f.flipBit != kNoIndex)
            combine(s, ofBits({f.flipBit}));
        return s;
    }

    std::vector<Signature> partsOf(const Signature& s) const {
        if (!split_)
            return {s};
        Signature x, z;
        for (std::uint32_t d : s.detectors)
            (ex_.detectors[d].type == CheckType::X ? x : z).detectors.push_back(d);
        Signature& carrier = readoutType_ == CheckType::X ? x : z;
        carrier.observables = s.observables;
        carrier.dataFlips = s.dataFlips;
        return {std::move(x), std::move(z)};
    }

    // The faults of one noise site exclude each other, so within a site the probabilities of a
    // class add; different sites are independent and combine as "an odd number occurred".
    void file(const Signature& part, double p) {
        const EdgeKey key{part.detectors[0],
                          part.detectors.size() == 2 ? part.detectors[1] : kBoundary};
        EdgeClass& cls = site_[key];
        cls.probability += p;
        auto& action = cls.actions[part.observables];
        if (action.first == 0.0)
            action.second = part.dataFlips;
        action.first += p;
    }

    void mergeSite() {
        for (auto& [key, local] : site_) {
            EdgeClass& cls = classes_[key];
            cls.probability = xorProbability(cls.probability, local.probability);
            for (auto& [mask, action] : local.actions) {
                auto& total = cls.actions[mask];
                if (total.first == 0.0)
                    total.second = std::move(action.second);
                total.first += action.first;
            }
        }
        site_.clear();
    }

    // Partition the fired detectors into existing edges (pairs, or singles with a boundary edge),
    // preferring a partition with the hyperedge's own logical action (spec 16 §5.1).
    bool partition(std::vector<std::uint32_t> rest, std::uint64_t wantAction,
                   std::vector<EdgeKey>& out, bool exactAction) {
        if (rest.empty())
            return !exactAction || wantAction == 0;
        const std::uint32_t first = rest.front();
        rest.erase(rest.begin());
        for (std::size_t i = 0; i <= rest.size(); ++i) {
            const EdgeKey key{first, i < rest.size() ? rest[i] : kBoundary};
            const auto it = classes_.find(key);
            if (it == classes_.end())
                continue;
            std::vector<std::uint32_t> remaining = rest;
            if (i < rest.size())
                remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            const std::uint64_t action =
                it->second.actions.empty() ? 0 : dominantAction(it->second);
            out.push_back(key);
            if (partition(std::move(remaining), wantAction ^ action, out, exactAction))
                return true;
            out.pop_back();
        }
        return false;
    }

    static std::uint64_t dominantAction(const EdgeClass& cls) {
        return std::max_element(
                   cls.actions.begin(), cls.actions.end(),
                   [](const auto& l, const auto& r) { return l.second.first < r.second.first; })
            ->first;
    }

    void decompose(const Signature& part, double p, DecodingGraph& g) {
        std::vector<EdgeKey> components;
        if (!partition(part.detectors, part.observables, components, true)) {
            components.clear();
            if (!partition(part.detectors, part.observables, components, false)) {
                ++g.hyperedgesDropped;
                return;
            }
        }
        ++g.hyperedgesDecomposed;
        for (const EdgeKey& key : components)
            classes_[key].probability = xorProbability(classes_[key].probability, p);
    }

    const MemoryExperiment& ex_;
    const NoisePlan& noise_;
    FrameSampler frame_;
    std::vector<std::vector<std::uint32_t>> bitDetectors_;
    std::vector<std::uint64_t> bitObservables_;
    CheckType readoutType_ = CheckType::Z;
    bool split_ = false;
    std::unordered_map<std::uint64_t, Signature> cache_;
    std::map<EdgeKey, EdgeClass> classes_, site_;
    std::vector<std::uint32_t> scratch_;
};

} // namespace

Result<DecodingGraph> buildDecodingGraph(const MemoryExperiment& experiment,
                                         const NoisePlan& noise) {
    if (experiment.detectors.empty())
        return fail(err::BadOptions, "the experiment defines no detector to decode");
    for (const NoiseSite& s : noise.sites)
        if (s.afterOp >= experiment.schedule.ops.size())
            return fail(err::BadOptions, "the noise plan was made for a different schedule");
    return ModelBuilder(experiment, noise).build();
}

} // namespace qlab::qec
