#include "Numerics/Eigen.hpp"
#include "Numerics/Blas.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
namespace qlab::num {
namespace {
// Cyclic Jacobi for Hermitian matrices. H ← G†HG, V ← VG with G = D·P in the (p,q) plane.
EigResult jacobiEigh(ConstMatrixView Hin) {
    const std::size_t n = Hin.rows;
    Matrix H(Hin), V = Matrix::identity(n);
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0;
        for (std::size_t p = 0; p < n; ++p) for (std::size_t q = p + 1; q < n; ++q) off += std::norm(H(p, q));
        if (off < 1e-30) break;
        for (std::size_t p = 0; p < n; ++p)
            for (std::size_t q = p + 1; q < n; ++q) {
                Complex hpq = H(p, q);
                double w = std::abs(hpq);
                if (w < 1e-300) continue;
                Complex ph = hpq / w; // e^{iφ}
                double a = H(p, p).real(), b = H(q, q).real();
                double theta = (b - a) / (2.0 * w);
                double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                // G columns: G(:,p) = c e_p - s conj(ph)? Derive: D = diag(1, conj(ph)) on (p,q); P = [[c, s],[-s, c]].
                // G = D P: G_pp = c, G_pq = s, G_qp = -s conj(ph), G_qq = c conj(ph).
                Complex gpp = c, gpq = s, gqp = -s * std::conj(ph), gqq = c * std::conj(ph);
                // H ← H G  (columns p,q)
                for (std::size_t i = 0; i < n; ++i) {
                    Complex hip = H(i, p), hiq = H(i, q);
                    H(i, p) = hip * gpp + hiq * gqp;
                    H(i, q) = hip * gpq + hiq * gqq;
                }
                // H ← G† H (rows p,q)
                for (std::size_t j = 0; j < n; ++j) {
                    Complex hpj = H(p, j), hqj = H(q, j);
                    H(p, j) = std::conj(gpp) * hpj + std::conj(gqp) * hqj;
                    H(q, j) = std::conj(gpq) * hpj + std::conj(gqq) * hqj;
                }
                H(p, q) = 0.0; H(q, p) = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    Complex vip = V(i, p), viq = V(i, q);
                    V(i, p) = vip * gpp + viq * gqp;
                    V(i, q) = vip * gpq + viq * gqq;
                }
            }
    }
    EigResult r; r.values.resize(n);
    std::vector<std::size_t> order(n); std::iota(order.begin(), order.end(), 0);
    for (std::size_t i = 0; i < n; ++i) r.values[i] = H(i, i).real();
    std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return r.values[x] < r.values[y]; });
    RealVector sv(n); Matrix SV(n, n);
    for (std::size_t k = 0; k < n; ++k) { sv[k] = r.values[order[k]]; for (std::size_t i = 0; i < n; ++i) SV(i, k) = V(i, order[k]); }
    r.values = std::move(sv); r.vectors = std::move(SV);
    return r;
}
} // namespace

Result<EigResult> eigh(ConstMatrixView H, bool forceOwn) {
    if (!H.square()) return fail(ErrorCode::InvalidArgument, "eigh: matrix not square");
    const std::size_t n = H.rows;
    if (n == 0) return EigResult{};
    if (!forceOwn && n >= Blas::kMinDim && Blas::zheevAvailable()) {
        EigResult r; r.vectors = Matrix(H); r.values.resize(n);
        auto st = Blas::zheev(r.vectors.view(), r.values);
        if (st) return r;
    }
    return jacobiEigh(H);
}

