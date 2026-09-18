// Spec 21 §2.1–2.2 — reduced states and scalar measures (see Reduced.hpp).
#include "Viz/Math/Reduced.hpp"
#include "Numerics/Tensor.hpp"
#include "QSim/Measures.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace qlab::viz::math {
namespace {

Status checkState(std::span<const Complex> psi, std::uint32_t n) {
    if (n == 0 || n > 40) return fail(ErrorCode::InvalidArgument, "reduced state: qubit count out of range");
    if (psi.size() != (std::size_t{1} << n))
        return fail(ErrorCode::InvalidArgument, "reduced state: the state has " + std::to_string(psi.size()) +
                                                    " amplitudes, expected 2^" + std::to_string(n));
    return {};
}

// Checks that `keep` names distinct sites below n and returns them as size_t.
Result<std::vector<std::size_t>> checkedSites(std::span<const QubitIndex> keep, std::uint32_t n) {
    std::vector<std::size_t> out;
    out.reserve(keep.size());
    for (QubitIndex q : keep) {
        if (q.get() >= n) return fail(ErrorCode::OutOfRange, "reduced state: qubit " + std::to_string(q.get()) + " out of range");
        if (std::find(out.begin(), out.end(), std::size_t{q.get()}) != out.end())
            return fail(ErrorCode::InvalidArgument, "reduced state: qubit " + std::to_string(q.get()) + " listed twice");
        out.push_back(q.get());
    }
    if (out.empty()) return fail(ErrorCode::InvalidArgument, "reduced state: empty qubit subset");
    return out;
}

// Inserts a zero bit at position `pos` of x (bits at and above `pos` move up by one).
constexpr std::size_t insertZero(std::size_t x, std::uint32_t pos) {
    const std::size_t low = x & ((std::size_t{1} << pos) - 1);
    return ((x >> pos) << (pos + 1)) | low;
}

} // namespace

Result<std::uint32_t> qubitCountOf(std::size_t amplitudes) {
    if (amplitudes < 2 || !std::has_single_bit(amplitudes))
        return fail(ErrorCode::InvalidArgument, "state length " + std::to_string(amplitudes) + " is not a power of two");
    return static_cast<std::uint32_t>(std::countr_zero(amplitudes));
}

Result<Matrix> reducedSingle(std::span<const Complex> psi, std::uint32_t n, std::uint32_t k) {
    QXL_TRY(checkState(psi, n));
    if (k >= n) return fail(ErrorCode::OutOfRange, "reduced state: qubit " + std::to_string(k) + " out of range");
    const std::size_t bit = std::size_t{1} << k;
    double p0 = 0.0, p1 = 0.0;
    Complex c01{};
    const std::size_t half = psi.size() >> 1;
    for (std::size_t t = 0; t < half; ++t) {
        const std::size_t j = insertZero(t, k);      // every index with j_k = 0
        const Complex a0 = psi[j], a1 = psi[j | bit];
        p0 += std::norm(a0);
        p1 += std::norm(a1);
        c01 += a0 * std::conj(a1);
    }
    Matrix rho(2, 2);
    rho(0, 0) = p0;
    rho(1, 1) = p1;
    rho(0, 1) = c01;
    rho(1, 0) = std::conj(c01);
    return rho;
}

Result<Matrix> reducedPair(std::span<const Complex> psi, std::uint32_t n, std::uint32_t i, std::uint32_t j) {
    QXL_TRY(checkState(psi, n));
    if (i >= n || j >= n) return fail(ErrorCode::OutOfRange, "reduced pair: qubit out of range");
    if (i == j) return fail(ErrorCode::InvalidArgument, "reduced pair: the two qubits must differ");
    const std::uint32_t lo = std::min(i, j), hi = std::max(i, j);
    const std::size_t bi = std::size_t{1} << i, bj = std::size_t{1} << j;
    const std::array<std::size_t, 4> off{0, bi, bj, bi | bj}; // index c = b_i + 2·b_j
    std::array<Complex, 16> acc{};
    const std::size_t quarter = psi.size() >> 2;
    for (std::size_t t = 0; t < quarter; ++t) {
        const std::size_t base = insertZero(insertZero(t, lo), hi);
        const std::array<Complex, 4> a{psi[base], psi[base | off[1]], psi[base | off[2]], psi[base | off[3]]};
        for (std::size_t r = 0; r < 4; ++r)
            for (std::size_t c = r; c < 4; ++c) acc[r * 4 + c] += a[r] * std::conj(a[c]);
    }
    Matrix rho(4, 4);
    for (std::size_t r = 0; r < 4; ++r)
        for (std::size_t c = r; c < 4; ++c) {
            rho(r, c) = acc[r * 4 + c];
            if (c != r) rho(c, r) = std::conj(acc[r * 4 + c]);
        }
    return rho;
}

