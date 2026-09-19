// Spec 21 §2.5, §3.7, §3.11 — Wilson intervals, histogram model with theory overlay and distances,
// Pauli expectations (exact and from shots). Headless.
#include "Viz/Math/Statistics.hpp"
#include "Core/Random.hpp"
#include "Data/Fidelity.hpp"
#include "Numerics/Matrix.hpp"
#include "Viz/Math/PauliTable.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace qlab;
using namespace qlab::viz::math;
using Catch::Approx;
using num::Complex;

namespace {
const double kInvSqrt2 = 1.0 / std::sqrt(2.0);

// Spec 21 §2.5 written out, independent of data::wilson.
std::pair<double, double> wilsonFormula(double k, double n, double z) {
    const double p = k / n;
    const double centre = p + z * z / (2 * n);
    const double half = z * std::sqrt(p * (1 - p) / n + z * z / (4 * n * n));
    return {(centre - half) / (1 + z * z / n), (centre + half) / (1 + z * z / n)};
}

qsim::PauliString pauli(const char* label) {
    return *qsim::PauliString::parse(label);
}
} // namespace

TEST_CASE("Wilson score interval at 68.3 % (z = 1) matches the closed form") {
    for (auto [k, n] : {std::pair<std::uint64_t, std::uint64_t>{500, 1000},
                        {3, 1000},
                        {997, 1000},
                        {1, 7},
                        {40, 100}}) {
        const auto bar = wilsonBar(k, n);
        const auto [lo, hi] = wilsonFormula(static_cast<double>(k), static_cast<double>(n), 1.0);
        CHECK(bar.pHat == Approx(static_cast<double>(k) / static_cast<double>(n)));
        CHECK(bar.lo == Approx(lo).margin(1e-14));
        CHECK(bar.hi == Approx(hi).margin(1e-14));
        CHECK(bar.lo <= bar.pHat);
        CHECK(bar.hi >= bar.pHat);
    }
    // k = 0: the interval is [0, z²/(N + z²)] — never the degenerate [0, 0] of the normal
    // approximation.
    const auto none = wilsonBar(0, 1000);
    CHECK(none.lo == Approx(0.0).margin(1e-15));
    CHECK(none.hi == Approx(1.0 / 1001.0).margin(1e-14));
    const auto all = wilsonBar(1000, 1000);
    CHECK(all.hi == Approx(1.0).margin(1e-15));
    CHECK(all.lo == Approx(1000.0 / 1001.0).margin(1e-14));
    // The 95 % interval is wider than the 68.3 % one.
    CHECK(wilsonBar(500, 1000, 1.96).hi > wilsonBar(500, 1000, 1.0).hi);
    const auto empty = wilsonBar(0, 0);
    CHECK(empty.lo == 0.0);
    CHECK(empty.hi == 1.0);
}

TEST_CASE(
    "Bell-state histogram: two bars at 0.5 within the Wilson interval for N = 1000 (spec 21 §5)") {
    core::Random rng(20260917);
    data::Histogram counts(2);
    for (int shot = 0; shot < 1000; ++shot)
        counts.add(rng.bernoulli(0.5) ? 0b11u : 0b00u);
    const std::vector<double> ideal{0.5, 0.0, 0.0, 0.5};
    auto model = buildHistogram(counts, ideal);
    REQUIRE(model.has_value());
    REQUIRE(model->bars.size() == 2);
    CHECK(model->shots == 1000);
    CHECK(model->bars[0].label == "00");
    CHECK(model->bars[1].label == "11");
    for (const auto& bar : model->bars) {
        REQUIRE(bar.ideal.has_value());
        CHECK(*bar.ideal == Approx(0.5));
        // 1σ interval of half-width ≈ 0.0158: allow two of them around the exact value.
        CHECK(std::abs(bar.estimate.pHat - 0.5) < 2.0 * (bar.estimate.hi - bar.estimate.lo) / 2.0);
    }
    CHECK(model->bars[1].cumulative == Approx(1.0));
    REQUIRE(model->hellinger.has_value());
    CHECK(*model->hellinger < 0.03);
    CHECK(*model->totalVariation ==
          Approx(std::abs(model->bars[0].estimate.pHat - 0.5)).margin(1e-12));
    CHECK(model->cls == data::FidelityClass::Statistical);
}

