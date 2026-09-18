#pragma once
// Seeded random circuits, Kraus sets and comparison helpers shared by the QSim backend tests.
// Every gate is a plain matrix applied through IBackend, so one circuit drives every backend.
#include "Gates.hpp"
#include "Numerics/Tensor.hpp"
#include <string>
#include <vector>

namespace qtest {

struct Op {
    std::string name;
    Matrix u;
    std::vector<QubitIndex> targets;
    std::vector<QubitIndex> controls;
};
using Circuit = std::vector<Op>;

inline Status applyAll(IBackend& b, const Circuit& c) {
    for (const auto& op : c) {
        Status s = op.controls.empty() ? b.applyGate(op.u, op.targets)
                                       : b.applyControlled(op.u, op.controls, op.targets);
        if (!s) return s;
    }
    return {};
}

// Two distinct qubits (and optionally a third), uniformly at random.
inline std::vector<std::uint32_t> distinctQubits(core::Random& rng, std::uint32_t n, std::size_t k) {
    std::vector<std::uint32_t> out;
    while (out.size() < k) {
        auto c = static_cast<std::uint32_t>(rng.uniformInt(n));
        bool dup = false;
        for (auto x : out) dup = dup || x == c;
        if (!dup) out.push_back(c);
    }
    return out;
}

// Clifford circuit over {H, S, Sdg, X, Y, Z, CX, CZ, SWAP} (spec 25 §3.3, Clifford row).
inline Circuit randomClifford(std::uint32_t n, std::size_t gates, std::uint64_t seed) {
    core::Random rng(seed);
    const std::vector<std::pair<std::string, Matrix>> one{
        {"h", H()}, {"s", S()}, {"sdg", Sdg()}, {"x", X()}, {"y", Y()}, {"z", Z()}};
    const std::vector<std::pair<std::string, Matrix>> two{{"cx", CX()}, {"cz", CZ()}, {"swap", SWAP()}};
    Circuit c;
    for (std::size_t g = 0; g < gates; ++g) {
        if (n >= 2 && rng.uniform() < 0.4) {
            auto t = distinctQubits(rng, n, 2);
            const auto& [name, u] = two[rng.uniformInt(two.size())];
            c.push_back({name, u, q({t[0], t[1]}), {}});
        } else {
            const auto& [name, u] = one[rng.uniformInt(one.size())];
            c.push_back({name, u, q({static_cast<std::uint32_t>(rng.uniformInt(n))}), {}});
        }
    }
    return c;
}

// Universal circuit: generic rotations, fast-path gates, controlled gates and a dense 3-qubit gate,
// so that every kernel family of spec 07 §2.2–2.3 is visited.
inline Circuit randomUniversal(std::uint32_t n, std::size_t gates, std::uint64_t seed) {
    core::Random rng(seed);
    Circuit c;
    for (std::size_t g = 0; g < gates; ++g) {
        const double r = rng.uniform();
        const double th = rng.uniform(-3.0, 3.0);
        if (r < 0.45) {
            const std::vector<std::pair<std::string, Matrix>> one{
                {"h", H()}, {"t", T()}, {"sx", SX()}, {"rx", RX(th)}, {"ry", RY(th)}, {"rz", RZ(th)}, {"y", Y()}};
            const auto& [name, u] = one[rng.uniformInt(one.size())];
            c.push_back({name, u, q({static_cast<std::uint32_t>(rng.uniformInt(n))}), {}});
        } else if (r < 0.75) {
            const std::vector<std::pair<std::string, Matrix>> two{{"cx", CX()}, {"cz", CZ()}, {"swap", SWAP()}};
            auto t = distinctQubits(rng, n, 2);
            const auto& [name, u] = two[rng.uniformInt(two.size())];
            c.push_back({name, u, q({t[0], t[1]}), {}});
        } else if (r < 0.9) {
            auto t = distinctQubits(rng, n, 3); // doubly-controlled RY
            c.push_back({"ccry", RY(th), q({t[2]}), q({t[0], t[1]})});
        } else {
            auto t = distinctQubits(rng, n, 3); // dense 8x8: RX ⊗ RY ⊗ RZ, exercises the k-qubit kernel
            std::vector<Matrix> f{RZ(th), RY(0.5 * th), RX(-th)};
            c.push_back({"u3q", num::kronList(f), q({t[0], t[1], t[2]}), {}});
        }
    }
    return c;
}

// Kraus sets (spec 08 §3). Each satisfies Σ K†K = I.
inline Kraus amplitudeDamping(double gamma) {
    return {mat2(1, 0, 0, std::sqrt(1.0 - gamma)), mat2(0, std::sqrt(gamma), 0, 0)};
}
inline Kraus depolarizing(double p) {
    Kraus k{I2(), X(), Y(), Z()};
    k[0] *= std::sqrt(1.0 - p);
    for (std::size_t i = 1; i < 4; ++i) k[i] *= std::sqrt(p / 3.0);
    return k;
}
inline Kraus phaseFlip(double p) {
    Kraus k{I2(), Z()};
    k[0] *= std::sqrt(1.0 - p);
    k[1] *= std::sqrt(p);
    return k;
}

inline double maxAbsDiff(const Matrix& a, const Matrix& b) {
    if (a.rows != b.rows || a.cols != b.cols) return 1e300;
    double worst = 0;
    for (std::size_t i = 0; i < a.data.size(); ++i) worst = std::max(worst, std::abs(a.data[i] - b.data[i]));
    return worst;
}
inline double maxHermitianDefect(const Matrix& rho) {
    double worst = 0;
    for (std::size_t i = 0; i < rho.rows; ++i)
        for (std::size_t j = i; j < rho.cols; ++j) worst = std::max(worst, std::abs(rho(i, j) - std::conj(rho(j, i))));
    return worst;
}
inline std::vector<QubitIndex> allQubits(std::uint32_t n) {
    std::vector<QubitIndex> v;
    for (std::uint32_t i = 0; i < n; ++i) v.push_back(QubitIndex{i});
    return v;
}
// |count − N p| ≤ kσ with σ² = N p (1 − p); an impossible outcome (p = 0) must never appear.
inline bool withinSigma(std::uint64_t count, std::uint64_t shots, double p, double k) {
    const double n = static_cast<double>(shots);
    return std::abs(static_cast<double>(count) - n * p) <= k * std::sqrt(n * p * (1.0 - p)) + 1e-9;
}
} // namespace qtest