Result<SvdResult> svd(ConstMatrixView Ain) {
    // One-sided (Hestenes) Jacobi on A (m×n, m ≥ n); for m < n run on A† and swap roles.
    bool flip = Ain.rows < Ain.cols;
    Matrix A = flip ? adjoint(Ain) : Matrix(Ain);
    const std::size_t m = A.rows, n = A.cols;
    Matrix V = Matrix::identity(n);
    for (int sweep = 0; sweep < 100; ++sweep) {
        double rot = 0;
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = i + 1; j < n; ++j) {
                double alpha = 0, beta = 0; Complex gamma{};
                for (std::size_t r = 0; r < m; ++r) { alpha += std::norm(A(r, i)); beta += std::norm(A(r, j)); gamma += std::conj(A(r, i)) * A(r, j); }
                double g = std::abs(gamma);
                if (g < 1e-300 || g <= 1e-15 * std::sqrt(alpha * beta)) continue;
                rot += g * g;
                Complex ph = gamma / g;
                double zeta = (beta - alpha) / (2.0 * g);
                double t = (zeta >= 0 ? 1.0 : -1.0) / (std::abs(zeta) + std::sqrt(1.0 + zeta * zeta));
                double c = 1.0 / std::sqrt(1.0 + t * t), s = c * t;
                // columns i,j of A and V: [ai aj] ← [ai aj] G with G = [[c, s],[-s conj(ph), c conj(ph)]]
                for (std::size_t r = 0; r < m; ++r) {
                    Complex ai = A(r, i), aj = A(r, j);
                    A(r, i) = ai * c - aj * s * std::conj(ph);
                    A(r, j) = ai * s + aj * c * std::conj(ph);
                }
                for (std::size_t r = 0; r < n; ++r) {
                    Complex vi = V(r, i), vj = V(r, j);
                    V(r, i) = vi * c - vj * s * std::conj(ph);
                    V(r, j) = vi * s + vj * c * std::conj(ph);
                }
            }
        if (rot < 1e-30) break;
    }
    RealVector sing(n); Matrix U(m, n);
    for (std::size_t j = 0; j < n; ++j) {
        double nn = 0; for (std::size_t r = 0; r < m; ++r) nn += std::norm(A(r, j));
        sing[j] = std::sqrt(nn);
        for (std::size_t r = 0; r < m; ++r) U(r, j) = sing[j] > 1e-300 ? A(r, j) / sing[j] : Complex{};
    }
    std::vector<std::size_t> order(n); std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return sing[x] > sing[y]; });
    SvdResult r; r.singular.resize(n); r.U = Matrix(m, n); r.V = Matrix(n, n);
    for (std::size_t k = 0; k < n; ++k) {
        r.singular[k] = sing[order[k]];
        for (std::size_t i = 0; i < m; ++i) r.U(i, k) = U(i, order[k]);
        for (std::size_t i = 0; i < n; ++i) r.V(i, k) = V(i, order[k]);
    }
    if (flip) { // Ain = (A)† = (U S V†)† = V S U†
        std::swap(r.U, r.V);
    }
    return r;
}

Result<SchmidtResult> schmidt(std::span<const Complex> psi, std::size_t dimLow, std::size_t dimHigh) {
    if (psi.size() != dimLow * dimHigh) return fail(ErrorCode::InvalidArgument, "schmidt: dimension mismatch");
    // C(low, high) = psi[high*dimLow + low]
    Matrix C(dimLow, dimHigh);
    for (std::size_t h = 0; h < dimHigh; ++h) for (std::size_t l = 0; l < dimLow; ++l) C(l, h) = psi[h * dimLow + l];
    auto s = svd(C);
    if (!s) return std::unexpected(s.error());
    SchmidtResult r; r.coefficients = s->singular; r.low = s->U; r.high = conj(s->V); // C = U S V† → ψ = Σ s_k u_k ⊗ conj(v_k)
    return r;
}

Result<Matrix> hermitianFunction(ConstMatrixView H, double (*f)(double)) {
    auto e = eigh(H);
    if (!e) return std::unexpected(e.error());
    const std::size_t n = H.rows;
    Matrix D(n, n);
    for (std::size_t i = 0; i < n; ++i) D(i, i) = f(e->values[i]);
    return matmul(matmul(e->vectors, D), adjoint(e->vectors));
}
Result<Matrix> sqrtm(ConstMatrixView psd) {
    return hermitianFunction(psd, [](double x) { return x > 0 ? std::sqrt(x) : 0.0; });
}
} // namespace qlab::num