TEST_CASE(
    "histogram distances, ordering, unobserved ideal outcomes, marginal and the 'other' bin") {
    data::Histogram counts(3);
    counts.add("000", 50);
    counts.add("011", 30);
    counts.add("101", 20);
    // Ideal: 000 → 0.5, 011 → 0.25, 110 → 0.25 (never observed), 101 → 0.
    std::vector<double> ideal(8, 0.0);
    ideal[0b000] = 0.5;
    ideal[0b011] = 0.25;
    ideal[0b110] = 0.25;
    auto m = buildHistogram(counts, ideal);
    REQUIRE(m.has_value());
    REQUIRE(m->bars.size() == 4); // the unobserved 110 is listed with zero counts: no mass is lost
    CHECK(m->bars[3].label == "110");
    CHECK(m->bars[3].count == 0);
    CHECK(*m->bars[3].ideal == Approx(0.25));
    // H = sqrt(1 − Σ sqrt(p̂ p)) and TVD = ½ Σ |p̂ − p| over all eight outcomes.
    const double bc = std::sqrt(0.5 * 0.5) + std::sqrt(0.3 * 0.25);
    CHECK(*m->hellinger == Approx(std::sqrt(1.0 - bc)).margin(1e-12));
    CHECK(*m->totalVariation == Approx(0.5 * (0.0 + 0.05 + 0.2 + 0.25)).margin(1e-12));
    const std::vector<double> pHat{0.5, 0, 0, 0.3, 0, 0.2, 0, 0};
    CHECK(hellingerDistance(pHat, ideal) == Approx(*m->hellinger).margin(1e-12));
    CHECK(totalVariationDistance(pHat, ideal) == Approx(*m->totalVariation).margin(1e-12));
    CHECK(hellingerDistance(ideal, ideal) == Approx(0.0).margin(1e-7));

    HistogramOptions byCount;
    byCount.order = HistogramOrder::ByCount;
    auto sorted = buildHistogram(counts, ideal, byCount);
    CHECK(sorted->bars[0].label == "000");
    CHECK(sorted->bars[1].label == "011");
    CHECK(sorted->bars[2].label == "101");

    HistogramOptions marginal;
    marginal.marginalBits = {0}; // bit 0 is the rightmost character
    auto bit0 = buildHistogram(counts, ideal, marginal);
    REQUIRE(bit0->bars.size() == 2);
    CHECK(bit0->bars[0].label == "0");
    CHECK(bit0->bars[0].count == 50);
    CHECK(bit0->bars[1].count == 50);
    CHECK(*bit0->bars[0].ideal == Approx(0.75)); // 000 and 110
    CHECK(*bit0->bars[1].ideal == Approx(0.25)); // 011

    HistogramOptions top;
    top.maxOutcomes = 2;
    auto cut = buildHistogram(counts, {}, top);
    REQUIRE(cut->bars.size() == 3);
    CHECK(cut->bars.back().other);
    CHECK(cut->bars.back().count == 20);
    CHECK(cut->distinct == 3);
    CHECK_FALSE(cut->hasIdeal);
    CHECK_FALSE(buildHistogram(counts, std::vector<double>(4, 0.25)).has_value()); // wrong length
}

