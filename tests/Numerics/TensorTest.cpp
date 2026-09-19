#include "Numerics/Numerics.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
using namespace qlab::num;
using Catch::Approx;

static Matrix mat2(const Mat2& m) {
    return m.toMatrix();
}

TEST_CASE("kron and kronList follow little-endian ordering") {
    Matrix X = mat2(pauli::X), I = mat2(pauli::I);
    // kron(A,B): left factor more significant → kron(I, X) = X on qubit 0.
    Matrix IX = kron(I, X);
    REQUIRE(IX.rows == 4);
    REQUIRE(std::abs(IX(0, 1) - Complex(1, 0)) < 1e-15); // |00> ↔ |01> (index 1 = qubit0 set)
    REQUIRE(std::abs(IX(2, 3) - Complex(1, 0)) < 1e-15);
    Matrix ops[2] = {X, I}; // element 0 acts on qubit 0
    Matrix L = kronList(ops);
    REQUIRE(approxEqual(L, IX, 1e-15));
    std::size_t tgt[1] = {0};
    Matrix E = embed(X, tgt, 2);
    REQUIRE(approxEqual(E, IX, 1e-15));
    std::size_t tgt1[1] = {1};
    REQUIRE(approxEqual(embed(X, tgt1, 2), kron(X, I), 1e-15));
}

TEST_CASE("embed of a 2-qubit gate on reversed targets") {
    // CNOT with control = targets[0] (low bit of U), target = targets[1].
    Matrix cx = Matrix::fromRows({{1, 0, 0, 0},
                                  {0, 0, 0, 1},
                                  {0, 0, 1, 0},
                                  {0, 1, 0, 0}}); // little-endian CX: control q0, target q1
    std::size_t t01[2] = {0, 1};
    Matrix E = embed(cx, t01, 2);
    REQUIRE(approxEqual(E, cx, 1e-15));
    std::size_t t10[2] = {1, 0};
    Matrix E2 = embed(cx, t10, 2); // control q1, target q0
    Matrix cx10 = Matrix::fromRows({{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}, {0, 0, 1, 0}});
    REQUIRE(approxEqual(E2, cx10, 1e-15));
}

TEST_CASE("partialTrace of Bell state gives maximally mixed qubit") {
    Vector bell = {1 / std::sqrt(2.0), 0, 0, 1 / std::sqrt(2.0)};
    Matrix rho = projector(bell);
    std::size_t dims[2] = {2, 2};
    std::size_t keep0[1] = {0};
    Matrix r0 = partialTrace(rho, dims, keep0);
    REQUIRE(r0.rows == 2);
    REQUIRE(std::abs(r0(0, 0) - Complex(0.5, 0)) < 1e-14);
    REQUIRE(std::abs(r0(1, 1) - Complex(0.5, 0)) < 1e-14);
    REQUIRE(std::abs(r0(0, 1)) < 1e-14);
    std::size_t keep1[1] = {1};
    REQUIRE(approxEqual(partialTrace(rho, dims, keep1), r0, 1e-14));
    REQUIRE(approxEqual(reducedState(bell, 2, keep0), r0, 1e-14));
}

TEST_CASE("partialTrace of product state returns the factor") {
    Vector a = {std::sqrt(0.3), std::sqrt(0.7)}; // qubit 0
    Vector b = {0.6, Complex(0, 0.8)};           // qubit 1
    Vector psi = kronVec(a, b);
    REQUIRE(psi.size() == 4);
    std::size_t dims[2] = {2, 2};
    std::size_t keep0[1] = {0};
    Matrix r = partialTrace(projector(psi), dims, keep0);
    REQUIRE(approxEqual(r, projector(a), 1e-14));
    std::size_t keep1[1] = {1};
    REQUIRE(approxEqual(partialTrace(projector(psi), dims, keep1), projector(b), 1e-14));
}

TEST_CASE("permuteAxes swaps two qubits") {
    Vector psi(4);
    psi[1] = 1.0; // |q1 q0> = |01> (qubit 0 set)
    std::size_t dims[2] = {2, 2};
    std::size_t perm[2] = {1, 0};
    Vector out = permuteAxes(psi, dims, perm);
    REQUIRE(std::abs(out[2] - Complex(1, 0)) < 1e-15);
}

TEST_CASE("schmidt decomposition of Bell state") {
    Vector bell = {1 / std::sqrt(2.0), 0, 0, 1 / std::sqrt(2.0)};
    auto s = schmidt(bell, 2, 2);
    REQUIRE(s);
    REQUIRE(s->coefficients.size() >= 2);
    REQUIRE(s->coefficients[0] == Approx(1 / std::sqrt(2.0)).margin(1e-12));
    REQUIRE(s->coefficients[1] == Approx(1 / std::sqrt(2.0)).margin(1e-12));
}
