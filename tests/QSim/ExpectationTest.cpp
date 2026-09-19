// Spec 07 §1, §8, T11 §2.8 — Pauli expectations and reduced states on every backend that supports
// them, with the most-significant-qubit-first label convention.
#include "TimeDomain.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qtest;
using Catch::Approx;

namespace {
struct Oracle {
    const char* label;
    double value;
};

// |Φ+⟩ = (|00⟩ + |11⟩)/√2 is the +1 eigenstate of XX and ZZ and the −1 eigenstate of YY (T09 §1.2).
constexpr Oracle kBell[] = {{"II", 1.0}, {"ZZ", 1.0}, {"XX", 1.0}, {"YY", -1.0}, {"-ZZ", -1.0},
                            {"ZI", 0.0}, {"IZ", 0.0}, {"XI", 0.0}, {"IY", 0.0},  {"XY", 0.0},
                            {"YX", 0.0}, {"ZX", 0.0}, {"XZ", 0.0}};
// |q1 q0⟩ = |0⟩ ⊗ |+i⟩: Z on q1 reads +1, Y on q0 reads +1, and the labels say which is which.
constexpr Oracle kPlusI[] = {{"ZI", 1.0}, {"IY", 1.0}, {"ZY", 1.0},
                             {"IZ", 0.0}, {"YI", 0.0}, {"IX", 0.0}};

void check(const IBackend& b, std::span<const Oracle> oracles, double tol, const char* what) {
    for (const auto& o : oracles) {
        INFO(what << " ⟨" << o.label << "⟩");
        auto p = PauliString::parse(o.label);
        REQUIRE(p.has_value());
        auto value = b.expectation(*p);
        REQUIRE(value.has_value());
        REQUIRE(*value == Approx(o.value).margin(tol));
    }
}

// H = J (Y_q1 ⊗ X_q0) carries |00⟩ to cos(JT)|00⟩ + sin(JT)|11⟩, so JT = π/4 prepares |Φ+⟩ on a
// time-domain backend (num::kron puts the left factor on the more significant site).
SystemModel bellHamiltonian(double duration) {
    SystemModel m = bareModel(2, 2);
    m.h0 = num::kron(Y(), X());
    m.h0 *= std::numbers::pi / (4.0 * duration);
    return m;
}
} // namespace

TEST_CASE("Pauli expectations agree with the closed forms on every backend") {
    const double s = 1.0 / std::sqrt(2.0);
    const Circuit bell{{"h", H(), q({0}), {}}, {"cx", CX(), q({0, 1}), {}}};
    const Circuit plusI{{"h", H(), q({0}), {}}, {"s", S(), q({0}), {}}};

    StateVectorBackend sv;
    REQUIRE(sv.allocate(2).has_value());
    REQUIRE(applyAll(sv, bell).has_value());
    check(sv, kBell, 1e-13, "state vector");

    DensityMatrixBackend dm;
    REQUIRE(dm.allocate(2).has_value());
    REQUIRE(applyAll(dm, bell).has_value());
    check(dm, kBell, 1e-13, "density matrix");

    DensityMatrixBackend mixed; // qubit gates on d = 3 sites keep the computational subspace
    REQUIRE(mixed.allocateMixed(std::vector<std::uint32_t>{3, 3}).has_value());
    REQUIRE(applyAll(mixed, bell).has_value());
    check(mixed, kBell, 1e-13, "density matrix (d = 3)");

    StabilizerBackend st;
    REQUIRE(st.allocate(2).has_value());
    REQUIRE(applyAll(st, bell).has_value());
    check(st, kBell, 1e-13, "stabilizer");

    LindbladBackend lb;
    REQUIRE(lb.allocate(2).has_value());
    REQUIRE(lb.setPure(std::vector<Complex>{s, 0.0, 0.0, s}).has_value());
    check(lb, kBell, 1e-13, "Lindblad");

    LindbladBackend transmons; // the same state embedded in two d = 3 sites
    REQUIRE(transmons.allocate(2, 3).has_value());
    REQUIRE(transmons.setPure(std::vector<Complex>{s, 0.0, 0.0, s}).has_value());
    check(transmons, kBell, 1e-13, "Lindblad (d = 3)");

    // Time-domain preparation of the same state on both pulse-level backends.
    const double duration = 5e-9;
    LindbladBackend evolvedLb;
    REQUIRE(evolvedLb.setModel(bellHamiltonian(duration)).has_value());
    REQUIRE(evolvedLb.evolve(duration).has_value());
    check(evolvedLb, kBell, 1e-9, "Lindblad (evolved)");

    TrajectoriesBackend tb;
    tb.setSettings(TrajectorySettings{1, 10e-12, 1e-9, true});
    REQUIRE(tb.setModel(bellHamiltonian(duration)).has_value());
    core::Random rng(5);
    REQUIRE(tb.runEnsemble(duration, rng).has_value());
    check(tb, kBell, 1e-9, "trajectories");

    // Label order and the sign of Y: |0⟩ ⊗ |+i⟩ on (q1, q0).
    StateVectorBackend svY;
    REQUIRE(svY.allocate(2).has_value());
    REQUIRE(applyAll(svY, plusI).has_value());
    check(svY, kPlusI, 1e-13, "state vector");
    DensityMatrixBackend dmY;
    REQUIRE(dmY.allocate(2).has_value());
    REQUIRE(applyAll(dmY, plusI).has_value());
    check(dmY, kPlusI, 1e-13, "density matrix");
    StabilizerBackend stY;
    REQUIRE(stY.allocate(2).has_value());
    REQUIRE(applyAll(stY, plusI).has_value());
    check(stY, kPlusI, 1e-13, "stabilizer");
    LindbladBackend lbY;
    REQUIRE(lbY.allocate(2).has_value());
    REQUIRE(lbY.setPure(std::vector<Complex>{s, Complex(0, s), 0.0, 0.0}).has_value());
    check(lbY, kPlusI, 1e-13, "Lindblad");
    TrajectoriesBackend tbY;
    tbY.setSettings(TrajectorySettings{1, 10e-12, 1e-9, true});
    SystemModel yModel = bareModel(2, 2);
    yModel.h0 = num::kron(I2(), X());                  // X on q0
    yModel.h0 *= -std::numbers::pi / (4.0 * duration); // e^{+iπX/4}|0⟩ = |+i⟩
    REQUIRE(tbY.setModel(yModel).has_value());
    REQUIRE(tbY.runEnsemble(duration, rng).has_value());
    check(tbY, kPlusI, 1e-9, "trajectories");

    // A Pauli string of the wrong length is refused by every backend.
    auto oneQubit = PauliString::parse("Z");
    REQUIRE(sv.expectation(*oneQubit).error().code == err::BadPauli);
    REQUIRE(dm.expectation(*oneQubit).error().code == err::BadPauli);
    REQUIRE(st.expectation(*oneQubit).error().code == err::BadPauli);
    REQUIRE(lb.expectation(*oneQubit).error().code == err::BadPauli);
    REQUIRE(tb.expectation(*oneQubit).error().code == err::BadPauli);
    REQUIRE(PauliString::parse("XQ").error().code == err::BadPauli);
    REQUIRE(PauliString::parse("").error().code == err::BadPauli);
}

