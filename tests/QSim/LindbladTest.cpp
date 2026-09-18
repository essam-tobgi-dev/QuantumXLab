// Spec 07 §5, §9, spec 25 §3.5 — Lindblad backend on qubits against closed forms: T1 decay from a
// collapse operator, Rabi and detuned Rabi, drive quadratures, pure dephasing, every integrator.
#include "TimeDomain.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace qtest;
using Catch::Approx;

namespace {
constexpr LindbladIntegrator kIntegrators[] = {LindbladIntegrator::Rk4, LindbladIntegrator::Dopri5, LindbladIntegrator::Magnus2};

LindbladBackend install(const SystemModel& m, std::vector<Complex> psi, LindbladSettings s = {}) {
    LindbladBackend lb;
    lb.setSettings(s);
    REQUIRE(lb.setModel(m).has_value());
    REQUIRE(lb.setPure(psi).has_value());
    return lb;
}
double expect(const LindbladBackend& lb, const char* label) { return lb.expectation(*PauliString::parse(label)).value(); }
} // namespace

TEST_CASE("Lindblad: sqrt(gamma) sigma- decays |1> as exp(-gamma t)") {
    // γ = 1/(100 µs), t = 50 µs; default settings take the exact propagator for this idle (spec 07 §5).
    const double gamma = 1.0 / 100e-6, t = 50e-6;
    SystemModel m = bareModel(1, 2);
    m.collapse.push_back(decay(m, 0, gamma));
    auto lb = install(m, {0.0, 1.0});
    REQUIRE(lb.evolve(t).has_value());
    REQUIRE(lb.population(0, 1) == Approx(std::exp(-gamma * t)).margin(1e-6));
    REQUIRE(lb.stateNorm() == Approx(1.0).margin(1e-12));
    // The 1 ns record starts from the installed |1⟩ and follows e^{−γt} throughout.
    const auto traj = lb.trajectory();
    REQUIRE(traj.size() == 50001);
    for (std::size_t k : {0u, 1u, 10000u, 25000u, 50000u}) {
        INFO("sample " << k);
        REQUIRE(traj[k].timeS == Approx(double(k) * 1e-9).margin(1e-15));
        REQUIRE(traj[k].populations[1] == Approx(std::exp(-gamma * traj[k].timeS)).margin(1e-6));
    }
    // Coherence of |+⟩ decays at γ/2 (T04 (3.1)).
    const double s = 1.0 / std::sqrt(2.0);
    auto plus = install(m, {s, s});
    REQUIRE(plus.evolve(t).has_value());
    REQUIRE(expect(plus, "X") == Approx(std::exp(-0.5 * gamma * t)).margin(1e-9));
    REQUIRE(std::abs(plus.rho()(0, 1)) == Approx(0.5 * std::exp(-0.5 * gamma * t)).margin(1e-9));
    // Every integrator reproduces the same oracle when the exact path is disabled.
    for (auto integrator : kIntegrators) {
        INFO("integrator " << static_cast<int>(integrator));
        LindbladSettings st;
        st.integrator = integrator;
        st.stepS = 0.5e-6;
        st.sampleS = 5e-6;
        st.constantPropagatorSteps = std::numeric_limits<std::size_t>::max();
        auto run = install(m, {0.0, 1.0}, st);
        REQUIRE(run.evolve(t).has_value());
        REQUIRE(run.population(0, 1) == Approx(std::exp(-gamma * t)).margin(1e-6));
        REQUIRE(run.trajectory().size() == 11);
    }
}

TEST_CASE("Lindblad: resonant (Omega/2) X drive gives P1 = sin^2(Omega t / 2)") {
    const double omega = kTwoPi * 10e6, t = 80e-9;
    SystemModel m = bareModel(1, 2);
    m.drives.push_back(ladderDrive(m, 0, [omega](double) { return Complex(0.5 * omega, 0.0); }));
    for (auto integrator : kIntegrators) {
        INFO("integrator " << static_cast<int>(integrator));
        LindbladSettings st;
        st.integrator = integrator;
        auto lb = install(m, {1.0, 0.0}, st);
        REQUIRE(lb.evolve(t).has_value());
        REQUIRE(lb.trajectory().size() == 81);
        for (const auto& sample : lb.trajectory()) {
            INFO("t = " << sample.timeS);
            REQUIRE(sample.populations[1] == Approx(std::pow(std::sin(0.5 * omega * sample.timeS), 2)).margin(1e-6));
        }
    }
    // Spec 07 §9: ideal π/2 pulse versus the gate model RX(π/2)|0⟩.
    auto half = install(m, {1.0, 0.0});
    REQUIRE(half.evolve(0.25 / 10e6).has_value());
    StateVectorBackend sv;
    REQUIRE(sv.allocate(1).has_value());
    REQUIRE(sv.applyGate(RX(std::numbers::pi / 2), q({0})).has_value());
    REQUIRE(measures::fidelityTo(half.rho(), sv.amplitudes()) > 1.0 - 1e-9);
}

