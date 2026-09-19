// Spec 16 §1, T09 §1.1 — Pauli strings: parsing (qubit 0 first), products with phase, the
// symplectic form T09 (1.2), and F2 linear algebra over (x|z) rows.
#include "QEC/Pauli.hpp"
#include <algorithm>
#include <format>

namespace qlab::qec {
namespace {

// Exponent of i produced by the single-qubit product P1·P2, T09 (3.1).
int productExponent(std::uint8_t x1, std::uint8_t z1, std::uint8_t x2, std::uint8_t z2) {
    if (!x1 && !z1)
        return 0;
    if (x1 && z1)
        return int(z2) - int(x2);
    if (x1)
        return int(z2) * (2 * int(x2) - 1);
    return int(x2) * (1 - 2 * int(z2));
}

struct Reduced {
    std::vector<std::uint8_t> bits;  // (x|z) row after elimination
    std::vector<std::uint8_t> combo; // which input rows were multiplied to obtain it
    std::size_t pivot = 0;
};

std::vector<std::uint8_t> rowBits(const PauliString& p) {
    std::vector<std::uint8_t> b(p.x.begin(), p.x.end());
    b.insert(b.end(), p.z.begin(), p.z.end());
    return b;
}

void xorInto(std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    for (std::size_t i = 0; i < a.size(); ++i)
        a[i] ^= b[i];
}

// Incremental elimination: every stored row is zero on the pivots of the rows stored before it.
std::vector<Reduced> eliminate(std::span<const PauliString> rows) {
    std::vector<Reduced> basis;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        Reduced r{rowBits(rows[i]), std::vector<std::uint8_t>(rows.size(), 0), 0};
        r.combo[i] = 1;
        for (const Reduced& b : basis)
            if (b.pivot < r.bits.size() && r.bits[b.pivot]) {
                xorInto(r.bits, b.bits);
                xorInto(r.combo, b.combo);
            }
        auto it = std::find(r.bits.begin(), r.bits.end(), std::uint8_t{1});
        if (it == r.bits.end())
            continue;
        r.pivot = static_cast<std::size_t>(it - r.bits.begin());
        basis.push_back(std::move(r));
    }
    return basis;
}

} // namespace

PauliString PauliString::identity(std::uint32_t n) {
    PauliString p;
    p.n = n;
    p.x.assign(n, 0);
    p.z.assign(n, 0);
    return p;
}

PauliString PauliString::single(std::uint32_t n, std::uint32_t qubit, char letter) {
    PauliString p = identity(n);
    if (qubit < n)
        p.setLetter(qubit, letter);
    return p;
}

Result<PauliString> PauliString::parse(std::string_view text) {
    std::int8_t phase = 0;
    std::size_t pos = 0;
    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
        if (text[pos] == '-')
            phase = 2;
        ++pos;
    }
    if (pos < text.size() && text[pos] == 'i') {
        phase = static_cast<std::int8_t>((phase + 1) & 3);
        ++pos;
    }
    const std::string_view body = text.substr(pos);
    if (body.empty())
        return fail(err::BadPauli, std::format("empty Pauli string '{}'", text));
    PauliString p = identity(static_cast<std::uint32_t>(body.size()));
    p.phase = phase;
    for (std::size_t q = 0; q < body.size(); ++q) {
        const char c = body[q];
        if (c != 'I' && c != 'X' && c != 'Y' && c != 'Z')
            return fail(
                err::BadPauli,
                std::format("Pauli string '{}': letter '{}' at qubit {} is not one of I, X, Y, Z",
                            text, c, q));
        p.setLetter(static_cast<std::uint32_t>(q), c);
    }
    return p;
}

char PauliString::letter(std::uint32_t q) const {
    if (q >= n)
        return 'I';
    return x[q] ? (z[q] ? 'Y' : 'X') : (z[q] ? 'Z' : 'I');
}

void PauliString::setLetter(std::uint32_t q, char c) {
    if (q >= n)
        return;
    x[q] = (c == 'X' || c == 'Y') ? 1 : 0;
    z[q] = (c == 'Z' || c == 'Y') ? 1 : 0;
}

std::string PauliString::str(bool withPhase) const {
    static constexpr std::string_view prefix[4] = {"+", "+i", "-", "-i"};
    std::string s = withPhase ? std::string(prefix[phase & 3]) : std::string();
    for (std::uint32_t q = 0; q < n; ++q)
        s.push_back(letter(q));
    return s;
}

