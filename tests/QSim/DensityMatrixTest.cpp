// Spec 07 §3, §9 — density-matrix backend: reference states, agreement with the state vector,
// multi-level sites and the memory guard.
#include "Circuits.hpp"
#include "Numerics/Checks.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qtest;
using Catch::Approx;

namespace {
Matrix pureProjector(const StateVectorBackend& sv) {
    return num::projector(sv.amplitudes());
}
} // namespace

TEST_CASE("DM: Bell and GHZ density matrices") {
    DensityMatrixBackend bell;
    REQUIRE(bell.allocate(2).has_value());
    REQUIRE(bell.applyGate(H(), q({0})).has_value());
    REQUIRE(bell.applyGate(CX(), q({0, 1})).has_value());
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < 4; ++j) {
            const bool corner = (i == 0 || i == 3) && (j == 0 || j == 3);
            INFO("rho(" << i << "," << j << ")");
            REQUIRE(std::abs(bell.rho()(i, j) - Complex(corner ? 0.5 : 0.0)) < 1e-14);
        }
    REQUIRE(bell.stateNorm() == Approx(1.0).margin(1e-14));
    REQUIRE(bell.purity() == Approx(1.0).margin(1e-14));

    const std::uint32_t n = 5;
    DensityMatrixBackend ghz;
    REQUIRE(ghz.allocate(n).has_value());
    REQUIRE(ghz.applyGate(H(), q({0})).has_value());
    for (std::uint32_t i = 0; i + 1 < n; ++i)
        REQUIRE(ghz.applyGate(CX(), q({i, i + 1})).has_value());
    const std::size_t last = (std::size_t{1} << n) - 1;
    double offCorner = 0;
    for (std::size_t i = 0; i <= last; ++i)
        for (std::size_t j = 0; j <= last; ++j) {
            const bool corner = (i == 0 || i == last) && (j == 0 || j == last);
            if (!corner)
                offCorner = std::max(offCorner, std::abs(ghz.rho()(i, j)));
        }
    REQUIRE(offCorner < 1e-14);
    REQUIRE(std::abs(ghz.rho()(0, 0) - Complex(0.5)) < 1e-14);
    REQUIRE(std::abs(ghz.rho()(0, last) - Complex(0.5)) < 1e-14);
    REQUIRE(std::abs(ghz.rho()(last, 0) - Complex(0.5)) < 1e-14);
    REQUIRE(std::abs(ghz.rho()(last, last) - Complex(0.5)) < 1e-14);
    // Any single qubit of a GHZ state is maximally mixed; the pair {0,1} is classically correlated.
    auto one = ghz.reducedDensityMatrix(q({2}));
    REQUIRE(one.has_value());
    REQUIRE(measures::entropyBits(*one) == Approx(1.0).margin(1e-12));
    auto two = ghz.reducedDensityMatrix(q({0, 1}));
    REQUIRE(two.has_value());
    REQUIRE(std::abs((*two)(0, 0) - Complex(0.5)) < 1e-14);
    REQUIRE(std::abs((*two)(3, 3) - Complex(0.5)) < 1e-14);
    REQUIRE(std::abs((*two)(0, 3)) < 1e-14);
}

TEST_CASE("DM agrees with the state vector on seeded random 6-qubit circuits") {
    const std::uint32_t n = 6;
    for (std::uint64_t seed : {101u, 202u, 303u, 404u}) {
        INFO("seed " << seed);
        const Circuit c = randomUniversal(n, 60, seed);
        StateVectorBackend sv;
        DensityMatrixBackend dm;
        REQUIRE(sv.allocate(n).has_value());
        REQUIRE(dm.allocate(n).has_value());
        REQUIRE(applyAll(sv, c).has_value());
        REQUIRE(applyAll(dm, c).has_value());
        // Spec 07 §9: max |ρ_DM − |ψ⟩⟨ψ|| < 1e-10.
        REQUIRE(maxAbsDiff(dm.rho(), pureProjector(sv)) < 1e-10);
        REQUIRE(dm.stateNorm() == Approx(1.0).margin(1e-12));
        REQUIRE(dm.purity() == Approx(1.0).margin(1e-12));
        REQUIRE(maxHermitianDefect(dm.rho()) < 1e-12);
        // Derived quantities agree as well: marginals, reduced states, Pauli expectations.
        auto pSv = sv.probabilities(q({4, 1, 3}));
        auto pDm = dm.probabilities(q({4, 1, 3}));
        REQUIRE(pSv.has_value());
        REQUIRE(pDm.has_value());
        for (std::size_t i = 0; i < pSv->size(); ++i)
            REQUIRE((*pDm)[i] == Approx((*pSv)[i]).margin(1e-12));
        auto rSv = sv.reducedDensityMatrix(q({0, 5}));
        auto rDm = dm.reducedDensityMatrix(q({0, 5}));
        REQUIRE(rSv.has_value());
        REQUIRE(rDm.has_value());
        REQUIRE(maxAbsDiff(*rSv, *rDm) < 1e-12);
        for (const char* label : {"ZIIIII", "IXIIYI", "YYZXIZ", "XXXXXX", "IIZZII"}) {
            INFO(label);
            auto p = PauliString::parse(label);
            REQUIRE(p.has_value());
            REQUIRE(dm.expectation(*p).value() == Approx(sv.expectation(*p).value()).margin(1e-12));
        }
    }
}

