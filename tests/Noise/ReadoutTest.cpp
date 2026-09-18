// Spec 08 §3, T10 §7 / spec 25 §3.4 — assignment matrices (row-stochastic, M_ij = P(read j | prepared i)),
// correlated groups, sampling statistics and constrained least-squares mitigation.
#include "NoiseTestSupport.hpp"
#include <catch2/catch_approx.hpp>

using namespace ntest;
using namespace qlab::noise;
using Catch::Approx;

namespace {
// Independent dense construction: M = M_{n−1} ⊗ … ⊗ M_0 (little-endian), entries P(read j | prepared i).
num::RealMatrix kronAssign(const std::vector<Assignment2>& per) {
    const std::size_t n = per.size(), dim = std::size_t{1} << n;
    num::RealMatrix m(dim, dim);
    for (std::size_t i = 0; i < dim; ++i)
        for (std::size_t j = 0; j < dim; ++j) {
            double v = 1.0;
            for (std::size_t k = 0; k < n; ++k) v *= per[k][(i >> k) & 1u][(j >> k) & 1u];
            m(i, j) = v;
        }
    return m;
}
const std::vector<Assignment2> kThree{{{{0.976236, 0.023764}, {0.046459, 0.953541}}},
                                      {{{0.95, 0.05}, {0.10, 0.90}}},
                                      {{{0.99, 0.01}, {0.04, 0.96}}}};
} // namespace

TEST_CASE("assignment matrices must be row-stochastic") {
    REQUIRE(validateAssignment(toRealMatrix(kThree[0]), "q0"));
    num::RealMatrix column(2, 2); // column-stochastic (T10 convention) is refused
    column(0, 0) = 0.9; column(0, 1) = 0.2; column(1, 0) = 0.1; column(1, 1) = 0.8;
    auto bad = validateAssignment(column, "q0");
    REQUIRE_FALSE(bad);
    REQUIRE(bad.error().code == err::BadReadout);
    REQUIRE(bad.error().message.find("row 0") != std::string::npos);
    num::RealMatrix negative(2, 2);
    negative(0, 0) = 1.1; negative(0, 1) = -0.1; negative(1, 1) = 1.0;
    REQUIRE_FALSE(validateAssignment(negative, "q1"));
    REQUIRE(readoutFidelity(kThree[1]) == Approx(1.0 - 0.5 * (0.05 + 0.10)).epsilon(1e-15));
}

TEST_CASE("probability vectors are mapped by q = M^T p, factor by factor") {
    auto model = ReadoutModel::independent(q({4, 0, 2}), kThree);
    NOISE_REQUIRE_OK(model);
    auto full = model->fullMatrix();
    NOISE_REQUIRE_OK(full);
    const auto expected = kronAssign(kThree);
    for (std::size_t i = 0; i < 8; ++i) {
        double row = 0.0;
        for (std::size_t j = 0; j < 8; ++j) {
            REQUIRE((*full)(i, j) == Approx(expected(i, j)).margin(1e-15));
            row += (*full)(i, j);
        }
        REQUIRE(row == Approx(1.0).margin(1e-14));
    }
    // A correlated 2-qubit group replaces the product for its members (spec 08 §3).
    num::RealMatrix corr(4, 4);
    const double rows[4][4] = {{0.93, 0.03, 0.03, 0.01}, {0.06, 0.88, 0.01, 0.05}, {0.05, 0.02, 0.90, 0.03}, {0.01, 0.07, 0.06, 0.86}};
    for (std::size_t i = 0; i < 4; ++i) for (std::size_t j = 0; j < 4; ++j) corr(i, j) = rows[i][j];
    std::vector<ReadoutFactor> factors{{{0, 2}, corr}, {{1}, toRealMatrix(kThree[1])}};
    auto grouped = ReadoutModel::make(q({4, 0, 2}), factors);
    NOISE_REQUIRE_OK(grouped);
    const std::vector<double> p{0.1, 0.05, 0.2, 0.15, 0.0, 0.3, 0.12, 0.08};
    auto out = grouped->applyToProbabilities(p);
    NOISE_REQUIRE_OK(out);
    for (std::size_t j = 0; j < 8; ++j) {
        double acc = 0.0; // bits 0 and 2 through `corr` (local index = b0 + 2 b2), bit 1 through M_1
        for (std::size_t i = 0; i < 8; ++i) {
            const std::size_t li = ((i >> 0) & 1u) | (((i >> 2) & 1u) << 1), lj = ((j >> 0) & 1u) | (((j >> 2) & 1u) << 1);
            acc += p[i] * corr(li, lj) * kThree[1][(i >> 1) & 1u][(j >> 1) & 1u];
        }
        REQUIRE((*out)[j] == Approx(acc).margin(1e-15));
    }
    std::vector<ReadoutFactor> overlapping{{{0, 2}, corr}, {{2}, toRealMatrix(kThree[1])}, {{1}, toRealMatrix(kThree[1])}};
    REQUIRE_FALSE(ReadoutModel::make(q({4, 0, 2}), overlapping));
}

