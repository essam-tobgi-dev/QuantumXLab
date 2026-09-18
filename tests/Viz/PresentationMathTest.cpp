// Spec 21 §2.4–2.5, §3.2–3.6 — phase hue, Q-sphere placement, amplitude selection, city/Hinton
// geometry, display colour transform and readout text. Headless.
#include "Graphics/Colormap.hpp"
#include "Viz/Math/Amplitudes.hpp"
#include "Viz/Math/Color.hpp"
#include "Viz/Math/DensityPlot.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Math/Phase.hpp"
#include "Viz/Math/QSphere.hpp"
#include "Numerics/Matrix.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

using namespace qlab;
using namespace qlab::viz::math;
using Catch::Approx;

namespace {
constexpr double kPi = std::numbers::pi;
const double kInvSqrt2 = 1.0 / std::sqrt(2.0);
bool sameColor(glm::vec3 a, glm::vec3 b, float tol = 1e-6f) {
    return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol && std::abs(a.b - b.b) <= tol;
}
} // namespace

TEST_CASE("phase hue H = (phi + pi)/(2 pi): endpoints, centre and wrap") {
    CHECK(phaseHue(0.0) == Approx(0.5).margin(1e-15));
    CHECK(phaseHue(kPi) == Approx(1.0).margin(1e-15));
    CHECK(phaseHue(kPi / 2) == Approx(0.75).margin(1e-15));
    CHECK(phaseHue(-kPi / 2) == Approx(0.25).margin(1e-15));
    // φ ∈ (−π, π]: −π is the same phase as +π, and the hue is periodic in 2π.
    CHECK(wrapPhase(-kPi) == Approx(kPi).margin(1e-15));
    CHECK(phaseHue(-kPi) == Approx(1.0).margin(1e-15));
    CHECK(phaseHue(0.3 + 2 * kPi) == Approx(phaseHue(0.3)).margin(1e-12));
    CHECK(phaseHue(0.3 - 6 * kPi) == Approx(phaseHue(0.3)).margin(1e-12));
    // The map is cyclic: H = 1 and H = 0 are the same colour, and both ends of the LUT meet.
    CHECK(sameColor(phaseColorAtHue(0.0), phaseColorAtHue(1.0)));
    CHECK(sameColor(phaseColor(kPi), phaseColor(-kPi)));
    CHECK(sameColor(phaseColor(kPi - 1e-9), phaseColor(-kPi + 1e-9), 1e-4f));
    // It is the twilight LUT of gfx::Colormap, 256 entries, entry i = twilight(i/256).
    REQUIRE(phaseLut().size() == 256);
    const auto& twilight = gfx::Colormap::get(gfx::ColormapId::Twilight);
    CHECK(twilight.cyclic());
    CHECK(sameColor(phaseLut()[64], twilight.sample(0.25f)));
    CHECK(sameColor(phaseColor(0.0), twilight.sample(0.5f)));
    CHECK(sameColor(phaseColor(-kPi / 2), twilight.sample(0.25f)));
    CHECK_FALSE(sameColor(phaseColor(0.0), phaseColor(kPi), 0.05f));
    // Phases of amplitudes: arg ∈ (−π, π], zero amplitude → 0.
    CHECK(phaseOf(Complex(-1.0, 0.0)) == Approx(kPi).margin(1e-15));
    CHECK(phaseOf(Complex(-1.0, -0.0)) == Approx(kPi).margin(1e-15));
    CHECK(phaseOf(Complex(0.0, 0.0)) == 0.0);
    const auto legend = phaseLegend(4);
    REQUIRE(legend.size() == 4);
    CHECK(legend[0].label == "0");
    CHECK(legend[1].label == "π/2");
    CHECK(legend[2].label == "π");
    CHECK(legend[3].label == "-π/2");
}