TEST_CASE("DM: controlled gates equal the state-vector controlled kernel") {
    StateVectorBackend sv;
    DensityMatrixBackend dm;
    REQUIRE(sv.allocate(4).has_value());
    REQUIRE(dm.allocate(4).has_value());
    for (IBackend* b : {static_cast<IBackend*>(&sv), static_cast<IBackend*>(&dm)}) {
        for (std::uint32_t i = 0; i < 4; ++i)
            REQUIRE(b->applyGate(RY(0.4 + 0.3 * i), q({i})).has_value());
        REQUIRE(b->applyControlled(RX(1.1), q({3, 0}), q({2})).has_value());
        REQUIRE(b->applyControlled(SWAP(), q({1}), q({0, 3})).has_value()); // Fredkin
    }
    REQUIRE(maxAbsDiff(dm.rho(), pureProjector(sv)) < 1e-12);
    // A control that is also a target is a caller error on both backends.
    REQUIRE(sv.applyControlled(X(), q({1}), q({1})).error().code == err::BadTargets);
    REQUIRE(dm.applyControlled(X(), q({1}), q({1})).error().code == err::BadTargets);
}

TEST_CASE("DM: a d = 3 site keeps |2> empty under qubit-subspace gates") {
    // Spec 07 §3.2: a qubit gate on a d = 3 site acts on {|0>,|1>} and as identity on |2>.
    const std::vector<std::uint32_t> dims{3, 2, 3};
    DensityMatrixBackend mixed, qubits;
    REQUIRE(mixed.allocateMixed(dims).has_value());
    REQUIRE(qubits.allocate(3).has_value());
    REQUIRE(mixed.dim() == 18);
    REQUIRE(mixed.levels() == 3);
    Circuit c = randomUniversal(3, 40, 77);
    REQUIRE(applyAll(mixed, c).has_value());
    REQUIRE(applyAll(qubits, c).has_value());
    for (std::uint32_t site : {0u, 2u}) {
        INFO("site " << site);
        REQUIRE(std::abs(mixed.population(QubitIndex{site}, 2)) < 1e-15);
        REQUIRE(mixed.population(QubitIndex{site}, 1) ==
                Approx(qubits.population(QubitIndex{site}, 1)).margin(1e-12));
    }
    REQUIRE(mixed.stateNorm() == Approx(1.0).margin(1e-12));
    auto pm = mixed.probabilities(allQubits(3));
    auto pq = qubits.probabilities(allQubits(3));
    REQUIRE(pm.has_value());
    REQUIRE(pq.has_value());
    for (std::size_t i = 0; i < 8; ++i)
        REQUIRE((*pm)[i] == Approx((*pq)[i]).margin(1e-12));
    for (const char* label : {"ZII", "XYZ", "YIX"}) {
        auto p = PauliString::parse(label);
        REQUIRE(mixed.expectation(*p).value() ==
                Approx(qubits.expectation(*p).value()).margin(1e-12));
    }
    // A full-dimension 3x3 unitary does reach |2>: the |1> <-> |2> swap moves P1 into P2.
    const double p1 = mixed.population(QubitIndex{0}, 1);
    Matrix x12(3, 3);
    x12(0, 0) = 1;
    x12(1, 2) = 1;
    x12(2, 1) = 1;
    REQUIRE(mixed.applyGate(x12, q({0})).has_value());
    REQUIRE(mixed.population(QubitIndex{0}, 2) == Approx(p1).margin(1e-12));
    REQUIRE(mixed.population(QubitIndex{0}, 1) == Approx(0.0).margin(1e-15));
}

TEST_CASE("DM: memory guard and argument validation") {
    DensityMatrixBackend dm;
    auto big = dm.allocate(20);
    REQUIRE_FALSE(big.has_value());
    REQUIRE(big.error().code == err::TooLarge);
    // Spec 07 §3.1: the cap is n <= 13 for d = 2 whatever the machine has installed.
    auto fourteen = dm.allocate(14);
    REQUIRE_FALSE(fourteen.has_value());
    REQUIRE(fourteen.error().code == err::TooLarge);
    REQUIRE_FALSE(dm.allocate(64).has_value());   // 2^64 must not wrap around to a tiny allocation
    REQUIRE_FALSE(dm.allocate(9, 3).has_value()); // 3^9 = 19683 > 2^13
    REQUIRE(DensityMatrixBackend::maxQubits() <= 13);
    REQUIRE(dm.capabilities().maxQubits == DensityMatrixBackend::maxQubits());
    REQUIRE(dm.capabilities().exactNoise);
    REQUIRE(dm.capabilities().multiLevel);
    REQUIRE(dm.allocate(2, 4).error().code == err::Unsupported);
    // Use before allocation and bad targets are reported, not crashed on.
    DensityMatrixBackend fresh;
    REQUIRE(fresh.applyGate(X(), q({0})).error().code == err::NotAllocated);
    REQUIRE(dm.allocate(3).has_value());
    REQUIRE(dm.applyGate(X(), q({3})).error().code == err::BadTargets);
    REQUIRE(dm.applyGate(CX(), q({1, 1})).error().code == err::BadTargets);
    REQUIRE(dm.applyGate(CX(), q({1})).error().code == err::BadTargets);
    REQUIRE(dm.probabilities(q({7})).error().code == err::BadTargets);
    REQUIRE(dm.reducedDensityMatrix(q({5})).error().code == err::BadTargets);
#ifdef QXL_DEV
    // Spec 07 §1: applyGate validates unitarity to 1e-10 in developer builds.
    REQUIRE(dm.applyGate(mat2(1, 0, 0, 0.5), q({0})).error().code == err::NotUnitary);
    REQUIRE(dm.stateNorm() == Approx(1.0).margin(1e-15));
#endif
}