TEST_CASE("apply-then-mitigate recovers the ideal distribution") {
    auto model = ReadoutModel::independent(q({0, 1, 2}), kThree);
    NOISE_REQUIRE_OK(model);
    core::Random rng(0x5eed);
    for (int trial = 0; trial < 20; ++trial) {
        std::vector<double> ideal(8);
        double total = 0.0;
        for (auto& x : ideal) { x = trial % 4 == 0 && rng.uniform() < 0.5 ? 0.0 : rng.uniform(); total += x; }
        for (auto& x : ideal) x /= total;
        auto measured = model->applyToProbabilities(ideal);
        NOISE_REQUIRE_OK(measured);
        auto mit = model->mitigate(*measured);
        NOISE_REQUIRE_OK(mit);
        REQUIRE(mit->unconstrained.size() == 8);
        for (std::size_t i = 0; i < 8; ++i) {
            REQUIRE(mit->unconstrained[i] == Approx(ideal[i]).margin(1e-12));
            REQUIRE(mit->probabilities[i] == Approx(ideal[i]).margin(1e-12));
        }
        REQUIRE(mit->residual < 1e-12);
        REQUIRE(mit->iterations == 0); // a feasible unconstrained inverse is already the optimum
        REQUIRE(mit->converged);
    }
}

TEST_CASE("constrained mitigation satisfies the KKT conditions of min ||M^T p - q|| on the simplex") {
    auto model = ReadoutModel::independent(q({0, 1, 2}), kThree);
    NOISE_REQUIRE_OK(model);
    // A GHZ-like histogram with shot noise: the unconstrained inverse has negative entries.
    const std::vector<double> measured{0.52, 0.02, 0.01, 0.0, 0.0, 0.01, 0.02, 0.42};
    auto mit = model->mitigate(measured);
    NOISE_REQUIRE_OK(mit);
    REQUIRE(mit->converged);
    REQUIRE(mit->iterations > 0);
    REQUIRE(std::any_of(mit->unconstrained.begin(), mit->unconstrained.end(), [](double x) { return x < 0.0; }));
    double sum = 0.0;
    for (double x : mit->probabilities) { REQUIRE(x >= 0.0); sum += x; }
    REQUIRE(sum == Approx(1.0).margin(1e-12));
    // Gradient g = M(Mᵀp − q): equal on the support, not smaller off it (Lagrange multiplier of Σp = 1).
    auto mt = model->applyToProbabilities(mit->probabilities);
    NOISE_REQUIRE_OK(mt);
    auto full = model->fullMatrix();
    NOISE_REQUIRE_OK(full);
    std::vector<double> g(8, 0.0);
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t j = 0; j < 8; ++j) g[i] += (*full)(i, j) * ((*mt)[j] - measured[j]);
    double multiplier = 0.0;
    std::size_t support = 0;
    for (std::size_t i = 0; i < 8; ++i)
        if (mit->probabilities[i] > 1e-9) { multiplier += g[i]; ++support; }
    REQUIRE(support > 0);
    multiplier /= static_cast<double>(support);
    for (std::size_t i = 0; i < 8; ++i) {
        if (mit->probabilities[i] > 1e-9) REQUIRE(g[i] == Approx(multiplier).margin(1e-9));
        else REQUIRE(g[i] >= multiplier - 1e-9);
    }
    // Residual never exceeds that of other feasible points.
    core::Random rng(11);
    for (int trial = 0; trial < 200; ++trial) {
        std::vector<double> other(8);
        double t = 0.0;
        for (auto& x : other) { x = -std::log(1.0 - rng.uniform()); t += x; }
        for (auto& x : other) x /= t;
        auto r = model->applyToProbabilities(other);
        NOISE_REQUIRE_OK(r);
        double res = 0.0;
        for (std::size_t j = 0; j < 8; ++j) res += ((*r)[j] - measured[j]) * ((*r)[j] - measured[j]);
        REQUIRE(std::sqrt(res) >= mit->residual - 1e-12);
    }
}

TEST_CASE("sampled readout flips match the assignment matrix within 3 sigma (10^6 shots)") {
    auto model = ReadoutModel::independent(q({3}), std::vector<Assignment2>{kThree[0]});
    NOISE_REQUIRE_OK(model);
    core::Random rng(20250916);
    const std::uint64_t perState = 500000;
    for (std::size_t prepared = 0; prepared < 2; ++prepared) {
        std::uint64_t flipped = 0, failures = 0;
        std::uint8_t bit[1];
        for (std::uint64_t s = 0; s < perState; ++s) {
            bit[0] = static_cast<std::uint8_t>(prepared);
            failures += !model->applyToBits(bit, rng).has_value();
            flipped += bit[0] != prepared;
        }
        REQUIRE(failures == 0);
        const double p = kThree[0][prepared][1 - prepared], n = static_cast<double>(perState);
        const double sigma = std::sqrt(p * (1 - p) / n);
        INFO("prepared " << prepared << " flip rate " << static_cast<double>(flipped) / n << " expected " << p);
        REQUIRE(std::abs(static_cast<double>(flipped) / n - p) < 3.0 * sigma);
    }
    // Counts keys are MSB-first over the measured qubits, like qsim::Counts.
    auto pair = ReadoutModel::independent(q({0, 1}), std::vector<Assignment2>{kThree[1], kThree[2]});
    NOISE_REQUIRE_OK(pair);
    qsim::Counts ideal{{"10", 200000}};
    auto noisy = pair->applyToCounts(ideal, rng);
    NOISE_REQUIRE_OK(noisy);
    const double n = 200000.0;
    const double p10 = kThree[1][0][0] * kThree[2][1][1]; // q0 stays 0, q1 stays 1
    REQUIRE(std::abs(static_cast<double>((*noisy)["10"]) / n - p10) < 4.0 * std::sqrt(p10 * (1 - p10) / n));
    auto probs = probabilitiesFromCounts(*noisy, 2);
    NOISE_REQUIRE_OK(probs);
    REQUIRE((*probs)[2] == Approx(static_cast<double>((*noisy)["10"]) / n).epsilon(1e-15)); // "10" = index 2
}
