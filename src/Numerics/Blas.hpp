#pragma once
// Spec 06 §11 — BLAS/LAPACK dispatch (Accelerate on macOS). Own kernels remain the fallback.
#include "Numerics/Types.hpp"
namespace qlab::num {
struct Blas {
    static bool available();
    static constexpr std::size_t kMinDim = 64;
    // C = alpha A B + beta C
    static void zgemm(ConstMatrixView A, ConstMatrixView B, MatrixView C, Complex alpha, Complex beta);
    // Hermitian eigendecomposition: on success H_inout holds eigenvectors as COLUMNS, eigvals ascending.
    static Status zheev(MatrixView H_inout_eigvecs, std::span<double> eigvals);
    static bool zheevAvailable();
};
} // namespace qlab::num
