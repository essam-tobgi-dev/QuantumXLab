// Spec 07 §4, T09 §3.2 — stabilizer measurement, reset, exact probabilities, sampling, Pauli
// expectations and snapshots.
#include "QSim/Stabilizer.hpp"
#include <bit>
#include <format>

namespace qlab::qsim {

Result<bool> StabilizerBackend::measureOne(std::uint32_t q, core::Random& rng, double* prob) {
    std::size_t p = 0;
    bool random = false;
    for (std::size_t i = n_; i < 2 * std::size_t(n_); ++i)
        if (getX(i, q)) {
            p = i;
            random = true;
            break;
        }
    if (random) {
        for (std::size_t i = 0; i < 2 * std::size_t(n_); ++i)
            if (i != p && getX(i, q))
                rowsum(i, p);
        // destabilizer p−n ← row p; row p ← ±Z_q
        for (std::size_t w = 0; w < W_; ++w)
            words_[(p - n_) * W_ + w] = words_[p * W_ + w];
        r_[p - n_] = r_[p];
        for (std::size_t w = 0; w < W_; ++w)
            words_[p * W_ + w] = 0;
        setZ(p, q, true);
        bool out = rng.uniform() < 0.5;
        r_[p] = out ? 1 : 0;
        if (prob)
            *prob = 0.5;
        return out;
    }
    std::vector<std::uint64_t> acc(W_, 0);
    int phase = 0;
    for (std::size_t i = 0; i < n_; ++i)
        if (getX(i, q))
            rowsumInto(acc, phase, i + n_);
    if (prob)
        *prob = 1.0;
    return phase == 2;
}

Result<Outcome> StabilizerBackend::measure(std::span<const QubitIndex> qubits, core::Random& rng) {
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    for (auto q : qubits)
        if (q.value >= n_)
            return fail(err::BadTargets, "qubit out of range");
    Outcome o;
    for (auto q : qubits) {
        double p = 1.0;
        auto b = measureOne(q.value, rng, &p);
        if (!b)
            return std::unexpected(b.error());
        o.bits.push_back(*b ? 1 : 0);
        o.probability *= p;
        ++ops_;
    }
    return o;
}

Status StabilizerBackend::reset(std::span<const QubitIndex> qubits, core::Random& rng) {
    // Measure-then-flip (spec 07 §6).
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    for (auto q : qubits)
        if (q.value >= n_)
            return fail(err::BadTargets, "reset qubit out of range");
    for (auto q : qubits) {
        auto b = measureOne(q.value, rng, nullptr);
        if (!b)
            return std::unexpected(b.error());
        if (*b)
            x(q.value);
        ++ops_;
    }
    return {};
}

Result<Probabilities> StabilizerBackend::probabilities(std::span<const QubitIndex> qubits) const {
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    if (!validTargets(qubits, n_))
        return fail(err::BadTargets, "invalid or repeated qubits");
    std::vector<QubitIndex> all;
    if (qubits.empty()) { // an empty request means the whole register in index order
        for (std::uint32_t i = 0; i < n_; ++i)
            all.push_back(QubitIndex{i});
        qubits = all;
    }
    if (qubits.size() > 20)
        return fail(err::TooLarge, "probabilities limited to 20 qubits on the stabilizer backend");
    Probabilities p(std::size_t{1} << qubits.size(), 0.0);
    // Branch on random outcomes only: each random measurement halves the weight.
    struct Node {
        StabilizerBackend st;
        std::size_t idx;
        double w;
        std::size_t depth;
    };
    std::vector<Node> stack;
    stack.push_back({*this, 0, 1.0, 0});
    core::Random unused(1);
    while (!stack.empty()) {
        Node nd = std::move(stack.back());
        stack.pop_back();
        if (nd.depth == qubits.size()) {
            p[nd.idx] += nd.w;
            continue;
        }
        const std::uint32_t q = qubits[nd.depth].value;
        std::size_t pivot = 0;
        bool random = false;
        for (std::size_t i = n_; i < 2 * std::size_t(n_); ++i)
            if (nd.st.getX(i, q)) {
                pivot = i;
                random = true;
                break;
            }
        if (random) {
            for (int out = 0; out < 2; ++out) {
                Node c{nd.st, nd.idx, nd.w * 0.5, nd.depth + 1};
                double pr;
                (void)c.st.measureOne(q, unused, &pr);
                c.st.r_[pivot] = static_cast<std::uint8_t>(
                    out); // the pivot row now holds ±Z_q: force the branch
                if (out)
                    c.idx |= std::size_t{1} << nd.depth;
                stack.push_back(std::move(c));
            }
        } else {
            double pr;
            auto b = nd.st.measureOne(q, unused, &pr);
            if (*b)
                nd.idx |= std::size_t{1} << nd.depth;
            nd.depth += 1;
            stack.push_back(std::move(nd));
        }
    }
    return p;
}

Result<Counts> StabilizerBackend::sample(std::span<const QubitIndex> qubits, std::uint64_t shots,
                                         core::Random& rng) const {
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    if (!validTargets(qubits, n_))
        return fail(err::BadTargets, "invalid or repeated qubits");
    std::vector<QubitIndex> all;
    if (qubits.empty()) {
        for (std::uint32_t i = 0; i < n_; ++i)
            all.push_back(QubitIndex{i});
        qubits = all;
    }
    // Spec 07 §4: outcomes are correlated, so every shot re-measures a copy of the tableau.
    Counts c;
    for (std::uint64_t s = 0; s < shots; ++s) {
        StabilizerBackend copy(*this);
        auto o = copy.measure(qubits, rng);
        if (!o)
            return std::unexpected(o.error());
        ++c[bitsToKey(o->bits)];
    }
    return c;
}

Result<double> StabilizerBackend::expectation(const PauliString& ps) const {
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    if (ps.size() != n_)
        return fail(err::BadPauli, "Pauli string length mismatch");
    // Pack P as a tableau row from its letters (its 64-bit masks cannot hold a large register).
    std::vector<std::uint64_t> pw(W_, 0);
    for (std::uint32_t q = 0; q < n_; ++q) {
        const char c = ps.op(q);
        const std::uint64_t bit = std::uint64_t{1} << (q & 63);
        if (c == 'X' || c == 'Y')
            pw[q >> 6] |= bit;
        if (c == 'Z' || c == 'Y')
            pw[zOff_ + (q >> 6)] |= bit;
    }
    auto anticommutes = [&](std::size_t row) { // symplectic form T09 (1.2), word-parallel
        const std::uint64_t* rw = &words_[row * W_];
        unsigned parity = 0;
        for (std::size_t w = 0; w < zOff_; ++w)
            parity += static_cast<unsigned>(
                std::popcount((rw[w] & pw[zOff_ + w]) ^ (rw[zOff_ + w] & pw[w])));
        return (parity & 1u) != 0;
    };
    for (std::size_t i = n_; i < 2 * std::size_t(n_); ++i)
        if (anticommutes(i))
            return 0.0;
    // P commutes with every generator, so ±P is the product of the stabilizers whose destabilizers
    // anticommute with it (T09 §3.2, deterministic case).
    std::vector<std::uint64_t> acc(W_, 0);
    int phase = 0;
    for (std::size_t i = 0; i < n_; ++i)
        if (anticommutes(i))
            rowsumInto(acc, phase, i + n_);
    if (acc != pw)
        return 0.0;
    return (phase == 2 ? -1.0 : 1.0) * ps.phase().real();
}

Result<Snapshot> StabilizerBackend::snapshot(const SnapshotRequest& req) const {
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    Snapshot s;
    s.kind = Kind::Stabilizer;
    s.nQubits = n_;
    s.gateIndex = ops_;
    s.cls = FidelityClass::Exact;
    if (req.tableau)
        s.tableau = exportTableau();
    if (req.probabilities) {
        if (n_ > 20)
            return fail(err::TooLarge, "probability snapshots limited to 20 qubits");
        auto p = probabilities({});
        if (!p)
            return std::unexpected(p.error());
        s.probabilities = std::move(*p);
    }
    return s;
}

} // namespace qlab::qsim
