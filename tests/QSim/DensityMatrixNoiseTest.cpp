// Spec 07 §3.2, spec 25 §3.4 — Kraus channels, projective measurement and sampling on the
// density-matrix backend, with closed-form expectations.
#include "Circuits.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "Numerics/Checks.hpp"

using namespace qtest;
using Catch::Approx;

namespace {
// sqrt(1-p1)|00> + sqrt(p1)|11> on (q1,q0), q2 in |+>.
void prepareCorrelated(IBackend& b, double p1) {
    REQUIRE(b.allocate(3).has_value());
    REQUIRE(b.applyGate(RY(2.0 * std::asin(std::sqrt(p1))), q({0})).has_value());
    REQUIRE(b.applyGate(CX(), q({0, 1})).has_value());
    REQUIRE(b.applyGate(H(), q({2})).has_value());
}
} // namespace

TEST_CASE("DM: amplitude damping gives the exact T1 population") {
    // Spec 25 §3.4: prepare |1>, idle t: rho_11 = exp(-t/T1) with gamma = 1 - exp(-t/T1).
    const double t1 = 80e-6, t = 35e-6;
    DensityMatrixBackend dm;
    REQUIRE(dm.allocate(3).has_value());
    REQUIRE(dm.applyGate(X(), q({1})).has_value());
    REQUIRE(dm.applyGate(H(), q({0})).has_value()); // spectators must be untouched
    REQUIRE(dm.applyGate(CX(), q({0, 2})).has_value());
    REQUIRE(dm.applyChannel(amplitudeDamping(1.0 - std::exp(-t / t1)), q({1})).has_value());
    REQUIRE(dm.population(QubitIndex{1}, 1) == Approx(std::exp(-t / t1)).margin(1e-12));
    REQUIRE(dm.population(QubitIndex{1}, 0) == Approx(1.0 - std::exp(-t / t1)).margin(1e-12));
    REQUIRE(dm.stateNorm() == Approx(1.0).margin(1e-12));
    auto spect = dm.reducedDensityMatrix(q({0, 2}));
    REQUIRE(spect.has_value());
    REQUIRE(measures::purity(*spect) == Approx(1.0).margin(1e-12)); // the Bell pair on (0,2) stays pure
    // Composition: ten idles of t/10 equal one idle of t (semigroup property of the channel).
    DensityMatrixBackend steps;
    REQUIRE(steps.allocate(1).has_value());
    REQUIRE(steps.applyGate(X(), q({0})).has_value());
    for (int i = 0; i < 10; ++i)
        REQUIRE(steps.applyChannel(amplitudeDamping(1.0 - std::exp(-t / (10.0 * t1))), q({0})).has_value());
    REQUIRE(steps.population(QubitIndex{0}, 1) == Approx(std::exp(-t / t1)).margin(1e-12));
    // Coherence of |+> decays as sqrt(1-gamma) = exp(-t/2T1) (T2 = 2 T1 limit).
    DensityMatrixBackend plus;
    REQUIRE(plus.allocate(1).has_value());
    REQUIRE(plus.applyGate(H(), q({0})).has_value());
    REQUIRE(plus.applyChannel(amplitudeDamping(1.0 - std::exp(-t / t1)), q({0})).has_value());
    REQUIRE(std::abs(plus.rho()(0, 1)) == Approx(0.5 * std::exp(-t / (2.0 * t1))).margin(1e-12));
}

