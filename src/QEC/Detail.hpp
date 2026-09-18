#pragma once
// Internal helpers of qlab::qec (not part of the public API): Pauli strings on n ≤ 64 qubits as
// two machine words, used by the exhaustive distance search (spec 16 §1) and the lookup-table
// build (spec 16 §5, T09 §6.1).
#include "QEC/Pauli.hpp"
#include <bit>
#include <cstdint>
#include <span>
#include <vector>

namespace qlab::qec::detail {

struct Mask {
    std::uint64_t x = 0, z = 0;
    constexpr bool operator==(const Mask&) const = default;
    constexpr bool empty() const { return x == 0 && z == 0; }
};

inline Mask toMask(const PauliString& p) {
    Mask m;
    for (std::uint32_t q = 0; q < p.n && q < 64; ++q) {
        if (p.x[q]) m.x |= std::uint64_t{1} << q;
        if (p.z[q]) m.z |= std::uint64_t{1} << q;
    }
    return m;
}

inline PauliString fromMask(Mask m, std::uint32_t n) {
    PauliString p = PauliString::identity(n);
    for (std::uint32_t q = 0; q < n && q < 64; ++q) {
        p.x[q] = static_cast<std::uint8_t>((m.x >> q) & 1u);
        p.z[q] = static_cast<std::uint8_t>((m.z >> q) & 1u);
    }
    return p;
}

// Symplectic form T09 (1.2).
inline bool commute(Mask a, Mask b) { return (std::popcount((a.x & b.z) ^ (a.z & b.x)) & 1) == 0; }
inline int weight(Mask m) { return std::popcount(m.x | m.z); }
inline Mask operator^(Mask a, Mask b) { return {a.x ^ b.x, a.z ^ b.z}; }

// F2 span of (x|z) rows with a membership test. Every stored row is zero on the pivots of the rows
// stored before it, so one ordered pass reduces a candidate.
class MaskSpan {
public:
    MaskSpan() = default;
    explicit MaskSpan(std::span<const Mask> rows) {
        for (Mask m : rows) insert(m);
    }
    bool insert(Mask m) {
        m = reduce(m);
        if (m.empty()) return false;
        Row r{m, m.x != 0, 0};
        r.pivot = static_cast<unsigned>(std::countr_zero(r.inX ? m.x : m.z));
        rows_.push_back(r);
        return true;
    }
    bool contains(Mask m) const { return reduce(m).empty(); }
    std::size_t rank() const { return rows_.size(); }

private:
    struct Row { Mask m; bool inX; unsigned pivot; };
    Mask reduce(Mask m) const {
        for (const Row& r : rows_)
            if (((r.inX ? m.x : m.z) >> r.pivot) & 1u) m = m ^ r.m;
        return m;
    }
    std::vector<Row> rows_;
};

// Next k-subset of a machine word in lexicographic order (Gosper); 0 when `v` is the last one
// below `limit` = 1 << n.
inline std::uint64_t nextSubset(std::uint64_t v, std::uint64_t limit) {
    const std::uint64_t c = v & (~v + 1), r = v + c;
    if (c == 0 || r == 0) return 0;
    const std::uint64_t next = (((r ^ v) >> 2) / c) | r;
    return next < limit ? next : 0;
}

} // namespace qlab::qec::detail
