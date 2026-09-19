#include "QSim/Types.hpp"
#include <format>
namespace qlab::qsim {

std::string_view kindName(Kind k) {
    switch (k) {
    case Kind::StateVector:
        return "StateVector";
    case Kind::DensityMatrix:
        return "DensityMatrix";
    case Kind::Stabilizer:
        return "Stabilizer";
    case Kind::Lindblad:
        return "Lindblad";
    case Kind::Trajectories:
        return "Trajectories";
    }
    return "?";
}

Result<PauliString> PauliString::parse(std::string_view label) {
    PauliString p;
    std::size_t i = 0;
    // optional phase prefix
    if (i < label.size() && (label[i] == '+' || label[i] == '-')) {
        if (label[i] == '-')
            p.phase_ = -1.0;
        ++i;
    }
    if (i < label.size() && label[i] == 'i') {
        p.phase_ *= Complex(0, 1);
        ++i;
    }
    std::string_view body = label.substr(i);
    if (body.empty())
        return fail(err::BadPauli, "empty Pauli string");
    const std::size_t n = body.size();
    p.ops_.assign(n, 'I');
    for (std::size_t k = 0; k < n; ++k) {
        const char c = body[n - 1 - k]; // MSB-first label: last char is qubit 0
        if (c != 'I' && c != 'X' && c != 'Y' && c != 'Z')
            return fail(err::BadPauli, std::format("invalid Pauli letter '{}'", c));
        p.setLetter(k, c);
    }
    return p;
}

PauliString PauliString::fromQubits(std::size_t n,
                                    std::span<const std::pair<QubitIndex, char>> terms,
                                    Complex phase) {
    PauliString p;
    p.ops_.assign(n, 'I');
    p.phase_ = phase;
    for (auto& [q, c] : terms)
        if (q.value < n)
            p.setLetter(q.value, c);
    return p;
}

// The masks only hold qubits 0..63: shifting a 64-bit word by ≥ 64 is undefined (the overflow that
// broke expectations on large stabilizer registers).
void PauliString::setLetter(std::size_t k, char c) {
    ops_[k] = c;
    if (k >= 64)
        return;
    const std::uint64_t bit = std::uint64_t{1} << k;
    x_ &= ~bit;
    z_ &= ~bit;
    if (c == 'X' || c == 'Y')
        x_ |= bit;
    if (c == 'Z' || c == 'Y')
        z_ |= bit;
}

bool PauliString::isIdentity() const {
    for (char c : ops_)
        if (c == 'X' || c == 'Y' || c == 'Z')
            return false;
    return true;
}

std::string PauliString::label() const {
    std::string s(ops_.size(), 'I');
    for (std::size_t k = 0; k < ops_.size(); ++k)
        s[ops_.size() - 1 - k] = ops_[k];
    return s;
}

std::string bitsToKey(std::span<const std::uint8_t> bits) {
    std::string s(bits.size(), '0');
    for (std::size_t i = 0; i < bits.size(); ++i)
        s[bits.size() - 1 - i] = bits[i] ? '1' : '0';
    return s;
}

Counts countsFromIndices(std::span<const std::size_t> idx, std::size_t nBits) {
    Counts c;
    for (auto i : idx) {
        std::string s(nBits, '0');
        for (std::size_t b = 0; b < nBits; ++b)
            if (i & (std::size_t{1} << b))
                s[nBits - 1 - b] = '1';
        ++c[s];
    }
    return c;
}

Counts countsFromHistogram(std::span<const std::size_t> hist, std::size_t nBits) {
    Counts c;
    for (std::size_t i = 0; i < hist.size(); ++i) {
        if (hist[i] == 0)
            continue;
        std::string s(nBits, '0');
        for (std::size_t b = 0; b < nBits; ++b)
            if (i & (std::size_t{1} << b))
                s[nBits - 1 - b] = '1';
        c[s] += hist[i];
    }
    return c;
}

bool validTargets(std::span<const QubitIndex> t, std::uint32_t n) {
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i].value >= n)
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (t[i] == t[j])
                return false;
    }
    return true;
}

} // namespace qlab::qsim