TEST_CASE("Pauli expectations of a Bell state: <XX> = 1, <YY> = -1, <ZZ> = 1, singles vanish") {
    const std::vector<Complex> bell{kInvSqrt2, 0.0, 0.0, kInvSqrt2};
    CHECK(*pauliExpectation(bell, pauli("XX")) == Approx(1.0).margin(1e-12));
    CHECK(*pauliExpectation(bell, pauli("YY")) == Approx(-1.0).margin(1e-12));
    CHECK(*pauliExpectation(bell, pauli("ZZ")) == Approx(1.0).margin(1e-12));
    CHECK(*pauliExpectation(bell, pauli("-ZZ")) == Approx(-1.0).margin(1e-12));
    for (const auto& p : singleQubitPaulis(2))
        CHECK(*pauliExpectation(bell, p) == Approx(0.0).margin(1e-12));
    const num::Matrix rho = num::projector(bell);
    CHECK(*pauliExpectation(rho, pauli("XX")) == Approx(1.0).margin(1e-12));
    CHECK(*pauliExpectation(rho, pauli("YY")) == Approx(-1.0).margin(1e-12));
    CHECK(*pauliExpectation(rho, pauli("XY")) == Approx(0.0).margin(1e-12));
    CHECK_FALSE(pauliExpectation(bell, pauli("XXX")).has_value());
    // MSB-first labels: "ZI" is Z on qubit 1. |01⟩ (index 1: qubit 0 excited) has ⟨ZI⟩ = +1, ⟨IZ⟩ =
    // −1.
    const std::vector<Complex> q0excited{0.0, 1.0, 0.0, 0.0};
    CHECK(*pauliExpectation(q0excited, pauli("ZI")) == Approx(1.0));
    CHECK(*pauliExpectation(q0excited, pauli("IZ")) == Approx(-1.0));
    // |+i⟩: ⟨Y⟩ = +1 (the sign convention of the Bloch vector).
    const std::vector<Complex> plusI{kInvSqrt2, Complex(0.0, kInvSqrt2)};
    CHECK(*pauliExpectation(plusI, pauli("Y")) == Approx(1.0).margin(1e-12));
    const auto singles = singleQubitPaulis(3);
    REQUIRE(singles.size() == 9);
    CHECK(pauliRowLabel(singles[0]) == "X0");
    CHECK(pauliRowLabel(singles[8]) == "Z2");
    CHECK(singles[8].label() == "ZII");
    CHECK(pauliRowLabel(pauli("XZIY")) == "XZIY");
    auto padded = parseUserPauli(" ZZ ", 4);
    REQUIRE(padded.has_value());
    CHECK(padded->label() == "IIZZ");
    CHECK_FALSE(parseUserPauli("ZZQ", 4).has_value());
    CHECK_FALSE(parseUserPauli("ZZZZZ", 4).has_value());
    CHECK_FALSE(parseUserPauli("iZZ", 4).has_value());
}

TEST_CASE("Pauli estimator from shots: value, standard error, and 'not measured'") {
    data::Histogram counts(2);
    counts.add("00", 480);
    counts.add("11", 470);
    counts.add("01", 30);
    counts.add("10", 20);
    MeasurementMap map;
    map.basis = {'Z', 'Z'};
    map.bitOfQubit = {0, 1};
    auto zz = pauliFromCounts(counts, pauli("ZZ"), map);
    REQUIRE(zz.has_value());
    CHECK(zz->value == Approx((480.0 + 470.0 - 30.0 - 20.0) / 1000.0));
    CHECK(zz->standardError ==
          Approx(std::sqrt((1.0 - 0.9 * 0.9) / 1000.0)).margin(1e-12)); // spec 21 §3.11
    auto zi = pauliFromCounts(counts, pauli("ZI"), map); // qubit 1 → bit 1: "10" and "11" are −1
    REQUIRE(zi.has_value());
    CHECK(zi->value == Approx((480.0 + 30.0 - 470.0 - 20.0) / 1000.0));
    CHECK_FALSE(
        pauliFromCounts(counts, pauli("XX"), map).has_value()); // measured in Z: "not measured"
    map.basis = {'X', 'Z'};
    CHECK(pauliFromCounts(counts, pauli("ZX"), map).has_value());
    CHECK_FALSE(pauliFromCounts(counts, pauli("ZZ"), map).has_value());
    map.bitOfQubit = {-1, 1};
    CHECK_FALSE(
        pauliFromCounts(counts, pauli("ZX"), map).has_value()); // qubit 0 was never read out
    CHECK(pauliFromCounts(counts, pauli("ZI"), map).has_value());
}
