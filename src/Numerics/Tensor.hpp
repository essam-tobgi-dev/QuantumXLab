#pragma once
// Spec 06 §2 — tensor operations. Little-endian: subsystem k has stride Π_{j<k} d_j (T01 §4).
#include "Numerics/Matrix.hpp"
#include <span>
#include <vector>
namespace qlab::num {

// (A ⊗ B)_{(i1 i2),(j1 j2)} = A_{i1 j1} B_{i2 j2}; the left factor is the more significant index.
Matrix kron(ConstMatrixView a, ConstMatrixView b);
// kronList({A_0, …, A_{n-1}}) = A_{n-1} ⊗ … ⊗ A_0: element k acts on subsystem k (least significant first).
Matrix kronList(std::span<const Matrix> ops);
// Kronecker product of vectors with the same little-endian rule.
Vector kronVec(std::span<const Complex> a, std::span<const Complex> b); // b more significant? No: a is subsystem 0 (low), b subsystem 1 (high)
// ρ_keep = Tr_{others} ρ. `dims[k]` = local dimension of subsystem k; `keep` ascending subsystem indices.
Matrix partialTrace(ConstMatrixView rho, std::span<const std::size_t> dims, std::span<const std::size_t> keep);
// Reshape ψ (or a flat tensor) with local dims `dims` and permute axes: newDims[k] = dims[perm[k]].
Vector permuteAxes(std::span<const Complex> psi, std::span<const std::size_t> dims, std::span<const std::size_t> perm);
// Full 2^n × 2^n (or Π dims) matrix of a k-subsystem gate U on `targets` (targets[0] = least significant index of U).
Matrix embed(ConstMatrixView U, std::span<const std::size_t> targets, std::size_t nQubits, std::size_t localDim = 2);
// Zero-copy reshape view of a vector as rows×cols (rows*cols == size).
ConstMatrixView reshape(std::span<const Complex> v, std::size_t rows, std::size_t cols);
MatrixView reshape(std::span<Complex> v, std::size_t rows, std::size_t cols);
// Reduced density matrix of a pure state over `keep` (little-endian, all subsystems dimension `localDim`).
Matrix reducedState(std::span<const Complex> psi, std::size_t nQubits, std::span<const std::size_t> keep, std::size_t localDim = 2);
// Helpers
std::size_t ipow(std::size_t base, std::size_t exp);
} // namespace qlab::num
