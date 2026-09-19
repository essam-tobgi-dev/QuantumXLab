#include "QSim/Measures.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Eigen.hpp"
#include "Numerics/Tensor.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::qsim::measures {
namespace {
const num::Mat2& pauliY() {
    return num::pauli::Y;
}
} // namespace

Result<std::array<double, 3>> blochVector(const Matrix& rho) {
    if (rho.rows != 2 || rho.cols != 2)
        return fail(err::BadTargets, "blochVector requires a 2x2 density matrix");
    // Tr(ρX) = ρ01 + ρ10 = 2 Re ρ01; Tr(ρY) = i(ρ01 − ρ10) = −2 Im ρ01 (Y = [[0, −i], [i, 0]], so
    // |+i⟩ = (|0⟩ + i|1⟩)/√2 has ρ01 = −i/2 and r_y = +1); Tr(ρZ) = ρ00 − ρ11.
    return std::array<double, 3>{2.0 * rho(0, 1).real(), -2.0 * rho(0, 1).imag(),
                                 (rho(0, 0) - rho(1, 1)).real()};
}

double entropyBits(const Matrix& rho) {
    return num::vonNeumannEntropy(rho);
}

double purity(const Matrix& rho) {
    return num::purity(rho);
}

Result<double> concurrence(const Matrix& rho) {
    if (rho.rows != 4 || rho.cols != 4)
        return fail(err::BadTargets, "concurrence requires a 4x4 density matrix");
    const num::Mat2& Y = pauliY();
    Matrix yy = num::kron(Y.toMatrix(), Y.toMatrix()); // (Y⊗Y), real antisymmetric times i
    Matrix rhoConj(4, 4);
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < 4; ++j)
            rhoConj(i, j) = std::conj(rho(i, j));
    Matrix rhoTilde = num::matmul(yy, num::matmul(rhoConj, yy));
    // ρρ̃ is not Hermitian (its singular values are not its eigenvalues), but it has the spectrum of
    // the Hermitian √ρ ρ̃ √ρ (Wootters), which is what is diagonalised.
    auto sq = num::sqrtm(rho);
    if (!sq)
        return std::unexpected(sq.error());
    Matrix m = num::matmul(*sq, num::matmul(rhoTilde, *sq));
    // Symmetrize against round-off before the Hermitian solve.
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = i; j < 4; ++j) {
            Complex avg = 0.5 * (m(i, j) + std::conj(m(j, i)));
            m(i, j) = avg;
            m(j, i) = std::conj(avg);
        }
    auto e = num::eigh(m);
    if (!e)
        return std::unexpected(e.error());
    std::vector<double> lam;
    for (std::size_t i = 0; i < 4; ++i)
        lam.push_back(std::sqrt(std::max(0.0, e->values[i])));
    std::sort(lam.begin(), lam.end(), std::greater<double>());
    return std::max(0.0, lam[0] - lam[1] - lam[2] - lam[3]);
}

Result<num::RealVector> schmidtCoefficients(std::span<const Complex> psi, std::uint32_t n,
                                            std::span<const QubitIndex> keep) {
    if (psi.size() != (std::size_t{1} << n))
        return fail(err::BadTargets, "state length does not match qubit count");
    if (keep.empty() || keep.size() >= n)
        return fail(err::BadTargets, "the bipartition must be a proper non-empty subset");
    std::vector<std::uint32_t> a;
    for (auto q : keep) {
        if (q.get() >= n)
            return fail(err::BadTargets, "qubit index out of range");
        a.push_back(q.get());
    }
    std::sort(a.begin(), a.end());
    if (std::adjacent_find(a.begin(), a.end()) != a.end())
        return fail(err::BadTargets, "duplicate qubit in bipartition");
    std::vector<std::uint32_t> b;
    for (std::uint32_t q = 0; q < n; ++q)
        if (!std::binary_search(a.begin(), a.end(), q))
            b.push_back(q);
    const std::size_t dimA = std::size_t{1} << a.size();
    const std::size_t dimB = std::size_t{1} << b.size();
    // Reshape psi into M[iA][iB] by scattering the bits of each basis index.
    Matrix m(dimA, dimB);
    for (std::size_t idx = 0; idx < psi.size(); ++idx) {
        std::size_t ia = 0, ib = 0;
        for (std::size_t k = 0; k < a.size(); ++k)
            ia |= ((idx >> a[k]) & 1) << k;
        for (std::size_t k = 0; k < b.size(); ++k)
            ib |= ((idx >> b[k]) & 1) << k;
        m(ia, ib) = psi[idx];
    }
    auto s = num::svd(m);
    if (!s)
        return std::unexpected(s.error());
    return s->singular;
}

double mutualInformation(const Matrix& rhoA, const Matrix& rhoB, const Matrix& rhoAB) {
    return entropyBits(rhoA) + entropyBits(rhoB) - entropyBits(rhoAB);
}

double fidelityTo(const Matrix& rho, std::span<const Complex> target) {
    return num::fidelity(target, rho);
}
double fidelityTo(const Matrix& rho, const Matrix& sigma) {
    return num::fidelity(rho, sigma);
}

} // namespace qlab::qsim::measures
