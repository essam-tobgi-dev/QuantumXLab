// T10 §1–2, T04 §4.3, §5, spec 08 §4.1, §7.3 — fidelity relations and Pauli twirls, each checked
// against an independent computation.
#include "NoiseTestSupport.hpp"
#include <catch2/catch_approx.hpp>

using namespace ntest;
using namespace qlab::noise;
using Catch::Approx;

namespace {
// Haar-average fidelity of a qubit channel from the six Pauli eigenstates, a state 2-design.
double sixStateFidelity(const Kraus& k) {
    const double s = 1.0 / std::numbers::sqrt2;
    const std::vector<std::vector<Complex>> states{
        {1, 0}, {0, 1}, {s, s}, {s, -s}, {s, Complex(0, s)}, {s, Complex(0, -s)}};
    double sum = 0.0;
    for (const auto& psi : states) {
        auto out = applyToDensity(k, num::projector(psi));
        REQUIRE(out.has_value());
        sum += num::fidelity(std::span<const Complex>(psi), *out);
    }
    return sum / 6.0;
}
// Entanglement fidelity ⟨Φ|(E ⊗ I)(|Φ⟩⟨Φ|)|Φ⟩ of a two-qubit channel, simulated on 4 qubits.
double entanglementFidelity2q(const ChannelPtr& c) {
    core::Random rng(7);
    qsim::DensityMatrixBackend dm;
    NOISE_REQUIRE_OK(dm.allocate(4));
    for (std::uint32_t k = 0; k < 2; ++k) { // |Φ⟩ = ½ Σ_j |j⟩_{q1q0} |j⟩_{q3q2}
        NOISE_REQUIRE_OK(dm.applyGate(H(), q({k})));
        NOISE_REQUIRE_OK(dm.applyGate(CX(), q({k, k + 2})));
    }
    std::vector<Complex> phi(16, 0.0);
    for (std::size_t j = 0; j < 4; ++j)
        phi[j | (j << 2)] = 0.5;
    REQUIRE(num::fidelity(std::span<const Complex>(phi), dm.rho()) == Approx(1.0).margin(1e-14));
    apply(dm, attach(c, q({0, 1})), rng);
    return num::fidelity(std::span<const Complex>(phi), dm.rho());
}
// Pauli transfer matrix diagonal λ_P = Tr(P E(P))/2 of a qubit channel.
std::array<double, 4> ptmDiagonal(const Kraus& k) {
    std::array<double, 4> lambda{};
    for (std::uint32_t p = 0; p < 4; ++p) {
        auto out = applyToDensity(k, pauliMatrix(p));
        REQUIRE(out.has_value());
        lambda[p] = 0.5 * num::trace(num::matmul(pauliMatrix(p), *out)).real();
    }
    return lambda;
}
} // namespace

TEST_CASE("depolarizing average gate fidelity is 1 - p(d-1)/d and p = d r/(d-1) round-trips r") {
    for (double p : {0.0, 1e-4, 7.63e-4, 0.02, 0.5, 1.0}) {
        INFO("p = " << p);
        auto one = channels::depolarizing1q(p);
        NOISE_REQUIRE_OK(one);
        REQUIRE(averageGateFidelity(*one) ==
                Approx(1.0 - p / 2.0).margin(1e-12)); // T10 (1.3)–(1.5)
        REQUIRE(sixStateFidelity(*one) ==
                Approx(1.0 - p / 2.0).margin(1e-12)); // independent 2-design average
        REQUIRE(depolarizingEquivalent(*one) == Approx(p).margin(1e-12));
        auto two = channels::depolarizing2q(p);
        NOISE_REQUIRE_OK(two);
        REQUIRE(averageGateFidelity(*two) == Approx(1.0 - 3.0 * p / 4.0).margin(1e-12));
        const double fe = entanglementFidelity2q(channel(depolarizingChannel(2, p)));
        REQUIRE((4.0 * fe + 1.0) / 5.0 == Approx(1.0 - 3.0 * p / 4.0).margin(1e-12));
        REQUIRE(depolarizingEquivalent(*two) == Approx(p).margin(1e-12));
    }
    for (double r : {1e-5, 3.816328e-4, 6.8e-3, 0.1}) {
        for (std::uint32_t n : {1u, 2u}) {
            const double p = channels::depolarizingFromGateError(r, n);
            REQUIRE(p == Approx(n == 1 ? 2.0 * r : 4.0 * r / 3.0).epsilon(1e-15));
            REQUIRE(channels::gateErrorFromDepolarizing(p, n) == Approx(r).epsilon(1e-15));
            auto k = channels::depolarizingNq(n, p);
            NOISE_REQUIRE_OK(k);
            REQUIRE(1.0 - averageGateFidelity(*k) ==
                    Approx(r).epsilon(1e-10)); // the channel realises r
        }
    }
}