std::uint32_t PauliString::weight() const {
    std::uint32_t w = 0;
    for (std::uint32_t q = 0; q < n; ++q)
        w += (x[q] | z[q]) ? 1u : 0u;
    return w;
}

std::vector<std::uint32_t> PauliString::support() const {
    std::vector<std::uint32_t> s;
    for (std::uint32_t q = 0; q < n; ++q)
        if (x[q] | z[q])
            s.push_back(q);
    return s;
}

bool PauliString::isIdentity() const {
    return weight() == 0;
}
bool PauliString::isXType() const {
    return std::none_of(z.begin(), z.end(), [](std::uint8_t b) { return b != 0; });
}
bool PauliString::isZType() const {
    return std::none_of(x.begin(), x.end(), [](std::uint8_t b) { return b != 0; });
}
bool PauliString::sameLetters(const PauliString& o) const {
    return n == o.n && x == o.x && z == o.z;
}

bool PauliString::commutesWith(const PauliString& o) const {
    unsigned parity = 0;
    const std::uint32_t m = std::min(n, o.n);
    for (std::uint32_t q = 0; q < m; ++q)
        parity ^= unsigned((x[q] & o.z[q]) ^ (z[q] & o.x[q]));
    return parity == 0;
}

PauliString PauliString::operator*(const PauliString& o) const {
    PauliString r = identity(std::max(n, o.n));
    int exponent = int(phase) + int(o.phase);
    for (std::uint32_t q = 0; q < r.n; ++q) {
        const std::uint8_t x1 = q < n ? x[q] : 0, z1 = q < n ? z[q] : 0;
        const std::uint8_t x2 = q < o.n ? o.x[q] : 0, z2 = q < o.n ? o.z[q] : 0;
        exponent += productExponent(x1, z1, x2, z2);
        r.x[q] = x1 ^ x2;
        r.z[q] = z1 ^ z2;
    }
    r.phase = static_cast<std::int8_t>(((exponent % 4) + 4) % 4);
    return r;
}

qsim::PauliString PauliString::toQsim() const {
    std::vector<std::pair<QubitIndex, char>> terms;
    for (std::uint32_t q = 0; q < n; ++q)
        if (x[q] | z[q])
            terms.emplace_back(QubitIndex{q}, letter(q));
    static const qsim::Complex kPhase[4] = {{1.0, 0.0}, {0.0, 1.0}, {-1.0, 0.0}, {0.0, -1.0}};
    return qsim::PauliString::fromQubits(n, terms, kPhase[phase & 3]);
}

std::vector<std::uint8_t> syndromeOf(std::span<const PauliString> generators,
                                     const PauliString& error) {
    std::vector<std::uint8_t> s(generators.size(), 0);
    for (std::size_t j = 0; j < generators.size(); ++j)
        s[j] = generators[j].commutesWith(error) ? 0 : 1;
    return s;
}

std::uint32_t symplecticRank(std::span<const PauliString> rows) {
    return static_cast<std::uint32_t>(eliminate(rows).size());
}

std::optional<std::vector<std::uint32_t>> solveProduct(std::span<const PauliString> rows,
                                                       const PauliString& target) {
    std::vector<std::uint8_t> t = rowBits(target);
    std::vector<std::uint8_t> combo(rows.size(), 0);
    for (const PauliString& r : rows)
        if (r.n != target.n)
            return std::nullopt;
    for (const Reduced& b : eliminate(rows))
        if (t[b.pivot]) {
            xorInto(t, b.bits);
            xorInto(combo, b.combo);
        }
    if (std::any_of(t.begin(), t.end(), [](std::uint8_t v) { return v != 0; }))
        return std::nullopt;
    std::vector<std::uint32_t> subset;
    for (std::size_t j = 0; j < combo.size(); ++j)
        if (combo[j])
            subset.push_back(static_cast<std::uint32_t>(j));
    return subset;
}

bool inGroup(std::span<const PauliString> rows, const PauliString& target, int* signOut) {
    const auto subset = solveProduct(rows, target);
    if (!subset)
        return false;
    if (signOut) {
        PauliString product = PauliString::identity(target.n);
        for (std::uint32_t j : *subset)
            product = product * rows[j];
        const int ratio = ((int(product.phase) - int(target.phase)) % 4 + 4) % 4;
        *signOut = ratio == 0 ? 1 : (ratio == 2 ? -1 : 0);
    }
    return true;
}

} // namespace qlab::qec
