// Spec 08 §1, §9 / spec 25 §3.4 — CPTP validation of every catalogue channel across parameter sweeps.
#include "Core/Random.hpp"
#include "Noise/Noise.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Tensor.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

using namespace qlab;
using namespace qlab::noise;
using Catch::Approx;
using num::Complex;
using num::Matrix;

namespace {
// max |Σ K†K − I|, computed independently of Kraus::validate.
double cptpDefect(const Kraus& k) {
    const std::size_t d = k.dim();
    Matrix sum(d, d);
    for (const auto& K : k.ops) sum += num::matmul(num::adjoint(K), K);
    double worst = 0.0;
    for (std::size_t i = 0; i < d; ++i)
        for (std::size_t j = 0; j < d; ++j)
            worst = std::max(worst, std::abs(sum(i, j) - (i == j ? Complex(1, 0) : Complex(0, 0))));
    return worst;
}
void requireCptp(const Result<Kraus>& k, const std::string& what) {
    INFO(what);
    REQUIRE(k);
    REQUIRE(k->validate());
    REQUIRE(num::isTracePreserving(k->ops, num::tol::kTraceTol));
    REQUIRE(cptpDefect(*k) < 1e-12); // spec 08 §1 asks for 1e-12
    for (const auto& K : k->ops) { REQUIRE(K.rows == k->dim()); REQUIRE(K.cols == k->dim()); }
}
} // namespace

TEST_CASE("every catalogue channel is CPTP across 20 seeded parameter sets") {
    core::Random rng(0x08C0FFEEull);
    for (int trial = 0; trial < 20; ++trial) {
        const double p = rng.uniform(), q = rng.uniform();
        const double t1 = rng.uniform(20e-6, 300e-6);
        const double t2 = rng.uniform(0.05, 2.0) * t1; // up to and including the 2·T1 limit region
        const double t = rng.uniform(0.0, 3.0) * t1;
        const double pth = rng.uniform(0.0, 0.5);
        INFO("trial " << trial << " p=" << p << " q=" << q << " t1=" << t1 << " t2=" << t2 << " t=" << t);
        requireCptp(channels::bitFlip(p), "bit_flip");
        requireCptp(channels::phaseFlip(p), "phase_flip");
        requireCptp(channels::bitPhaseFlip(p), "bit_phase_flip");
        requireCptp(channels::pauli(p / 3, q / 3, (1 - p) / 3), "pauli");
        requireCptp(channels::depolarizing1q(p), "depolarizing_1q");
        requireCptp(channels::depolarizing2q(p), "depolarizing_2q");
        requireCptp(channels::depolarizingNq(3, p), "depolarizing_nq(3)");
        requireCptp(channels::amplitudeDamping(p), "amplitude_damping");
        requireCptp(channels::generalizedAmplitudeDamping(p, pth), "generalized_amplitude_damping");
        requireCptp(channels::amplitudeDampingOver(t1, t, pth), "amplitude_damping(T1, t, p_th)");
        requireCptp(channels::phaseDamping(p), "phase_damping");
        requireCptp(channels::phaseDampingOver(t1, t), "phase_damping(T_phi, t)");
        requireCptp(channels::thermalRelaxation(t1, t2, t, pth), "thermal_relaxation");
        requireCptp(channels::overRotation("X", rng.uniform(-0.3, 0.3)), "over_rotation X");
        requireCptp(channels::overRotation("ZX", rng.uniform(-0.3, 0.3)), "over_rotation ZX");
        requireCptp(channels::detuningPhase(rng.uniform(-1e6, 1e6), t), "detuning_phase");
        requireCptp(channels::zzCrosstalk(rng.uniform(-3e5, 3e5), t), "zz_crosstalk");
        requireCptp(channels::leakage(p * 0.01, q * 0.01), "leakage");
        requireCptp(channels::resetError(p * 0.05), "reset_error");
        requireCptp(channels::measurementDephasing(t * 0.01, t2, q), "measurement_dephasing");
        requireCptp(channels::gaussianDephasing(rng.uniform(0.0, 2e4), t), "gaussian_dephasing");
    }
}

TEST_CASE("CPTP holds at the parameter boundaries") {
    for (double p : {0.0, 1.0}) {
        requireCptp(channels::bitFlip(p), "bit_flip edge");
        requireCptp(channels::depolarizing1q(p), "depolarizing_1q edge");
        requireCptp(channels::depolarizing2q(p), "depolarizing_2q edge");
        requireCptp(channels::amplitudeDamping(p), "amplitude_damping edge");
        requireCptp(channels::generalizedAmplitudeDamping(p, p), "gad edge");
        requireCptp(channels::phaseDamping(p), "phase_damping edge");
        requireCptp(channels::leakage(p, 1.0 - p), "leakage edge");
    }
    requireCptp(channels::thermalRelaxation(50e-6, 100e-6, 1e-6, 0.01), "T2 = 2 T1 exactly");
    requireCptp(channels::thermalRelaxation(50e-6, 70e-6, 0.0, 0.01), "zero duration");
    auto idle = channels::thermalRelaxation(50e-6, 70e-6, 0.0, 0.0);
    REQUIRE(idle);
    REQUIRE(idle->isIdentity());
}

