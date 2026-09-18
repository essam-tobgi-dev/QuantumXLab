// Spec 07 §4, T09 §3 — Aaronson–Gottesman tableau: storage, Clifford updates, row products, export.
#include "Core/Log.hpp"
#include "QSim/Stabilizer.hpp"
#include <bit>
#include <format>
#include <limits>

namespace qlab::qsim {

Capabilities StabilizerBackend::capabilities() const { return {Kind::Stabilizer, 10000, false, true, false, true, false, false, true}; }

Status StabilizerBackend::allocate(std::uint32_t n, std::uint32_t levels) {
    if (levels != 2) return fail(err::Unsupported, "stabilizer backend supports qubits only");
    if (n > 10000) return fail(err::TooLarge, "stabilizer backend cap is 10000 qubits");
    n_ = n; zOff_ = (n + 63) / 64; W_ = 2 * zOff_;
    words_.assign(std::size_t(2) * n * W_, 0);
    r_.assign(std::size_t(2) * n, 0);
    for (std::uint32_t i = 0; i < n; ++i) { setX(i, i, true); setZ(n + i, i, true); } // destabilizers X_i, stabilizers Z_i
    allocated_ = true; ops_ = 0;
    QXL_LOG_INFO(Sim, "Stabilizer allocated: n={} bytes={}", n, bytesAllocated());
    return {};
}
std::unique_ptr<IBackend> StabilizerBackend::clone() const { return std::make_unique<StabilizerBackend>(*this); }

void StabilizerBackend::setX(std::size_t row, std::uint32_t q, bool v) {
    auto& w = words_[row * W_ + (q >> 6)]; std::uint64_t m = std::uint64_t{1} << (q & 63);
    w = v ? (w | m) : (w & ~m);
}
void StabilizerBackend::setZ(std::size_t row, std::uint32_t q, bool v) {
    auto& w = words_[row * W_ + zOff_ + (q >> 6)]; std::uint64_t m = std::uint64_t{1} << (q & 63);
    w = v ? (w | m) : (w & ~m);
}

namespace {
// Σ_k g(x1_k, z1_k, x2_k, z2_k) of T09 (3.1) for the product P1·P2, word-parallel (spec 07 §4):
// each qubit contributes i^{+1} for XY, YZ, ZX and i^{−1} for YX, ZY, XZ. Unused high bits of the
// last word are zero in both operands and contribute nothing.
int productPhase(const std::uint64_t* x1, const std::uint64_t* z1, const std::uint64_t* x2,
                 const std::uint64_t* z2, std::size_t words) {
    int sum = 0;
    for (std::size_t w = 0; w < words; ++w) {
        const std::uint64_t X1 = x1[w] & ~z1[w], Y1 = x1[w] & z1[w], Z1 = ~x1[w] & z1[w];
        const std::uint64_t X2 = x2[w] & ~z2[w], Y2 = x2[w] & z2[w], Z2 = ~x2[w] & z2[w];
        sum += std::popcount((X1 & Y2) | (Y1 & Z2) | (Z1 & X2));
        sum -= std::popcount((Y1 & X2) | (Z1 & Y2) | (X1 & Z2));
    }
    return sum;
}
} // namespace

int StabilizerBackend::rowsumPhase(std::size_t h, std::size_t i) const {
    const std::uint64_t* hi = &words_[h * W_];
    const std::uint64_t* ii = &words_[i * W_];
    const int sum = 2 * int(r_[h]) + 2 * int(r_[i]) + productPhase(ii, ii + zOff_, hi, hi + zOff_, zOff_);
    return ((sum % 4) + 4) % 4;
}
void StabilizerBackend::rowsum(std::size_t h, std::size_t i) {
    // Destabilizer rows may anticommute with row i (odd phase); their sign is irrelevant (T09 §3.2).
    r_[h] = rowsumPhase(h, i) == 2 ? 1 : 0;
    for (std::size_t w = 0; w < W_; ++w) words_[h * W_ + w] ^= words_[i * W_ + w];
}
void StabilizerBackend::rowsumInto(std::vector<std::uint64_t>& acc, int& phase, std::size_t i) const {
    // acc is a scratch row (x words then z words); phase holds 2·r in units of i.
    const std::uint64_t* ii = &words_[i * W_];
    const int sum = phase + 2 * int(r_[i]) + productPhase(ii, ii + zOff_, acc.data(), acc.data() + zOff_, zOff_);
    phase = ((sum % 4) + 4) % 4;
    for (std::size_t w = 0; w < W_; ++w) acc[w] ^= ii[w];
}

void StabilizerBackend::h(std::uint32_t q) {
    for (std::size_t i = 0; i < 2 * std::size_t(n_); ++i) {
        bool xq = getX(i, q), zq = getZ(i, q);
        r_[i] ^= (xq && zq);
        setX(i, q, zq); setZ(i, q, xq);
    }
}
void StabilizerBackend::s(std::uint32_t q) {
    for (std::size_t i = 0; i < 2 * std::size_t(n_); ++i) {
        bool xq = getX(i, q), zq = getZ(i, q);
        r_[i] ^= (xq && zq);
        setZ(i, q, zq ^ xq);
    }
}
void StabilizerBackend::sdg(std::uint32_t q) { s(q); s(q); s(q); }
// Paulis only flip signs (spec 07 §4): a row anticommutes with X_q iff it has Z or Y on q, etc.
void StabilizerBackend::x(std::uint32_t q) { for (std::size_t i = 0; i < 2 * std::size_t(n_); ++i) r_[i] ^= getZ(i, q); }
void StabilizerBackend::z(std::uint32_t q) { for (std::size_t i = 0; i < 2 * std::size_t(n_); ++i) r_[i] ^= getX(i, q); }
void StabilizerBackend::y(std::uint32_t q) {
    for (std::size_t i = 0; i < 2 * std::size_t(n_); ++i) r_[i] ^= (getX(i, q) != getZ(i, q));
}
void StabilizerBackend::sx(std::uint32_t q) { h(q); s(q); h(q); }
void StabilizerBackend::cnot(std::uint32_t c, std::uint32_t t) {
    for (std::size_t i = 0; i < 2 * std::size_t(n_); ++i) {
        bool xc = getX(i, c), zc = getZ(i, c), xt = getX(i, t), zt = getZ(i, t);
        r_[i] ^= (xc && zt && (xt ^ zc ^ 1));
        setX(i, t, xt ^ xc); setZ(i, c, zc ^ zt);
    }
}
void StabilizerBackend::cz(std::uint32_t a, std::uint32_t b) { h(b); cnot(a, b); h(b); }
void StabilizerBackend::swap(std::uint32_t a, std::uint32_t b) { cnot(a, b); cnot(b, a); cnot(a, b); }

Status StabilizerBackend::applyPauli(const PauliString& p) {
    if (!allocated_) return fail(err::NotAllocated, "backend not allocated");
    if (p.size() != n_) return fail(err::BadPauli, "Pauli string length mismatch");
    for (std::uint32_t q = 0; q < n_; ++q) {
        char c = p.op(q);
        if (c == 'X') x(q); else if (c == 'Y') y(q); else if (c == 'Z') z(q);
    }
    ++ops_;
    return {};
}

Status StabilizerBackend::applyChannel(const Kraus&, std::span<const QubitIndex>) {
    return fail(err::Unsupported, "stabilizer backend accepts Pauli-frame noise only (applyPauli)");
}

TableauExport StabilizerBackend::exportTableau() const {
    TableauExport t; t.n = n_;
    auto rowStr = [&](std::size_t i) {
        std::string s = r_[i] ? "-" : "+";
        std::string body(n_, 'I');
        for (std::uint32_t q = 0; q < n_; ++q) {
            bool xq = getX(i, q), zq = getZ(i, q);
            body[n_ - 1 - q] = xq ? (zq ? 'Y' : 'X') : (zq ? 'Z' : 'I');
        }
        return s + body;
    };
    for (std::size_t i = 0; i < n_; ++i) t.destabilizers.push_back(rowStr(i));
    for (std::size_t i = 0; i < n_; ++i) t.stabilizers.push_back(rowStr(n_ + i));
    return t;
}

double StabilizerBackend::entanglementEntropy(std::span<const QubitIndex> sub) const {
    // S = rank_F2(stabilizer rows restricted to A) − |A| (spec 07 §8). Invalid subsystems yield NaN.
    if (!allocated_ || !validTargets(sub, n_)) return std::numeric_limits<double>::quiet_NaN();
    std::vector<std::vector<std::uint8_t>> m;
    for (std::size_t i = n_; i < 2 * std::size_t(n_); ++i) {
        std::vector<std::uint8_t> row;
        for (auto q : sub) { row.push_back(getX(i, q.value)); row.push_back(getZ(i, q.value)); }
        m.push_back(row);
    }
    std::size_t rank = 0, cols = 2 * sub.size();
    for (std::size_t c = 0; c < cols && rank < m.size(); ++c) {
        std::size_t piv = rank; while (piv < m.size() && !m[piv][c]) ++piv;
        if (piv == m.size()) continue;
        std::swap(m[piv], m[rank]);
        for (std::size_t r = 0; r < m.size(); ++r) if (r != rank && m[r][c]) for (std::size_t k = 0; k < cols; ++k) m[r][k] ^= m[rank][k];
        ++rank;
    }
    return double(rank) - double(sub.size());
}

} // namespace qlab::qsim
