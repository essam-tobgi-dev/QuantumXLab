#include "Numerics/Blas.hpp"
#include <vector>
#ifdef QXL_HAVE_ACCELERATE
#define ACCELERATE_NEW_LAPACK
#define ACCELERATE_LAPACK_ILP64
#include <Accelerate/Accelerate.h>
#endif
namespace qlab::num {
bool Blas::available() {
#ifdef QXL_HAVE_ACCELERATE
    return true;
#else
    return false;
#endif
}
bool Blas::zheevAvailable() {
#ifdef QXL_HAVE_ACCELERATE
    return true;
#else
    return false;
#endif
}
void Blas::zgemm(ConstMatrixView A, ConstMatrixView B, MatrixView C, Complex alpha, Complex beta) {
#ifdef QXL_HAVE_ACCELERATE
    cblas_zgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, static_cast<int>(A.rows), static_cast<int>(B.cols),
                static_cast<int>(A.cols), &alpha, A.ptr, static_cast<int>(A.rowStride), B.ptr,
                static_cast<int>(B.rowStride), &beta, C.ptr, static_cast<int>(C.rowStride));
#else
    (void)A; (void)B; (void)C; (void)alpha; (void)beta;
#endif
}
Status Blas::zheev(MatrixView H, std::span<double> w) {
#ifdef QXL_HAVE_ACCELERATE
    // LAPACK is column-major: copy H(i,j) to a[j*n+i] so `a` holds H exactly.
    __LAPACK_int n = static_cast<__LAPACK_int>(H.rows);
    std::vector<Complex> a(static_cast<std::size_t>(n) * n);
    for (std::size_t i = 0; i < H.rows; ++i)
        for (std::size_t j = 0; j < H.cols; ++j) a[j * H.rows + i] = H(i, j);
    __LAPACK_int lwork = -1, info = 0;
    Complex wkopt;
    std::vector<double> rwork(static_cast<std::size_t>(std::max<__LAPACK_int>(1, 3 * n - 2)));
    char jobz = 'V', uplo = 'U';
    zheev_(&jobz, &uplo, &n, reinterpret_cast<__LAPACK_double_complex*>(a.data()), &n, w.data(),
           reinterpret_cast<__LAPACK_double_complex*>(&wkopt), &lwork, rwork.data(), &info);
    lwork = static_cast<__LAPACK_int>(wkopt.real());
    std::vector<Complex> work(static_cast<std::size_t>(lwork));
    zheev_(&jobz, &uplo, &n, reinterpret_cast<__LAPACK_double_complex*>(a.data()), &n, w.data(),
           reinterpret_cast<__LAPACK_double_complex*>(work.data()), &lwork, rwork.data(), &info);
    if (info != 0) return fail(ErrorCode::Internal, "zheev failed");
    // Column k of `a` (column-major) is eigenvector k: write it as column k of the row-major output.
    for (std::size_t k = 0; k < H.rows; ++k)
        for (std::size_t i = 0; i < H.rows; ++i) H(i, k) = a[k * H.rows + i];
    return {};
#else
    (void)H; (void)w;
    return fail(ErrorCode::Unsupported, "LAPACK not available");
#endif
}
} // namespace qlab::num
