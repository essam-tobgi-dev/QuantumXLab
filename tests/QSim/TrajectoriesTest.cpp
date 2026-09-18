// Spec 07 §5.1, §9, spec 25 §3.5, T04 §7 — Monte-Carlo wave-function backend against the Lindblad
// backend, exact unitary limit, seeded reproducibility, end-of-run record and validation.
#include "TimeDomain.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qtest;
using Catch::Approx;

namespace {
constexpr double kOmega = kTwoPi * 20e6;

// Resonantly driven qubit, H = (Ω/2)X, with T1 and pure dephasing (rates scaled for a 30 ns run).
SystemModel drivenDampedQubit(double gamma, double gammaPhi) {
    SystemModel m = bareModel(1, 2);
    m.drives.push_back(ladderDrive(m, 0, [](double) { return Complex(0.5 * kOmega, 0.0); }));
    m.collapse.push_back(decay(m, 0, gamma));
    m.collapse.push_back(dephasing(m, 0, gammaPhi));
    return m;
}

TrajectoriesBackend ensemble(const SystemModel& m, double duration, std::uint64_t seed, TrajectorySettings s) {
    TrajectoriesBackend tb;
    tb.setSettings(s);
    REQUIRE(tb.setModel(m).has_value());
    core::Random rng(seed);
    REQUIRE(tb.runEnsemble(duration, rng).has_value());
    return tb;
}
} // namespace

TEST_CASE("Trajectories: 3000 seeded trajectories match the Lindblad populations within 3 sigma") {
    const SystemModel m = drivenDampedQubit(1.0 / 15e-9, 1.0 / 40e-9);
    const double duration = 30e-9;
    LindbladBackend lb;
    REQUIRE(lb.setModel(m).has_value());
    REQUIRE(lb.evolve(duration).has_value());
    const auto tb = ensemble(m, duration, 2024, TrajectorySettings{3000, 50e-12, 1e-9, true});
    REQUIRE(tb.samples().size() == 31);
    REQUIRE(tb.samples().back().timeS == duration);
    // Spec 07 §9: populations at t_end within 3σ, σ the reported standard error of the mean.
    const double sigma = tb.populationStdErr(0, 1);
    REQUIRE(sigma > 0.0);
    REQUIRE(sigma < 0.01);
    REQUIRE(std::abs(tb.population(0, 1) - lb.population(0, 1)) < 3.0 * sigma);
    REQUIRE(tb.population(0, 0) + tb.population(0, 1) == Approx(1.0).margin(1e-12));
    // Along the run the damped Rabi oscillation is tracked at every 5 ns sample as well.
    for (std::size_t k = 5; k <= 30; k += 5) {
        const auto& s = tb.samples()[k];
        INFO("t = " << s.timeS);
        REQUIRE(s.timeS == Approx(double(k) * 1e-9).margin(1e-18));
        REQUIRE(std::abs(s.populations[1] - lb.trajectory()[k].populations[1]) < 3.0 * s.stderrs[1]);
    }
}

TEST_CASE("Trajectories: zero rates give the exact unitary evolution") {
    // Collapse operators present but with zero rate: no jump can happen, every trajectory is the same
    // pure-state evolution and the standard error vanishes.
    const SystemModel m = drivenDampedQubit(0.0, 0.0);
    const double duration = 20e-9;
    const auto tb = ensemble(m, duration, 7, TrajectorySettings{25, 10e-12, 1e-9, true});
    REQUIRE(tb.samples().size() == 21);
    for (const auto& s : tb.samples()) {
        INFO("t = " << s.timeS);
        REQUIRE(s.populations[1] == Approx(std::pow(std::sin(0.5 * kOmega * s.timeS), 2)).margin(1e-9));
        REQUIRE(s.stderrs[1] == 0.0); // identical trajectories: no statistical spread at all
    }
    REQUIRE(tb.stateNorm() == Approx(1.0).margin(1e-12));
    // The final trajectory state is the pure state RX(Ωt)|0⟩.
    StateVectorBackend sv;
    REQUIRE(sv.allocate(1).has_value());
    REQUIRE(sv.applyGate(RX(kOmega * duration), q({0})).has_value());
    auto snap = tb.snapshot(SnapshotRequest{true, true, false, {}, false});
    REQUIRE(snap.has_value());
    REQUIRE(snap->cls == FidelityClass::Statistical);
    REQUIRE(std::norm(num::dot(sv.amplitudes(), *snap->amplitudes)) == Approx(1.0).margin(1e-9));
}