TEST_CASE("thermal relaxation infidelity matches T04 (4.6) and ignores p_th") {
    for (double pth : {0.0, 0.01, 0.2}) {
        for (double t : {32e-9, 440e-9, 10e-6}) {
            const double t1 = 90.99e-6, t2 = 80.47e-6;
            auto k = channels::thermalRelaxation(t1, t2, t, pth);
            NOISE_REQUIRE_OK(k);
            const double closed = 0.5 + (2.0 * std::exp(-t / t2) + std::exp(-t / t1)) / 6.0;
            REQUIRE(averageGateFidelity(*k) == Approx(closed).margin(1e-14));
            REQUIRE(sixStateFidelity(*k) == Approx(closed).margin(1e-14));
            REQUIRE(channels::thermalRelaxationInfidelity(t1, t2, t) ==
                    Approx(1.0 - closed).epsilon(1e-9));
        }
    }
    // T04 §4.3 example: 35 ns, T1 = 150 µs, T2 = 100 µs gives 1 − F = 1.6e-4 (two significant
    // digits).
    REQUIRE(channels::thermalRelaxationInfidelity(150e-6, 100e-6, 35e-9) ==
            Approx(1.556e-4).epsilon(1e-3));
}

TEST_CASE("over-rotation infidelity is d sin^2(eps/2)/(d+1) and the calibration angle inverts it") {
    for (const char* axis : {"X", "Y", "ZX", "XX"}) {
        const std::uint32_t n = static_cast<std::uint32_t>(std::string_view(axis).size());
        const double d = std::ldexp(1.0, static_cast<int>(n));
        for (double eps : {1e-3, 0.05, 0.7}) {
            auto k = channels::overRotation(axis, eps);
            NOISE_REQUIRE_OK(k);
            REQUIRE(1.0 - averageGateFidelity(*k) ==
                    Approx(d * std::pow(std::sin(eps / 2), 2) / (d + 1)).margin(1e-15));
            const double r = 1.0 - averageGateFidelity(*k);
            REQUIRE(channels::overRotationAngleForInfidelity(r, n) == Approx(eps).epsilon(1e-9));
        }
    }
}

TEST_CASE("Pauli twirl of amplitude damping: p_x = p_y = gamma/4, p_z = (2 - gamma - 2 "
          "sqrt(1-gamma))/4") {
    for (double gamma : {0.0, 1e-4, 0.1, 0.5, 0.99, 1.0}) {
        auto ad = channels::amplitudeDamping(gamma);
        NOISE_REQUIRE_OK(ad);
        auto tw = pauliTwirl(*ad);
        NOISE_REQUIRE_OK(tw);
        INFO("gamma = " << gamma);
        REQUIRE(tw->pX() == Approx(gamma / 4).margin(1e-15));
        REQUIRE(tw->pY() == Approx(gamma / 4).margin(1e-15));
        REQUIRE(tw->pZ() == Approx((2.0 - gamma - 2.0 * std::sqrt(1.0 - gamma)) / 4).margin(1e-15));
        REQUIRE(tw->pI() + tw->pX() + tw->pY() + tw->pZ() == Approx(1.0).margin(1e-15));
        REQUIRE(tw->exact == (gamma == 0.0)); // a non-trivial damping is not a Pauli channel
    }
}