Result<Matrix> reducedSubset(std::span<const Complex> psi, std::uint32_t n, std::span<const QubitIndex> keep) {
    QXL_TRY(checkState(psi, n));
    QXL_TRY_ASSIGN(auto sites, checkedSites(keep, n));
    if (sites.size() == 1) return reducedSingle(psi, n, static_cast<std::uint32_t>(sites[0]));
    if (sites.size() == 2)
        return reducedPair(psi, n, static_cast<std::uint32_t>(sites[0]), static_cast<std::uint32_t>(sites[1]));
    if (sites.size() > 14) return fail(ErrorCode::OutOfRange, "reduced state: at most 14 qubits may be kept");
    return num::reducedState(psi, n, sites, 2);
}

Result<Matrix> reducedFromDensity(const Matrix& rho, std::uint32_t nSites, std::uint32_t levels,
                                  std::span<const QubitIndex> keep) {
    if (levels < 2 || nSites == 0) return fail(ErrorCode::InvalidArgument, "reduced state: bad site description");
    const std::size_t dim = num::ipow(levels, nSites);
    if (rho.rows != dim || rho.cols != dim)
        return fail(ErrorCode::InvalidArgument, "reduced state: density matrix is " + std::to_string(rho.rows) + "x" +
                                                    std::to_string(rho.cols) + ", expected dimension " + std::to_string(dim));
    QXL_TRY_ASSIGN(auto sites, checkedSites(keep, nSites));
    const std::vector<std::size_t> dims(nSites, levels);
    return num::partialTrace(rho, dims, sites);
}

Result<Matrix> computationalBlock(const Matrix& rho, std::uint32_t nSites, std::uint32_t levels) {
    if (levels < 2 || nSites == 0) return fail(ErrorCode::InvalidArgument, "computational block: bad site description");
    const std::size_t dim = num::ipow(levels, nSites);
    if (rho.rows != dim || rho.cols != dim) return fail(ErrorCode::InvalidArgument, "computational block: dimension mismatch");
    if (levels == 2) return rho;
    const std::size_t qdim = std::size_t{1} << nSites;
    std::vector<std::size_t> map(qdim); // qubit basis index → mixed-radix index with every digit ∈ {0, 1}
    for (std::size_t a = 0; a < qdim; ++a) {
        std::size_t idx = 0, stride = 1;
        for (std::uint32_t s = 0; s < nSites; ++s, stride *= levels) idx += ((a >> s) & 1u) * stride;
        map[a] = idx;
    }
    Matrix out(qdim, qdim);
    for (std::size_t a = 0; a < qdim; ++a)
        for (std::size_t b = 0; b < qdim; ++b) out(a, b) = rho(map[a], map[b]);
    return out;
}

double BlochVector::norm() const { return std::sqrt(x * x + y * y + z * z); }
double BlochVector::theta() const {
    const double r = norm();
    return r > 0.0 ? std::acos(std::clamp(z / r, -1.0, 1.0)) : 0.0;
}
double BlochVector::phi() const { return (x == 0.0 && y == 0.0) ? 0.0 : std::atan2(y, x); }

Result<BlochVector> blochVector(const Matrix& rho1q) {
    QXL_TRY_ASSIGN(auto r, qsim::measures::blochVector(rho1q));
    return BlochVector{r[0], r[1], r[2]};
}

double purity(const Matrix& rho) { return qsim::measures::purity(rho); }
double entropyBits(const Matrix& rho) { return qsim::measures::entropyBits(rho); }

Result<PairMeasures> pairMeasures(const Matrix& rhoIJ) {
    if (rhoIJ.rows != 4 || rhoIJ.cols != 4) return fail(ErrorCode::InvalidArgument, "pair measures need a 4x4 density matrix");
    const std::array<std::size_t, 2> dims{2, 2};
    const std::array<std::size_t, 1> first{0}, second{1};
    const Matrix rhoI = num::partialTrace(rhoIJ, dims, first);   // qubit i is the least significant
    const Matrix rhoJ = num::partialTrace(rhoIJ, dims, second);
    PairMeasures m;
    m.entropyI = entropyBits(rhoI);
    m.entropyJ = entropyBits(rhoJ);
    m.entropyIJ = entropyBits(rhoIJ);
    // Clamp the round-off of three eigen-decompositions into the exact range [0, 2].
    m.mutualInformation = std::clamp(m.entropyI + m.entropyJ - m.entropyIJ, 0.0, 2.0);
    QXL_TRY_ASSIGN(m.concurrence, qsim::measures::concurrence(rhoIJ));
    return m;
}

Result<SchmidtSpectrum> schmidtSpectrum(std::span<const Complex> psi, std::uint32_t n,
                                        std::span<const QubitIndex> partition, double rankTol) {
    QXL_TRY(checkState(psi, n));
    if (partition.size() > 14) return fail(ErrorCode::OutOfRange, "Schmidt spectrum: |A| must be at most 14 (spec 21 §3.9)");
    QXL_TRY_ASSIGN(auto lambda, qsim::measures::schmidtCoefficients(psi, n, partition));
    SchmidtSpectrum s;
    s.partition.assign(partition.begin(), partition.end());
    s.coefficients.assign(lambda.begin(), lambda.end());
    for (double l : s.coefficients) {
        if (l > rankTol) ++s.rank;
        const double p = l * l;
        if (p > 1e-300) s.entropyBits -= p * std::log2(p);
    }
    return s;
}

} // namespace qlab::viz::math