TEST_CASE("Reduced states are returned in the requested qubit order") {
    // |q2 q1 q0⟩ = |0⟩|1⟩|+⟩: ρ over (q0, q1) and over (q1, q0) are different matrices.
    const Circuit c{{"h", H(), q({0}), {}}, {"x", X(), q({1}), {}}};
    Matrix q0q1(4, 4), q1q0(4, 4);
    for (std::size_t i = 2; i < 4; ++i)
        for (std::size_t j = 2; j < 4; ++j)
            q0q1(i, j) = 0.5; // q1 = 1 is the high bit
    for (std::size_t i : {1u, 3u})
        for (std::size_t j : {1u, 3u})
            q1q0(i, j) = 0.5; // q1 = 1 is now the low bit
    StateVectorBackend sv;
    REQUIRE(sv.allocate(3).has_value());
    REQUIRE(applyAll(sv, c).has_value());
    REQUIRE(maxAbsDiff(*sv.reducedDensityMatrix(q({0, 1})), q0q1) < 1e-14);
    REQUIRE(maxAbsDiff(*sv.reducedDensityMatrix(q({1, 0})), q1q0) < 1e-14);
    DensityMatrixBackend dm;
    REQUIRE(dm.allocate(3).has_value());
    REQUIRE(applyAll(dm, c).has_value());
    REQUIRE(maxAbsDiff(*dm.reducedDensityMatrix(q({0, 1})), q0q1) < 1e-14);
    REQUIRE(maxAbsDiff(*dm.reducedDensityMatrix(q({1, 0})), q1q0) < 1e-14);
    // Mixed radix: a d = 3 site traced into a two-qubit reduced state keeps the same convention.
    DensityMatrixBackend mixed;
    REQUIRE(mixed.allocateMixed(std::vector<std::uint32_t>{2, 3, 2}).has_value());
    REQUIRE(applyAll(mixed, c).has_value());
    auto reduced = mixed.reducedDensityMatrix(q({1, 0}));
    REQUIRE(reduced.has_value());
    REQUIRE(reduced->rows == 6); // (q1 with 3 levels) ⊗ (q0)
    REQUIRE(std::abs((*reduced)(1, 1) - Complex(0.5)) < 1e-14);
    REQUIRE(std::abs((*reduced)(1, 4) - Complex(0.5)) < 1e-14);
    // Snapshots carry both the requested order and the matrix in that order.
    auto snap = sv.snapshot(SnapshotRequest{false, false, true, {q({1, 0}), q({0, 1})}, false});
    REQUIRE(snap.has_value());
    REQUIRE(snap->reduced.size() == 2);
    REQUIRE(snap->reduced[0].qubits == q({1, 0}));
    REQUIRE(maxAbsDiff(snap->reduced[0].rho, q1q0) < 1e-14);
    REQUIRE(maxAbsDiff(snap->reduced[1].rho, q0q1) < 1e-14);
    auto dmSnap = dm.snapshot(SnapshotRequest{false, false, true, {q({1, 0})}, false});
    REQUIRE(maxAbsDiff(dmSnap->reduced[0].rho, q1q0) < 1e-14);
    LindbladBackend lb;
    REQUIRE(lb.allocate(3).has_value());
    REQUIRE(lb.setPure(std::vector<Complex>{0.0, 0.0, 1.0 / std::sqrt(2.0), 1.0 / std::sqrt(2.0),
                                            0.0, 0.0, 0.0, 0.0})
                .has_value());
    auto lbSnap = lb.snapshot(SnapshotRequest{false, false, true, {q({1, 0})}, false});
    REQUIRE(lbSnap.has_value());
    REQUIRE(maxAbsDiff(lbSnap->reduced[0].rho, q1q0) < 1e-14);
    REQUIRE(lb.snapshot(SnapshotRequest{false, false, true, {q({1, 1})}, false}).error().code ==
            err::BadTargets);
    REQUIRE(sv.reducedDensityMatrix(q({3})).error().code == err::BadTargets);
}
