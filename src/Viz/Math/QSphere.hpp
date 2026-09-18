#pragma once
// Spec 21 §3.4 — Q-sphere node placement. Basis state |i⟩ of n qubits sits at the latitude of its
// Hamming weight w(i), z = 1 − 2w/n (|0…0⟩ at the north pole, |1…1⟩ at the south pole), and at a
// longitude spaced uniformly among the C(n, w) states of equal weight, ordered by index.
// Node radius ∝ |a_i| so that the node's area is ∝ the probability; colour = phase hue (§2.4).
#include "Viz/Math/Amplitudes.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <span>
#include <vector>

namespace qlab::viz::math {

inline constexpr std::uint32_t kQSphereMaxQubits = 12; // 4096 nodes; above it only the top-k are shown

// C(n, k) (exact for n ≤ 62).
std::uint64_t binomial(std::uint32_t n, std::uint32_t k);
// Position of `index` among the basis states with the same Hamming weight, ordered by index
// (combinatorial number system): 0 … C(n, w) − 1.
std::uint64_t rankAmongEqualWeight(std::uint64_t index);

struct QSpherePlacement {
    std::uint32_t weight = 0;
    std::uint64_t rank = 0, count = 1;   // rank among the `count` states of this weight
    double z = 1.0;                      // 1 − 2w/n
    double longitude = 0.0;              // 2π·rank/count ∈ [0, 2π)
    glm::dvec3 position{0.0, 0.0, 1.0};  // on the unit sphere, quantum frame (z up)
};
QSpherePlacement qspherePlacement(std::uint64_t index, std::uint32_t nQubits);

struct QSphereNode {
    BasisEntry state;
    QSpherePlacement place;
    double radius = 0.0;                 // in sphere radii: maxRadius · |a_i|
    glm::vec3 color{0.0f};               // display colour of the phase
    bool spoke = false;                  // line from the centre (|a_i|² > ε)
};

struct QSphereOptions {
    double epsilon = kDefaultAmplitudeThreshold; // spokes and the top-k cut use |a_i|² > ε
    std::size_t topK = kDefaultTopK;
    double maxRadius = 0.16;                     // node radius for |a_i| = 1, in sphere radii
};

struct QSphereModel {
    std::uint32_t nQubits = 0;
    std::vector<QSphereNode> nodes;      // ascending index
    bool topKOnly = false;               // n > 12: only the top-k nodes are shown, and the view says so
    double shownProbability = 0.0;
    std::size_t totalStates = 0;
};
// n ≤ 12: one node per basis state with non-zero amplitude. n > 12: the top-k by probability.
QSphereModel buildQSphere(std::span<const Complex> psi, const QSphereOptions& options = {});
// The same from a precomputed selection (the run supplies it above 20 qubits).
QSphereModel buildQSphere(const AmplitudeSelection& selection, const QSphereOptions& options = {});

} // namespace qlab::viz::math
