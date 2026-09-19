// Spec 21 §3.4 — Q-sphere placement (see QSphere.hpp).
#include "Viz/Math/QSphere.hpp"
#include "Viz/Math/Phase.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace qlab::viz::math {

std::uint64_t binomial(std::uint32_t n, std::uint32_t k) {
    if (k > n)
        return 0;
    k = std::min(k, n - k);
    std::uint64_t c = 1;
    for (std::uint32_t i = 1; i <= k; ++i)
        c = c * (n - k + i) / i; // exact: c·(n−k+i) is divisible by i
    return c;
}

std::uint64_t rankAmongEqualWeight(std::uint64_t index) {
    // Numbers with the same popcount are ordered like their bit-position sets in colexicographic
    // order, whose rank is Σ_k C(p_k, k) over the set bits p_1 < p_2 < … (k counted from 1).
    std::uint64_t rank = 0;
    std::uint32_t k = 0;
    for (std::uint32_t pos = 0; pos < 64; ++pos)
        if ((index >> pos) & 1u)
            rank += binomial(pos, ++k);
    return rank;
}

QSpherePlacement qspherePlacement(std::uint64_t index, std::uint32_t n) {
    QSpherePlacement p;
    if (n == 0)
        return p;
    p.weight = static_cast<std::uint32_t>(std::popcount(index));
    p.count = binomial(n, p.weight);
    p.rank = rankAmongEqualWeight(index);
    p.z = 1.0 - 2.0 * static_cast<double>(p.weight) / static_cast<double>(n);
    p.longitude = p.count > 0 ? 2.0 * std::numbers::pi * static_cast<double>(p.rank) /
                                    static_cast<double>(p.count)
                              : 0.0;
    const double s = std::sqrt(std::max(0.0, 1.0 - p.z * p.z));
    p.position = glm::dvec3(s * std::cos(p.longitude), s * std::sin(p.longitude), p.z);
    return p;
}

QSphereModel buildQSphere(const AmplitudeSelection& selection, const QSphereOptions& options) {
    QSphereModel m;
    m.nQubits = selection.nQubits;
    m.totalStates = selection.totalStates;
    m.topKOnly = selection.nQubits > kQSphereMaxQubits;
    m.shownProbability = selection.shownProbability;
    m.nodes.reserve(selection.entries.size());
    for (const BasisEntry& e : selection.entries) {
        QSphereNode node;
        node.state = e;
        node.place = qspherePlacement(e.index, selection.nQubits);
        node.radius = options.maxRadius * std::sqrt(e.probability);
        node.color = phaseColor(e.phase);
        node.spoke = e.probability > options.epsilon;
        m.nodes.push_back(node);
    }
    std::sort(m.nodes.begin(), m.nodes.end(), [](const QSphereNode& a, const QSphereNode& b) {
        return a.state.index < b.state.index;
    });
    return m;
}

QSphereModel buildQSphere(std::span<const Complex> psi, const QSphereOptions& options) {
    AmplitudeFilter f;
    f.order = AmplitudeOrder::ByIndex;
    const std::uint32_t n =
        psi.empty() ? 0u : static_cast<std::uint32_t>(std::bit_width(psi.size()) - 1);
    if (n > kQSphereMaxQubits) {
        f.threshold = options.epsilon; // spec 21 §3.4: the top-k nodes only
        f.maxEntries = options.topK;
    } else {
        f.threshold = 1e-24; // every state with an amplitude (a zero node has no area)
        f.maxEntries = 0;
    }
    return buildQSphere(selectAmplitudes(psi, f), options);
}

} // namespace qlab::viz::math