TEST_CASE("DM: depolarizing Kraus set lowers purity and preserves the trace") {
    const double p = 0.3;
    DensityMatrixBackend one;
    REQUIRE(one.allocate(1).has_value());
    REQUIRE(one.applyChannel(depolarizing(p), q({0})).has_value());
    // |0><0| -> (1 - 2p/3)|0><0| + (2p/3)|1><1|.
    REQUIRE(one.rho()(1, 1).real() == Approx(2.0 * p / 3.0).margin(1e-12));
    REQUIRE(one.purity() == Approx(std::pow(1 - 2 * p / 3, 2) + std::pow(2 * p / 3, 2)).margin(1e-12));
    REQUIRE(one.purity() < 1.0);
    REQUIRE(one.stateNorm() == Approx(1.0).margin(1e-10));
    // On half a Bell pair: rho -> (1 - 4p/3)|Phi+><Phi+| + (4p/3) I/4, purity (1-p)^2 + p^2/3.
    DensityMatrixBackend bell;
    REQUIRE(bell.allocate(2).has_value());
    REQUIRE(bell.applyGate(H(), q({0})).has_value());
    REQUIRE(bell.applyGate(CX(), q({0, 1})).has_value());
    REQUIRE(bell.applyChannel(depolarizing(p), q({1})).has_value());
    REQUIRE(bell.purity() == Approx((1 - p) * (1 - p) + p * p / 3.0).margin(1e-12));
    REQUIRE(bell.stateNorm() == Approx(1.0).margin(1e-10));
    REQUIRE(maxHermitianDefect(bell.rho()) < 1e-12);
    REQUIRE(num::isPositiveSemidefinite(bell.rho(), 1e-12));
    REQUIRE(bell.rho()(0, 3).real() == Approx(0.5 * (1 - 4 * p / 3)).margin(1e-12));
    REQUIRE(bell.expectation(*PauliString::parse("ZZ")).value() == Approx(1 - 4 * p / 3).margin(1e-12));
    // A long noisy circuit keeps the trace at 1 to 1e-10 (spec 07 §3.2 invariants).
    DensityMatrixBackend run;
    REQUIRE(run.allocate(4).has_value());
    const Circuit c = randomUniversal(4, 150, 9);
    for (const auto& op : c) {
        REQUIRE((op.controls.empty() ? run.applyGate(op.u, op.targets)
                                     : run.applyControlled(op.u, op.controls, op.targets)).has_value());
        REQUIRE(run.applyChannel(depolarizing(0.01), {&op.targets[0], 1}).has_value());
    }
    REQUIRE(std::abs(run.stateNorm() - 1.0) < 1e-10);
    REQUIRE(run.purity() < 0.9);
    REQUIRE(num::isDensityMatrix(run.rho(), 1e-10));
    // Malformed Kraus sets are refused.
    REQUIRE(run.applyChannel(Kraus{}, q({0})).error().code == err::BadKraus);
    REQUIRE(run.applyChannel(depolarizing(0.1), q({0, 1})).error().code == err::BadKraus); // 2x2 on two sites
}

TEST_CASE("DM: a qubit channel on a d = 3 site preserves the leaked population") {
    DensityMatrixBackend dm;
    REQUIRE(dm.allocate(1, 3).has_value());
    REQUIRE(dm.applyGate(RY(1.2), q({0})).has_value());
    Matrix x12(3, 3);
    x12(0, 0) = 1; x12(1, 2) = 1; x12(2, 1) = 1;
    REQUIRE(dm.applyGate(x12, q({0})).has_value());
    const double p2 = dm.population(QubitIndex{0}, 2);
    REQUIRE(p2 == Approx(std::pow(std::sin(0.6), 2)).margin(1e-12));
    REQUIRE(dm.applyChannel(depolarizing(0.2), q({0})).has_value());
    REQUIRE(dm.stateNorm() == Approx(1.0).margin(1e-12));           // trace preserving on the full site
    REQUIRE(dm.population(QubitIndex{0}, 2) == Approx(p2).margin(1e-12)); // identity on |2>
    REQUIRE(dm.population(QubitIndex{0}, 1) == Approx((1.0 - p2) * 2.0 * 0.2 / 3.0).margin(1e-12));
}

