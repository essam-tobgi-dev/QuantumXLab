#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Numerics/Numerics.hpp"
#include "Core/Random.hpp"
#include <cmath>
using namespace qlab::num;
using Catch::Approx;

TEST_CASE("expm(0) = I and expm of diagonal") {
    Matrix Z(3, 3);
    auto E = expm(Z);
    REQUIRE(E);
    REQUIRE(approxEqual(*E, Matrix::identity(3), 1e-14));
    Matrix D(2, 2); D(0, 0) = 1.0; D(1, 1) = Complex(0, 3.0);
    auto ED = expm(D);
    REQUIRE(ED);
    REQUIRE(std::abs((*ED)(0, 0) - std::exp(1.0)) < 1e-13);
    REQUIRE(std::abs((*ED)(1, 1) - std::exp(Complex(0, 3.0))) < 1e-13);
}

TEST_CASE("expm matches closed form for Pauli rotations and the Hermitian path") {
    double nx = 0.2, ny = -0.5, nz = 0.8, theta = 2.3;
    double L = std::sqrt(nx * nx + ny * ny + nz * nz);
    // A = -i θ/2 (n·σ)/|n|
    Matrix nsig = scale(pauli::X.toMatrix(), nx / L); nsig += scale(pauli::Y.toMatrix(), ny / L); nsig += scale(pauli::Z.toMatrix(), nz / L);
    Matrix A = scale(nsig, Complex(0, -theta / 2));
    auto E = expm(A);
    REQUIRE(E);
    Matrix closed = expPauli(nx, ny, nz, theta).toMatrix();
    REQUIRE(maxAbsNorm(sub(*E, closed)) < 1e-12);
    auto EH = expmHermitian(nsig, theta / 2); // exp(-i H t)
    REQUIRE(EH);
    REQUIRE(maxAbsNorm(sub(*EH, closed)) < 1e-12);
    REQUIRE(isUnitary(*E, 1e-12));
}

TEST_CASE("expm large norm uses scaling and squaring correctly") {
    qlab::core::Random rng(5);
    Matrix H(6, 6);
    for (auto& v : H.data) v = Complex(rng.normal(), rng.normal());
    H = scale(add(H, adjoint(H)), 0.5);
    Matrix A = scale(H, Complex(0, -20.0)); // norm ≫ θ13
    auto E = expm(A);
    REQUIRE(E);
    auto EH = expmHermitian(H, 20.0);
    REQUIRE(EH);
    REQUIRE(maxAbsNorm(sub(*E, *EH)) < 1e-9);
}

TEST_CASE("expPauliPair gives exp(-i θ/2 Z⊗Z)") {
    Mat4 M = expPauliPair(pauli::Z, pauli::Z, 0.7);
    Matrix ZZ = kron(pauli::Z.toMatrix(), pauli::Z.toMatrix());
    auto E = expm(scale(ZZ, Complex(0, -0.35)));
    REQUIRE(E);
    REQUIRE(maxAbsNorm(sub(M.toMatrix(), *E)) < 1e-13);
}

TEST_CASE("expmTimesVector agrees with dense expm") {
    qlab::core::Random rng(9);
    std::size_t n = 24;
    Matrix H(n, n);
    for (auto& v : H.data) v = Complex(rng.normal(), rng.normal());
    H = scale(add(H, adjoint(H)), 0.5);
    Matrix A = scale(H, Complex(0, -0.7));
    Vector v(n); for (auto& x : v) x = Complex(rng.normal(), rng.normal());
    auto E = expm(A);
    REQUIRE(E);
    Vector ref = matvec(*E, v);
    Vector out = expmTimesVector(A, v, 1e-14);
    double err = 0; for (std::size_t i = 0; i < n; ++i) err = std::max(err, std::abs(ref[i] - out[i]));
    REQUIRE(err < 1e-9);
    // Sparse functor path.
    SparseMatrix S = SparseMatrix::fromDense(A);
    Vector out2 = expmTimesVector(n, [&](std::span<const Complex> x, std::span<Complex> y) { S.matvecInto(x, y); }, v, 1e-14);
    err = 0; for (std::size_t i = 0; i < n; ++i) err = std::max(err, std::abs(ref[i] - out2[i]));
    REQUIRE(err < 1e-9);
}

TEST_CASE("sparse CSR operations") {
    Matrix D = Matrix::fromRows({{1, 0, 2}, {0, 0, 3}, {4, 0, 0}});
    SparseMatrix S = SparseMatrix::fromDense(D);
    REQUIRE(S.nnz() == 4);
    REQUIRE(approxEqual(S.toDense(), D, 0.0));
    Vector x = {1, 2, 3};
    Vector y = S.matvec(x);
    REQUIRE(y[0] == Complex(7, 0)); REQUIRE(y[1] == Complex(9, 0)); REQUIRE(y[2] == Complex(4, 0));
    SparseMatrix P = matmul(S, S);
    REQUIRE(approxEqual(P.toDense(), matmul(D, D), 1e-14));
    SparseMatrix K = kron(S, SparseMatrix::identity(2));
    REQUIRE(approxEqual(K.toDense(), kron(D, Matrix::identity(2)), 1e-14));
    REQUIRE(approxEqual(S.adjoint().toDense(), adjoint(D), 1e-14));
    REQUIRE(approxEqual(add(S, S, 1.0, 2.0).toDense(), scale(D, 3.0), 1e-14));
}