TEST_CASE("angles print as multiples of pi up to denominator 16 (spec 21 §3.13)") {
    CHECK(formatAngle(0.0) == "0");
    CHECK(formatAngle(kPi) == "π");
    CHECK(formatAngle(-kPi / 4) == "-π/4");
    CHECK(formatAngle(3 * kPi / 8) == "3π/8");
    CHECK(formatAngle(2 * kPi) == "2π");
    CHECK(formatAngle(7 * kPi / 16) == "7π/16");
    CHECK(formatAngle(kPi / 17) == "0.1848 rad");      // denominator 17: radians, 4 s.f.
    CHECK(formatAngle(kPi / 4 + 1e-6) == "0.7854 rad"); // outside the 1e-9 window
    CHECK(formatAngle(1.0) == "1 rad");
}

TEST_CASE("Q-sphere latitude is set by the Hamming weight, longitude by index order") {
    const std::uint32_t n = 3;
    CHECK(qspherePlacement(0b000, n).z == Approx(1.0));          // north pole
    CHECK(qspherePlacement(0b111, n).z == Approx(-1.0));         // south pole
    for (std::uint64_t i : {0b001u, 0b010u, 0b100u}) CHECK(qspherePlacement(i, n).z == Approx(1.0 - 2.0 / 3.0));
    for (std::uint64_t i : {0b011u, 0b101u, 0b110u}) CHECK(qspherePlacement(i, n).z == Approx(1.0 - 4.0 / 3.0));
    // Equal-weight states are spaced uniformly, ordered by index: 001, 010, 100 → 0, 2π/3, 4π/3.
    CHECK(qspherePlacement(0b001, n).longitude == Approx(0.0).margin(1e-15));
    CHECK(qspherePlacement(0b010, n).longitude == Approx(2 * kPi / 3));
    CHECK(qspherePlacement(0b100, n).longitude == Approx(4 * kPi / 3));
    CHECK(qspherePlacement(0b011, n).rank == 0);
    CHECK(qspherePlacement(0b101, n).rank == 1);
    CHECK(qspherePlacement(0b110, n).rank == 2);
    // Every node is on the unit sphere and ranks enumerate 0 … C(n, w) − 1 exactly once.
    const std::uint32_t big = 12;
    std::vector<std::vector<bool>> seen(big + 1);
    for (std::uint32_t w = 0; w <= big; ++w) seen[w].assign(binomial(big, w), false);
    for (std::uint64_t i = 0; i < (1u << big); ++i) {
        const auto p = qspherePlacement(i, big);
        CHECK(glm::length(p.position) == Approx(1.0).margin(1e-12));
        REQUIRE(p.rank < seen[p.weight].size());
        CHECK_FALSE(seen[p.weight][p.rank]);
        seen[p.weight][p.rank] = true;
    }
    CHECK(binomial(12, 6) == 924);
    CHECK(binomial(60, 30) == 118264581564861424ull);
}

TEST_CASE("Q-sphere nodes: area proportional to probability, phase colour, top-k above 12 qubits") {
    // GHZ with a relative phase of π: two polar nodes of equal size, opposite colours.
    std::vector<Complex> ghz(8, 0.0);
    ghz[0] = kInvSqrt2;
    ghz[7] = -kInvSqrt2;
    QSphereOptions o;
    const auto m = buildQSphere(ghz, o);
    REQUIRE(m.nodes.size() == 2);
    CHECK_FALSE(m.topKOnly);
    CHECK(m.nodes[0].radius == Approx(o.maxRadius * kInvSqrt2));
    CHECK(m.nodes[1].radius == Approx(o.maxRadius * kInvSqrt2));
    CHECK(m.nodes[0].radius * m.nodes[0].radius / (o.maxRadius * o.maxRadius) == Approx(0.5)); // area ∝ p
    CHECK(sameColor(m.nodes[0].color, phaseColor(0.0)));
    CHECK(sameColor(m.nodes[1].color, phaseColor(kPi)));
    CHECK(m.nodes[0].spoke);
    CHECK(m.shownProbability == Approx(1.0));
    // 13 qubits, uniform superposition: only the top-k nodes are shown and the model says so.
    std::vector<Complex> flat(std::size_t{1} << 13, 1.0 / std::sqrt(8192.0));
    const auto big = buildQSphere(flat, o);
    CHECK(big.topKOnly);
    CHECK(big.nodes.size() == kDefaultTopK);
    CHECK(big.shownProbability == Approx(256.0 / 8192.0));
}