TEST_CASE("Pauli twirl of thermal_relaxation equals spec 08 (7.1) and the PTM diagonal") {
    for (double pth : {0.0, 0.05}) {
        for (double t : {32e-9, 20e-6, 90e-6}) {
            const double t1 = 60e-6, t2 = 45e-6;
            auto relax = channel(thermalRelaxationChannel(t1, t2, pth));
            auto tw = relax->twirled(Context{t, 0.0, false});
            REQUIRE(tw.has_value());
            const double g1 = 1.0 - std::exp(-t / t1), g2 = 1.0 - std::exp(-t / t2);
            REQUIRE(tw->pX() == Approx(g1 / 4).margin(1e-14));
            REQUIRE(tw->pY() == Approx(g1 / 4).margin(1e-14));
            REQUIRE(tw->pZ() == Approx(g2 / 2 - g1 / 4).margin(1e-14));
            REQUIRE_FALSE(tw->exact);
            // The twirl keeps the diagonal of the PTM: p_P from λ = (1, λx, λy, λz) (T04 §5.3).
            auto k = relax->kraus(Context{t, 0.0, false});
            NOISE_REQUIRE_OK(k);
            const auto l = ptmDiagonal(*k);
            REQUIRE(l[0] == Approx(1.0).margin(1e-15));
            REQUIRE(tw->pI() == Approx((1 + l[1] + l[2] + l[3]) / 4).margin(1e-14));
            REQUIRE(tw->pX() == Approx((1 + l[1] - l[2] - l[3]) / 4).margin(1e-14));
            REQUIRE(tw->pY() == Approx((1 - l[1] + l[2] - l[3]) / 4).margin(1e-14));
            REQUIRE(tw->pZ() == Approx((1 - l[1] - l[2] + l[3]) / 4).margin(1e-14));
        }
    }
    // Pauli and dephasing channels twirl to themselves (χ already diagonal).
    auto dep = channels::depolarizing2q(0.3);
    NOISE_REQUIRE_OK(dep);
    auto dtw = pauliTwirl(*dep);
    NOISE_REQUIRE_OK(dtw);
    REQUIRE(dtw->exact);
    REQUIRE(dtw->probs[0] == Approx(1.0 - 15.0 * 0.3 / 16.0).epsilon(1e-15));
    REQUIRE(dtw->probs[9] == Approx(0.3 / 16.0).epsilon(1e-14));
    const double lambda = 0.37;
    auto pd = channels::phaseDamping(lambda);
    NOISE_REQUIRE_OK(pd);
    REQUIRE_FALSE(pd->isPauli); // operators are not Pauli-proportional one by one…
    auto ptw = pauliTwirl(*pd);
    NOISE_REQUIRE_OK(ptw);
    REQUIRE(ptw->exact); // …but the channel is the Pauli-Z channel p_z = (1 − √(1−λ))/2
    REQUIRE(ptw->pZ() == Approx((1.0 - std::sqrt(1.0 - lambda)) / 2).margin(1e-15));
    // A coherent rotation twirls to p_I = cos²(ε/2), p_P = sin²(ε/2) but is flagged inexact.
    auto rot = channels::overRotation("ZX", 0.3);
    NOISE_REQUIRE_OK(rot);
    auto rtw = pauliTwirl(*rot);
    NOISE_REQUIRE_OK(rtw);
    REQUIRE_FALSE(rtw->exact);
    REQUIRE(rtw->probs[3 | (1 << 2)] == Approx(std::pow(std::sin(0.15), 2)).margin(1e-15));
    REQUIRE(rtw->probs[0] == Approx(std::pow(std::cos(0.15), 2)).margin(1e-15));
}
