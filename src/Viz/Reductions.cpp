// Spec 21 §1 — reduction requests, lookups and the ViewInput helpers (see Reductions.hpp).
#include "Viz/Reductions.hpp"
#include "Viz/ViewInput.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
namespace {
template <class T> void appendUnique(std::vector<T>& into, const std::vector<T>& from) {
    for (const T& v : from)
        if (std::find(into.begin(), into.end(), v) == into.end())
            into.push_back(v);
}
} // namespace

bool ReductionRequest::empty() const {
    return !singles && !pairs && subsets.empty() && !schmidtPartition && !singleQubitPaulis &&
           pauliStrings.empty() && topAmplitudes == 0 && !wigner;
}

void ReductionRequest::merge(const ReductionRequest& o) {
    // A restriction to some qubits only survives when both sides restrict; "all" absorbs a subset.
    const bool hadWork = singles || pairs, otherHasWork = o.singles || o.pairs;
    if (hadWork && otherHasWork) {
        if (qubits.empty() || o.qubits.empty())
            qubits.clear();
        else
            appendUnique(qubits, o.qubits);
    } else if (otherHasWork) {
        qubits = o.qubits;
    }
    singles = singles || o.singles;
    pairs = pairs || o.pairs;
    appendUnique(subsets, o.subsets);
    if (!schmidtPartition)
        schmidtPartition = o.schmidtPartition; // one bipartition per snapshot: first asker wins
    singleQubitPaulis = singleQubitPaulis || o.singleQubitPaulis;
    appendUnique(pauliStrings, o.pauliStrings);
    topAmplitudes = std::max(topAmplitudes, o.topAmplitudes);
    if (!wigner && o.wigner) {
        wigner = o.wigner;
        modeState = o.modeState;
    }
}

const SingleReduction* Reductions::single(QubitIndex q) const {
    for (const auto& s : singles)
        if (s.qubit == q)
            return &s;
    return nullptr;
}

const PairReduction* Reductions::pair(QubitIndex a, QubitIndex b) const {
    const QubitIndex lo = std::min(a, b), hi = std::max(a, b);
    for (const auto& p : pairs)
        if (p.i == lo && p.j == hi)
            return &p;
    return nullptr;
}

const qsim::ReducedState* Reductions::subset(std::span<const QubitIndex> qubits) const {
    for (const auto& s : subsets)
        if (s.qubits.size() == qubits.size() &&
            std::equal(s.qubits.begin(), s.qubits.end(), qubits.begin()))
            return &s;
    return nullptr;
}

const PauliValue* Reductions::pauli(std::string_view fullWidthLabel) const {
    for (const auto& p : paulis)
        if (p.pauli == fullWidthLabel)
            return &p;
    return nullptr;
}

// ---------------------------------------------------------------- ViewInput helpers

std::shared_ptr<const ir::Circuit> CircuitSet::latest() const {
    for (std::size_t k = stages.size(); k-- > 0;)
        if (stages[k])
            return stages[k];
    return nullptr;
}

bool ViewInput::reductionsStale() const {
    if (!snapshot || !reductions)
        return false;
    return reductions->gateIndex != snapshot->gateIndex ||
           reductions->simTimePs != snapshot->simTimePs;
}

std::uint32_t ViewInput::qubitCount() const {
    if (snapshot && snapshot->nQubits > 0)
        return snapshot->nQubits;
    if (device)
        return static_cast<std::uint32_t>(device->qubitCount());
    if (const auto c = circuits.latest())
        return c->qubitCount();
    return 0;
}

TrajectoryEnsemble ensembleFromAverages(std::span<const qsim::AveragedSample> samples,
                                        std::uint32_t site, std::uint32_t levels,
                                        std::uint64_t trajectories) {
    TrajectoryEnsemble e;
    e.qubit = site;
    e.trajectories = trajectories;
    for (const auto& s : samples) {
        const std::size_t base = static_cast<std::size_t>(site) * levels;
        if (base + 1 >= s.populations.size())
            continue;
        e.timeS.push_back(s.timeS);
        e.meanZ.push_back(s.populations[base] - s.populations[base + 1]); // ⟨Z⟩ = P_0 − P_1
        // P_0 and P_1 are anticorrelated (exactly so for two levels), so their errors add linearly.
        const double s0 = base < s.stderrs.size() ? s.stderrs[base] : 0.0;
        const double s1 = base + 1 < s.stderrs.size() ? s.stderrs[base + 1] : 0.0;
        e.stderrZ.push_back(s0 + s1);
    }
    return e;
}

} // namespace qlab::viz