TEST_CASE("amplitude selection: threshold, top-k with probability mass, ordering, marginals") {
    // QFT|1⟩ on 3 qubits: uniform magnitudes, phases 0, π/4, …, 7π/4 in little-endian order (spec 21 §5).
    std::vector<Complex> qft(8);
    for (std::size_t k = 0; k < 8; ++k) qft[k] = std::polar(1.0 / std::sqrt(8.0), 2 * kPi * static_cast<double>(k) / 8.0);
    const auto all = selectAmplitudes(qft);
    REQUIRE(all.entries.size() == 8);
    for (std::size_t k = 0; k < 8; ++k) {
        CHECK(all.entries[k].index == k);
        CHECK(all.entries[k].probability == Approx(0.125));
        CHECK(all.entries[k].phase == Approx(wrapPhase(2 * kPi * static_cast<double>(k) / 8.0)).margin(1e-12));
    }
    CHECK(all.totalProbability == Approx(1.0));
    CHECK_FALSE(all.truncated);

    std::vector<Complex> psi{0.1, 0.7, 0.0, 0.5, Complex(0, 0.5), 0.005, 0.0, 0.0};
    AmplitudeFilter f;
    f.maxEntries = 2;
    f.order = AmplitudeOrder::ByMagnitude;
    const auto top = selectAmplitudes(psi, f);
    REQUIRE(top.entries.size() == 2);
    CHECK(top.entries[0].index == 1);
    CHECK(top.entries[1].index == 3);          // ties (|0.5|² twice) break toward the smaller index
    CHECK(top.truncated);
    CHECK(top.aboveThreshold == 4);            // 0.005² = 2.5e-5 is under ε = 1e-4
    CHECK(top.shownProbability == Approx(0.49 + 0.25));
    // Marginal over qubit 2 then qubit 0 (subset[0] is the least significant bit of the result).
    const auto probs = bornProbabilities(psi);
    const std::vector<QubitIndex> subset{QubitIndex{2}, QubitIndex{0}};
    auto marg = marginalProbabilities(probs, 3, subset);
    REQUIRE(marg.has_value());
    REQUIRE(marg->size() == 4);
    CHECK((*marg)[0] == Approx(0.01));                       // q2 = 0, q0 = 0: index 0 and 2
    CHECK((*marg)[1] == Approx(0.25));                       // q2 = 1, q0 = 0: index 4 and 6
    CHECK((*marg)[2] == Approx(0.49 + 0.25));                // q2 = 0, q0 = 1: index 1 and 3
    CHECK((*marg)[3] == Approx(0.005 * 0.005));              // q2 = 1, q0 = 1: index 5 and 7
    CHECK_FALSE(marginalProbabilities(probs, 3, std::vector<QubitIndex>{QubitIndex{3}}).has_value());
}