TEST_CASE("DM: projective measurement probabilities and post-measurement state") {
    const double p1 = 0.7;
    DensityMatrixBackend dm;
    prepareCorrelated(dm, p1);
    auto m0 = dm.probabilities(q({0}));
    REQUIRE((*m0)[1] == Approx(p1).margin(1e-12));
    auto m01 = dm.probabilities(q({0, 1}));
    REQUIRE((*m01)[0] == Approx(1 - p1).margin(1e-12));
    REQUIRE((*m01)[3] == Approx(p1).margin(1e-12));
    REQUIRE(std::abs((*m01)[1]) + std::abs((*m01)[2]) < 1e-12);
    auto m20 = dm.probabilities(q({2, 0})); // index bit 0 = q2, bit 1 = q0
    REQUIRE((*m20)[0] == Approx(0.5 * (1 - p1)).margin(1e-12));
    REQUIRE((*m20)[1] == Approx(0.5 * (1 - p1)).margin(1e-12));
    REQUIRE((*m20)[2] == Approx(0.5 * p1).margin(1e-12));
    REQUIRE((*m20)[3] == Approx(0.5 * p1).margin(1e-12));
    // Collapse: measuring (q0, q1) leaves |bb><bb| (x) |+><+| and reports the branch probability.
    std::uint64_t ones = 0;
    const std::uint64_t shots = 4000;
    for (std::uint64_t s = 0; s < shots; ++s) {
        core::Random rng(1000 + s);
        auto copy = dm.clone();
        auto out = copy->measure(q({0, 1}), rng);
        REQUIRE(out.has_value());
        REQUIRE(out->bits.size() == 2);
        REQUIRE(out->bits[0] == out->bits[1]);
        REQUIRE(out->probability == Approx(out->bits[0] ? p1 : 1 - p1).margin(1e-12));
        ones += out->bits[0];
        if (s < 8) {
            auto& post = static_cast<DensityMatrixBackend&>(*copy);
            StateVectorBackend ref;
            REQUIRE(ref.allocate(3).has_value());
            if (out->bits[0]) { REQUIRE(ref.applyGate(X(), q({0})).has_value()); REQUIRE(ref.applyGate(X(), q({1})).has_value()); }
            REQUIRE(ref.applyGate(H(), q({2})).has_value());
            REQUIRE(maxAbsDiff(post.rho(), num::projector(ref.amplitudes())) < 1e-12);
            REQUIRE(post.stateNorm() == Approx(1.0).margin(1e-12));
        }
    }
    REQUIRE(withinSigma(ones, shots, p1, 5.0));
    REQUIRE(dm.purity() == Approx(1.0).margin(1e-12)); // the original was never touched
    // Mixed input: depolarize q0 of a Bell pair, measure q0 = b; then P(q1 != b) = 2p/3.
    const double p = 0.24;
    DensityMatrixBackend bell;
    REQUIRE(bell.allocate(2).has_value());
    REQUIRE(bell.applyGate(H(), q({0})).has_value());
    REQUIRE(bell.applyGate(CX(), q({0, 1})).has_value());
    REQUIRE(bell.applyChannel(depolarizing(p), q({0})).has_value());
    core::Random rng(5);
    auto out = bell.measure(q({0}), rng);
    REQUIRE(out.has_value());
    REQUIRE(out->probability == Approx(0.5).margin(1e-12));
    REQUIRE(bell.population(QubitIndex{1}, out->bits[0] ? 0u : 1u) == Approx(2.0 * p / 3.0).margin(1e-12));
    REQUIRE(bell.stateNorm() == Approx(1.0).margin(1e-12));
    REQUIRE(bell.measure(q({2}), rng).error().code == err::BadTargets);
}

TEST_CASE("DM: sampling uses the diagonal and does not mutate the state") {
    const double p1 = 0.3;
    DensityMatrixBackend dm;
    prepareCorrelated(dm, p1);
    REQUIRE(dm.applyChannel(phaseFlip(0.5), q({0})).has_value()); // kill the coherence: classical mixture
    REQUIRE(std::abs(dm.rho()(0, 3)) < 1e-15);
    const Matrix before = dm.rho();
    core::Random rng(321);
    const std::uint64_t shots = 20000;
    auto counts = dm.sample(q({0, 1}), shots, rng);
    REQUIRE(counts.has_value());
    REQUIRE(counts->size() == 2);
    REQUIRE(withinSigma((*counts)["11"], shots, p1, 5.0));
    REQUIRE((*counts)["00"] + (*counts)["11"] == shots);
    auto marg = dm.sample(q({2}), shots, rng);
    REQUIRE(withinSigma((*marg)["1"], shots, 0.5, 5.0));
    REQUIRE(maxAbsDiff(dm.rho(), before) == 0.0);
    // Keys are MSB-first over the requested qubits: |q1 q0> = |01> reads "01", and "10" when the
    // request is (q1, q0).
    DensityMatrixBackend basis;
    REQUIRE(basis.allocate(2).has_value());
    REQUIRE(basis.applyGate(X(), q({0})).has_value());
    REQUIRE((*basis.sample(q({0, 1}), 10, rng))["01"] == 10);
    REQUIRE((*basis.sample(q({1, 0}), 10, rng))["10"] == 10);
    // An empty request means every qubit in index order (as on the time-domain backends).
    auto all = basis.sample({}, 10, rng);
    REQUIRE(all.has_value());
    REQUIRE((*all)["01"] == 10);
    auto pAll = basis.probabilities({});
    REQUIRE(pAll.has_value());
    REQUIRE(pAll->size() == 4);
    REQUIRE((*pAll)[1] == Approx(1.0).margin(1e-15));
}
