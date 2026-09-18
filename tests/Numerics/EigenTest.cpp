#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Numerics/Numerics.hpp"
#include "Core/Random.hpp"
using namespace qlab::num;
using Catch::Approx;

static Matrix randomHermitian(std::size_t n, qlab::core::Random& rng) {
    Matrix A(n, n);
    for (std::size_t i = 0; i < n; ++i) for (std::size_t j = 0; j < n; ++j) A(i, j) = Complex(rng.normal(), rng.normal());
    return scale(add(A, adjoint(A)), 0.5);
}

TEST_CASE("eigh reconstructs random Hermitian matrices, own path and Blas path") {
    qlab::core::Random rng(11);
    for (std::size_t n : {2u, 5u, 12u, 40u, 80u}) {
        Matrix H = randomHermitian(n, rng);
        for (bool forceOwn : {true, false}) {
            auto e = eigh(H, forceOwn);
            REQUIRE(e);
            for (std::size_t i = 1; i < n; ++i) REQUIRE(e->values[i - 1] <= e->values[i] + 1e-12);
            Matrix D(n, n); for (std::size_t i = 0; i < n; ++i) D(i, i) = e->values[i];
            Matrix R = matmul(matmul(e->vectors, D), adjoint(e->vectors));
            REQUIRE(maxAbsNorm(sub(R, H)) < 1e-9);
            REQUIRE(isUnitary(e->vectors, 1e-9));
        }
    }
}

TEST_CASE("svd reconstructs and orders singular values") {
    qlab::core::Random rng(3);
    Matrix A(6, 4);
    for (auto& v : A.data) v = Complex(rng.normal(), rng.normal());
    auto s = svd(A);
    REQUIRE(s);
    for (std::size_t i = 1; i < s->singular.size(); ++i) REQUIRE(s->singular[i - 1] >= s->singular[i] - 1e-12);
    std::size_t k = s->singular.size();
    Matrix S(k, k); for (std::size_t i = 0; i < k; ++i) S(i, i) = s->singular[i];
    Matrix R = matmul(matmul(s->U, S), adjoint(s->V));
    REQUIRE(maxAbsNorm(sub(R, A)) < 1e-9);
}

TEST_CASE("solve and inverse") {
    Matrix A = Matrix::fromRows({{4, 1}, {2, 3}});
    auto inv = inverse(A);
    REQUIRE(inv);
    REQUIRE(approxEqual(matmul(A, *inv), Matrix::identity(2), 1e-13));
    Matrix sing = Matrix::fromRows({{1, 2}, {2, 4}});
    REQUIRE_FALSE(inverse(sing).has_value());
}

TEST_CASE("structure checks and distances") {
    Matrix X = pauli::X.toMatrix(), Z = pauli::Z.toMatrix();
    REQUIRE(isUnitary(X)); REQUIRE(isHermitian(X));
    Matrix notU = Matrix::fromRows({{1, 1}, {0, 1}});
    REQUIRE_FALSE(isUnitary(notU));
    Matrix rho0 = Matrix::fromRows({{1, 0}, {0, 0}}), rho1 = Matrix::fromRows({{0, 0}, {0, 1}});
    REQUIRE(isDensityMatrix(rho0));
    REQUIRE(traceDistance(rho0, rho1) == Approx(1.0).margin(1e-12));
    REQUIRE(fidelity(rho0, rho1) == Approx(0.0).margin(1e-12));
    REQUIRE(fidelity(rho0, rho0) == Approx(1.0).margin(1e-10));
    Matrix mixed = scale(Matrix::identity(2), 0.5);
    REQUIRE(purity(mixed) == Approx(0.5));
    REQUIRE(vonNeumannEntropy(mixed) == Approx(1.0).margin(1e-10));
    REQUIRE(fidelity(rho0, mixed) == Approx(0.5).margin(1e-10));
    Vector plus = {1 / std::sqrt(2.0), 1 / std::sqrt(2.0)};
    REQUIRE(fidelity(plus, rho0) == Approx(0.5));
    std::vector<Matrix> kraus = {scale(X, std::sqrt(0.3)), scale(Matrix::identity(2), std::sqrt(0.7))};
    REQUIRE(isTracePreserving(kraus));
    kraus[0] = scale(X, 0.9);
    REQUIRE_FALSE(isTracePreserving(kraus));
    (void)Z;
}

TEST_CASE("equalUpToGlobalPhase") {
    Matrix U = expPauli(0.3, 0.4, 0.5, 1.1).toMatrix();
    Matrix V = scale(U, std::polar(1.0, 2.2));
    REQUIRE(equalUpToGlobalPhase(U, V));
    REQUIRE(equalUpToGlobalPhase(V, U));
    REQUIRE(globalPhaseBetween(V, U) == Approx(2.2).margin(1e-12));
    Matrix W = expPauli(0.3, 0.4, 0.5, 1.2).toMatrix();
    REQUIRE_FALSE(equalUpToGlobalPhase(U, W));
    // Traceless case: X vs e^{iφ}X has Tr(X†X e^{iφ}) ≠ 0, but X vs Z has zero trace overlap → fallback path.
    REQUIRE_FALSE(equalUpToGlobalPhase(pauli::X.toMatrix(), pauli::Z.toMatrix()));
    REQUIRE(averageGateFidelity(U, U) == Approx(1.0).margin(1e-12));
}