TEST_CASE("city-plot bars and Hinton squares of a Bell state") {
    num::Matrix rho(4, 4);
    rho(0, 0) = rho(3, 3) = 0.5;
    rho(0, 3) = Complex(0.0, 0.5);  // (|00⟩ − i|11⟩)/√2 has ρ_03 = +i/2
    rho(3, 0) = Complex(0.0, -0.5);
    auto city = buildCity(rho, CityQuantity::Real);
    REQUIRE(city.has_value());
    REQUIRE(city->bars.size() == 2); // the imaginary coherences have no real part
    CHECK(city->bars[0].diagonal);
    CHECK(city->bars[0].height == Approx(0.5));
    auto imag = buildCity(rho, CityQuantity::Imag);
    REQUIRE(imag->bars.size() == 2);
    CHECK(imag->bars[0].row == 0);
    CHECK(imag->bars[0].col == 3);
    CHECK(imag->bars[0].height == Approx(0.5));
    CHECK(imag->bars[1].height == Approx(-0.5));
    auto hinton = buildHinton(rho);
    REQUIRE(hinton->squares.size() == 4);
    for (const auto& s : hinton->squares) CHECK(s.side == Approx(1.0)); // all four have |ρ_ij| = max
    CHECK(sameColor(hinton->squares[1].color, phaseColor(kPi / 2)));     // ρ_03 = +i/2
    // Area ∝ |ρ_ij|: a quarter of the magnitude is half the side.
    num::Matrix two(2, 2);
    two(0, 0) = 0.8;
    two(1, 1) = 0.2;
    auto h2 = buildHinton(two);
    CHECK(h2->squares[1].side == Approx(0.5));
    CHECK_FALSE(buildCity(num::Matrix(512, 512)).has_value()); // 9 qubits: a subset is required
    CHECK_FALSE(buildHinton(num::Matrix(3, 3)).has_value());
}

TEST_CASE("display colours survive the renderer's ACES + gamma tone map exactly") {
    for (const glm::vec3 c : {glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(0.31f, 0.64f, 1.0f), glm::vec3(0.886f, 0.85f, 0.888f),
                              glm::vec3(0.186f, 0.078f, 0.223f)}) {
        const glm::vec3 back = sceneToDisplay(displayToScene(c));
        CHECK(back.r == Approx(c.r).margin(2e-6));
        CHECK(back.g == Approx(c.g).margin(2e-6));
        CHECK(back.b == Approx(c.b).margin(2e-6));
    }
    const auto accent = colorFromHex("#4FA3FF");
    REQUIRE(accent.has_value());
    CHECK(accent->r == Approx(79.0 / 255.0));
    CHECK(accent->a == Approx(1.0));
    CHECK(colorFromHex("#4FA3FF33")->a == Approx(51.0 / 255.0));
    CHECK_FALSE(colorFromHex("4FA3FF").has_value());
    CHECK_FALSE(colorFromHex("#4FA3FG").has_value());
    CHECK(contrastRatio(glm::vec3(1.0f), glm::vec3(0.0f)) == Approx(21.0));
    CHECK(readableOn(phaseColor(0.0), glm::vec3(1.0f), glm::vec3(0.0f)) == glm::vec3(1.0f)); // dark hue → light text
    CHECK(readableOn(phaseColor(kPi), glm::vec3(1.0f), glm::vec3(0.0f)) == glm::vec3(0.0f));
}

TEST_CASE("readout text: 4 significant figures, little-endian kets, both complex forms") {
    CHECK(formatSig(0.70710678) == "0.7071");
    CHECK(formatSig(1234.5678) == "1235");
    CHECK(formatSig(1.2345e-5) == "1.234e-05");
    CHECK(formatSig(0.0) == "0");
    CHECK(bitString(1, 3) == "001");   // qubit 0 is the rightmost character
    CHECK(bitString(4, 3) == "100");
    CHECK(ketLabel(6, 3) == "|110⟩");
    CHECK(ketLabel(6, 3, true) == "|6⟩");
    CHECK(ketLabel(6, 3, false, true) == "|110>");
    CHECK(braLabel(1, 2) == "⟨01|");
    CHECK(formatCartesian(Complex(0.5, -0.5)) == "0.5 - 0.5i");
    CHECK(formatPolar(Complex(0.5, -0.5)) == "0.7071 e^{i(-π/4)}");
    CHECK(formatTime(320e-9) == "320 ns");
    CHECK(formatFrequency(4.812e9) == "4.812 GHz");
}