TEST_CASE("Lindblad: drive quadratures follow T07 and a detuned drive follows the Rabi formula") {
    // Ω(t) = iΩ/2 is H = (Ω/2)σy: |0⟩ rotates towards +x, ⟨X⟩ = sin Ωt, ⟨Z⟩ = cos Ωt (T07 (1.2)).
    const double omega = kTwoPi * 10e6;
    SystemModel m = bareModel(1, 2);
    m.drives.push_back(ladderDrive(m, 0, [omega](double) { return Complex(0.0, 0.5 * omega); }));
    for (double t : {25e-9, 60e-9}) {
        INFO("t = " << t);
        auto lb = install(m, {1.0, 0.0});
        REQUIRE(lb.evolve(t).has_value());
        REQUIRE(expect(lb, "X") == Approx(std::sin(omega * t)).margin(1e-6));
        REQUIRE(expect(lb, "Y") == Approx(0.0).margin(1e-6));
        REQUIRE(expect(lb, "Z") == Approx(std::cos(omega * t)).margin(1e-6));
    }
    // H = −(Δ/2)Z + (Ω/2)X: P₁ = Ω²/(Ω² + Δ²) sin²(√(Ω² + Δ²) t/2) (spec 25 §3.5, T07 (2.1)).
    const double delta = kTwoPi * 7e6;
    SystemModel detuned = bareModel(1, 2);
    detuned.h0 = Z();
    detuned.h0 *= -0.5 * delta;
    detuned.drives.push_back(ladderDrive(detuned, 0, [omega](double) { return Complex(0.5 * omega, 0.0); }));
    auto lb = install(detuned, {1.0, 0.0});
    REQUIRE(lb.evolve(100e-9).has_value());
    const double wr = std::hypot(omega, delta);
    for (const auto& sample : lb.trajectory()) {
        INFO("t = " << sample.timeS);
        const double p1 = omega * omega / (wr * wr) * std::pow(std::sin(0.5 * wr * sample.timeS), 2);
        REQUIRE(sample.populations[1] == Approx(p1).margin(1e-6));
    }
}

TEST_CASE("Lindblad: pure dephasing decays <X> and leaves <Z>") {
    // L = √(2γφ) a†a: ⟨X⟩ ∝ e^{−γφ t}, populations fixed (T04 (4.1)); state RY(θ)|0⟩.
    const double gphi = 1.0 / 30e-6, theta = 1.1, t = 20e-6;
    SystemModel m = bareModel(1, 2);
    m.collapse.push_back(dephasing(m, 0, gphi));
    auto lb = install(m, {std::cos(theta / 2), std::sin(theta / 2)});
    REQUIRE(expect(lb, "X") == Approx(std::sin(theta)).margin(1e-12));
    REQUIRE(lb.evolve(t).has_value());
    REQUIRE(expect(lb, "X") == Approx(std::sin(theta) * std::exp(-gphi * t)).margin(1e-9));
    REQUIRE(expect(lb, "Y") == Approx(0.0).margin(1e-12));
    REQUIRE(expect(lb, "Z") == Approx(std::cos(theta)).margin(1e-12));
    REQUIRE(lb.stateNorm() == Approx(1.0).margin(1e-12));
    for (const auto& sample : lb.trajectory()) REQUIRE(sample.populations[1] == Approx(std::pow(std::sin(theta / 2), 2)).margin(1e-12));
    // On a d = 3 transmon the same operator dephases the qubit coherence at γφ as well.
    SystemModel transmon = bareModel(1, 3);
    transmon.collapse.push_back(dephasing(transmon, 0, gphi));
    auto tr = install(transmon, {std::cos(theta / 2), std::sin(theta / 2)});
    REQUIRE(tr.evolve(t).has_value());
    REQUIRE(expect(tr, "X") == Approx(std::sin(theta) * std::exp(-gphi * t)).margin(1e-9));
    REQUIRE(expect(tr, "Z") == Approx(std::cos(theta)).margin(1e-12));
    REQUIRE(tr.leakage() == Approx(0.0).margin(1e-15));
}

TEST_CASE("Lindblad: measurement, reset, sampling and reduced states on the computational subspace") {
    // |ψ⟩ = (|00⟩ + |11⟩)/√2 on two d = 3 transmons plus 10 % of |2⟩ on site 0 through setRho.
    SystemModel m = bareModel(2, 3);
    const double s = 1.0 / std::sqrt(2.0);
    LindbladBackend lb = install(m, {s, 0.0, 0.0, s});
    REQUIRE(expect(lb, "XX") == Approx(1.0).margin(1e-12));
    Matrix rho = lb.rho();
    rho *= 0.9;
    rho(2, 2) += 0.1; // |q1 = 0, q0 = 2⟩
    REQUIRE(lb.setRho(rho).has_value());
    REQUIRE(lb.leakage() == Approx(0.1).margin(1e-12));
    auto p = lb.probabilities(q({0, 1}));
    REQUIRE(p.has_value());
    REQUIRE((*p)[0] == Approx(0.5).margin(1e-12)); // renormalised on the computational subspace
    REQUIRE((*p)[3] == Approx(0.5).margin(1e-12));
    REQUIRE(lb.probabilities(q({0, 0})).error().code == err::BadTargets);
    auto snap = lb.snapshot(SnapshotRequest{false, true, true, {q({1, 0})}, false});
    REQUIRE(snap.has_value());
    REQUIRE(snap->cls == FidelityClass::Numerical);
    REQUIRE(snap->reduced.size() == 1);
    REQUIRE(snap->reduced[0].qubits == q({1, 0}));
    core::Random rng(11);
    REQUIRE(lb.measure({}, rng)->bits.empty());
    auto copy = lb;
    auto out = copy.measure(q({1}), rng);
    REQUIRE(out.has_value());
    REQUIRE(out->probability == Approx(0.5).margin(1e-12));
    REQUIRE(copy.probabilities(q({0}))->at(out->bits[0]) == Approx(1.0).margin(1e-12));
    REQUIRE(copy.reset(q({0, 1}), rng).has_value());
    REQUIRE(copy.population(0, 0) == Approx(1.0).margin(1e-12));
    REQUIRE(copy.population(1, 0) == Approx(1.0).margin(1e-12));
    auto counts = lb.sample(q({1, 0}), 400, rng);
    REQUIRE(counts.has_value());
    REQUIRE((*counts)["00"] + (*counts)["11"] == 400);
}