TEST_CASE("Kraus construction rejects sets that are not trace preserving") {
    Matrix half = num::scale(Matrix::identity(2), Complex(0.5, 0.0));
    auto bad = Kraus::make({half}, 1);
    REQUIRE_FALSE(bad);
    REQUIRE(bad.error().code == err::NotTracePreserving);

    // Deviation just above the tolerance of num::tol::kTraceTol is refused, below it accepted.
    Matrix nearly = num::scale(Matrix::identity(2), Complex(std::sqrt(1.0 + 4e-10), 0.0));
    REQUIRE_FALSE(Kraus::make({nearly}, 1));
    Matrix close = num::scale(Matrix::identity(2), Complex(std::sqrt(1.0 + 1e-11), 0.0));
    REQUIRE(Kraus::make({close}, 1));

    auto wrongDim = Kraus::make({Matrix::identity(2)}, 2);
    REQUIRE_FALSE(wrongDim);
    REQUIRE(wrongDim.error().code == err::BadDimensions);
    REQUIRE_FALSE(Kraus::make({}, 1));
    REQUIRE_FALSE(Kraus::fromUnitary(half, 1));
}

TEST_CASE("catalogue parameters are range checked") {
    REQUIRE_FALSE(channels::bitFlip(-0.1));
    REQUIRE_FALSE(channels::bitFlip(1.1));
    REQUIRE_FALSE(channels::depolarizing1q(std::nan("")));
    REQUIRE_FALSE(channels::pauli(0.5, 0.4, 0.3));
    REQUIRE_FALSE(channels::amplitudeDampingOver(-1.0, 1e-6));
    REQUIRE_FALSE(channels::amplitudeDampingOver(50e-6, -1e-9));
    REQUIRE_FALSE(channels::leakage(1.5, 0.0));
    REQUIRE_FALSE(channels::overRotation("II", 0.1));
    REQUIRE_FALSE(channels::overRotation("Q", 0.1));
    auto unphysical = channels::thermalRelaxation(50e-6, 101e-6, 1e-6, 0.0); // T2 > 2 T1
    REQUIRE_FALSE(unphysical);
    REQUIRE(unphysical.error().code == err::Unphysical);
    REQUIRE(unphysical.error().message.find("2 T1") != std::string::npos);
}

TEST_CASE("Pauli structure is detected and carries the catalogue weights") {
    auto dep = channels::depolarizing1q(0.2);
    REQUIRE(dep);
    REQUIRE(dep->isPauli);
    REQUIRE(dep->ops.size() == 4);
    REQUIRE(dep->pauliWeights[0] == Approx(1.0 - 0.15).epsilon(1e-14)); // 1 − 3p/4
    for (int k = 1; k < 4; ++k) REQUIRE(dep->pauliWeights[k] == Approx(0.05).epsilon(1e-14));
    // Operators are exactly √w · P in the order I, X, Y, Z (spec 08 §2.1).
    REQUIRE(num::approxEqual(dep->ops[1], num::scale(pauliMatrix(1), Complex(std::sqrt(0.05), 0)), 1e-15));
    REQUIRE(num::approxEqual(dep->ops[2], num::scale(pauliMatrix(2), Complex(std::sqrt(0.05), 0)), 1e-15));
    REQUIRE(num::approxEqual(dep->ops[3], num::scale(pauliMatrix(3), Complex(std::sqrt(0.05), 0)), 1e-15));

    auto dep2 = channels::depolarizing2q(0.16);
    REQUIRE(dep2);
    REQUIRE(dep2->ops.size() == 16);
    REQUIRE(dep2->pauliWeights[0] == Approx(1.0 - 0.15).epsilon(1e-14)); // 1 − 15p/16
    REQUIRE(dep2->pauliWeights[7] == Approx(0.01).epsilon(1e-14));       // p/16

    // A hand-built set of scaled Paulis is recognised; amplitude damping is not a Pauli channel.
    auto manual = Kraus::make({num::scale(pauliMatrix(0), Complex(std::sqrt(0.7), 0)),
                               num::scale(pauliMatrix(2), Complex(0, std::sqrt(0.3)))}, 1);
    REQUIRE(manual);
    REQUIRE(manual->isPauli);
    REQUIRE(manual->pauliIndices == std::vector<std::uint32_t>{0, 2});
    REQUIRE(manual->pauliWeights[1] == Approx(0.3).epsilon(1e-14));
    auto ad = channels::amplitudeDamping(0.3);
    REQUIRE(ad);
    REQUIRE_FALSE(ad->isPauli);
    REQUIRE(ad->ops.size() == 2);

    // Coherent errors are single unitaries (spec 08 §1).
    auto rot = channels::overRotation("X", 0.05);
    REQUIRE(rot);
    REQUIRE(rot->isUnitary());
    REQUIRE_FALSE(rot->isPauli);
}

TEST_CASE("Pauli string helpers follow the little-endian target order") {
    // Index digit k acts on targets[k]; the matrix has targets[0] as the least significant factor.
    auto idx = pauliStringIndex("ZX"); // Z on targets[0], X on targets[1]
    REQUIRE(idx);
    REQUIRE(*idx == (3u | (1u << 2)));
    REQUIRE(pauliStringLabel(*idx, 2) == "XZ"); // MSB-first label, like qsim::PauliString
    Matrix expected = num::kron(pauliMatrix(1), pauliMatrix(3)); // X (high) ⊗ Z (low)
    REQUIRE(num::approxEqual(pauliStringMatrix(*idx, 2), expected, 1e-15));
}