TEST_CASE("Trajectories: runs are bit-identical per seed and differ between seeds") {
    const SystemModel m = drivenDampedQubit(1.0 / 10e-9, 1.0 / 20e-9);
    const TrajectorySettings s{200, 50e-12, 1e-9, true};
    const auto a = ensemble(m, 12e-9, 99, s), b = ensemble(m, 12e-9, 99, s), c = ensemble(m, 12e-9, 100, s);
    REQUIRE(a.samples().size() == b.samples().size());
    bool anyDifferent = false;
    for (std::size_t k = 0; k < a.samples().size(); ++k) {
        REQUIRE(a.samples()[k].populations == b.samples()[k].populations); // exact, not approximate
        REQUIRE(a.samples()[k].stderrs == b.samples()[k].stderrs);
        anyDifferent = anyDifferent || a.samples()[k].populations != c.samples()[k].populations;
    }
    REQUIRE(anyDifferent);
    auto sa = a.snapshot(SnapshotRequest{true, false, false, {}, false});
    auto sb = b.snapshot(SnapshotRequest{true, false, false, {}, false});
    REQUIRE(*sa->amplitudes == *sb->amplitudes);
    // Trajectory i draws from stream(i): a run of N trajectories is a prefix of a run of 2N... in the
    // sense that the same seed and N give the same ensemble whatever else ran before on the backend.
    TrajectoriesBackend reused;
    reused.setSettings(s);
    REQUIRE(reused.setModel(m).has_value());
    core::Random warm(5);
    REQUIRE(reused.runEnsemble(3e-9, warm).has_value());
    core::Random seed99(99);
    REQUIRE(reused.runEnsemble(12e-9, seed99).has_value());
    REQUIRE(reused.samples().back().populations == a.samples().back().populations);
}

TEST_CASE("Trajectories: the end of the run is always recorded") {
    const SystemModel m = drivenDampedQubit(1.0 / 10e-9, 0.0);
    const double duration = 7.5e-9; // not a multiple of the 1 ns grid
    auto recorded = ensemble(m, duration, 3, TrajectorySettings{100, 25e-12, 1e-9, true});
    auto endOnly = ensemble(m, duration, 3, TrajectorySettings{100, 25e-12, 1e-9, false});
    REQUIRE(recorded.samples().size() == 9); // 0, 1, …, 7 ns and 7.5 ns
    REQUIRE(recorded.samples().back().timeS == duration);
    REQUIRE(endOnly.samples().size() == 2);
    REQUIRE(endOnly.samples().front().timeS == 0.0);
    REQUIRE(endOnly.samples().back().timeS == duration);
    // Recording does not consume randomness or alter the integration grid.
    REQUIRE(endOnly.population(0, 1) == recorded.population(0, 1));
    REQUIRE(endOnly.populationStdErr(0, 1) == recorded.populationStdErr(0, 1));
    REQUIRE(recorded.population(0, 1) > 0.05); // the drive has moved population in 7.5 ns
    LindbladBackend lb;
    REQUIRE(lb.setModel(m).has_value());
    REQUIRE(lb.evolve(duration).has_value());
    REQUIRE(std::abs(recorded.population(0, 1) - lb.population(0, 1)) < 3.0 * recorded.populationStdErr(0, 1));
}

TEST_CASE("Trajectories: caps and argument validation") {
    TrajectoriesBackend tb;
    core::Random rng(1);
    REQUIRE(tb.capabilities().maxQubits == 8);
    REQUIRE(tb.runEnsemble(1e-9, rng).error().code == err::NotAllocated);
    REQUIRE(tb.expectation(*PauliString::parse("Z")).error().code == err::NotAllocated);
    REQUIRE(tb.allocate(9).error().code == err::TooLarge);
    REQUIRE(tb.allocate(2, 0).error().code == err::Unsupported);
    REQUIRE(tb.allocate(6, 3).error().code == err::TooLarge); // 729 > 256
    REQUIRE(tb.allocate(8, 2).has_value());
    REQUIRE(tb.allocate(5, 3).has_value());
    // A rejected model leaves the installed one usable.
    const SystemModel good = drivenDampedQubit(1.0 / 10e-9, 0.0);
    REQUIRE(tb.setModel(good).has_value());
    SystemModel wrongDrive = bareModel(2, 2);
    wrongDrive.drives.push_back(ladderDrive(bareModel(1, 2), 0, [](double) { return Complex(1.0); }));
    REQUIRE(tb.setModel(wrongDrive).error().code == err::BadTargets);
    SystemModel wrongCollapse = bareModel(1, 2);
    wrongCollapse.collapse.push_back(decay(bareModel(2, 2), 0, 1.0));
    REQUIRE(tb.setModel(wrongCollapse).error().code == err::BadTargets);
    tb.setSettings(TrajectorySettings{10, 50e-12, 1e-9, true});
    REQUIRE(tb.runEnsemble(2e-9, rng).has_value());
    REQUIRE(tb.nQubits() == 1);
    REQUIRE(tb.runEnsemble(-1.0, rng).error().code == ErrorCode::InvalidArgument);
    REQUIRE(tb.applyGate(X(), q({0})).error().code == err::Unsupported);
    REQUIRE(tb.applyChannel(depolarizing(0.1), q({0})).error().code == err::Unsupported);
    REQUIRE(tb.probabilities(q({1})).error().code == err::BadTargets);
    REQUIRE(tb.measure({}, rng)->bits.empty());
}
