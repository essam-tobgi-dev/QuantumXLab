// Spec 07 §3.2 — density-matrix operations.
#include "Core/Log.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Sampling.hpp"
#include "Numerics/Tensor.hpp"
#include "QSim/Detail.hpp"
#include "QSim/DensityMatrix.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qsim {

Status DensityMatrixBackend::applyOp(const Matrix& u, std::span<const QubitIndex> targets, std::span<const QubitIndex> controls) {
    if (!allocated_) return fail(err::NotAllocated, "backend not allocated");
    if (!validTargets(targets, n_)) return fail(err::BadTargets, "invalid or repeated target qubits");
    const std::size_t k = targets.size();
    if (k == 0 || k > 6) return fail(err::BadTargets, "gates act on 1–6 qubits");
    if (u.rows != u.cols) return fail(err::BadTargets, "matrix must be square");
    std::vector<std::uint32_t> sites(k), sd(k);
    std::size_t full = 1;
    for (std::size_t i = 0; i < k; ++i) { sites[i] = targets[i].value; sd[i] = dims_[sites[i]]; full *= sd[i]; }
    Matrix op;
    if (u.rows == (std::size_t{1} << k)) op = expandToSites(u, sd);
    else if (u.rows == full) op = u;
    else return fail(err::BadTargets, "matrix dimension matches neither the qubit subspace nor the full site dimension");
#ifdef QXL_DEV
    if (!num::isUnitary(u, 1e-10)) return fail(err::NotUnitary, "gate matrix is not unitary (tol 1e-10)"); // spec 07 §1
#endif
    std::vector<std::size_t> ctrl;
    for (auto c : controls) {
        if (c.value >= n_) return fail(err::BadTargets, "control out of range");
        for (auto t : targets) if (t == c) return fail(err::BadTargets, "control overlaps target");
        ctrl.push_back(c.value);
    }
    applyLeft(rho_, dims_, sites, op, ctrl);
    applyRightAdjoint(rho_, dims_, sites, op, ctrl);
    ++ops_;
    if (ops_ % 64 == 0) checkInvariants();
    return {};
}

Status DensityMatrixBackend::applyGate(const Matrix& u, std::span<const QubitIndex> targets) { return applyOp(u, targets, {}); }
Status DensityMatrixBackend::applyControlled(const Matrix& u, std::span<const QubitIndex> controls, std::span<const QubitIndex> targets) { return applyOp(u, targets, controls); }

Status DensityMatrixBackend::applyChannel(const Kraus& kraus, std::span<const QubitIndex> targets) {
    if (!allocated_) return fail(err::NotAllocated, "backend not allocated");
    if (kraus.empty()) return fail(err::BadKraus, "empty Kraus set");
    if (!validTargets(targets, n_)) return fail(err::BadTargets, "invalid targets");
    const std::size_t k = targets.size();
    if (k == 0 || k > 6) return fail(err::BadTargets, "channels act on 1–6 qubits");
    std::vector<std::uint32_t> sites(k), sd(k);
    std::size_t full = 1;
    for (std::size_t i = 0; i < k; ++i) { sites[i] = targets[i].value; sd[i] = dims_[sites[i]]; full *= sd[i]; }
    const std::size_t sub = std::size_t{1} << k;
    for (auto& K : kraus)
        if (K.rows != K.cols || K.rows != kraus.front().rows || (K.rows != sub && K.rows != full))
            return fail(err::BadKraus, std::format("Kraus operator is {}x{}; {} target site(s) need {}x{} (qubit subspace) or {}x{}",
                                                   K.rows, K.cols, k, sub, sub, full, full));
#ifdef QXL_DEV
    if (!num::isTracePreserving(kraus, 1e-10)) return fail(err::BadKraus, "Kraus set is not trace preserving (tol 1e-10)"); // T11 §9
#endif
    // Accumulate Σ K ρ K† in scratch_.
    std::fill(scratch_.data.begin(), scratch_.data.end(), Complex{});
    Matrix term;
    for (std::size_t idx = 0; idx < kraus.size(); ++idx) {
        const Matrix& K = kraus[idx];
        Matrix op = (K.rows == full) ? K : expandToSites(K, sd);
        // A qubit-subspace channel is the identity on the leakage levels: K_0 ⊕ 1 and K_{k>0} ⊕ 0 keeps
        // Σ K†K = 1 on the full site space (expandToSites alone would count |2⟩ once per operator).
        if (K.rows != full && idx > 0)
            for (std::size_t l = 0; l < full; ++l) {
                bool leaked = false; std::size_t r = l;
                for (std::size_t j = 0; j < k; ++j) { leaked = leaked || (r % sd[j]) > 1; r /= sd[j]; }
                if (leaked) op(l, l) = 0.0;
            }
        term = rho_;
        applyLeft(term, dims_, sites, op, {});
        applyRightAdjoint(term, dims_, sites, op, {});
        scratch_ += term;
    }
    std::swap(rho_, scratch_);
    ++ops_;
    if (ops_ % 64 == 0) checkInvariants();
    return {};
}

void DensityMatrixBackend::checkInvariants() {
    double tr = num::trace(rho_).real();
    double herm = 0.0;
    for (std::size_t i = 0; i < D_; ++i) for (std::size_t j = i + 1; j < D_; ++j) herm = std::max(herm, std::abs(rho_(i, j) - std::conj(rho_(j, i))));
    if (std::abs(tr - 1.0) > 1e-8) {
        QXL_LOG_WARN(Sim, "density matrix trace drift {:.3e}; renormalizing", tr - 1.0);
        rho_ *= 1.0 / tr;
    }
    if (herm > 1e-8) QXL_LOG_WARN(Sim, "density matrix Hermiticity drift {:.3e}", herm);
}

double DensityMatrixBackend::population(QubitIndex q, std::uint32_t level) const {
    double p = 0.0;
    for (std::size_t i = 0; i < D_; ++i)
        if ((i / strides_[q.value]) % dims_[q.value] == level) p += rho_(i, i).real();
    return p;
}

Result<Probabilities> DensityMatrixBackend::probabilities(std::span<const QubitIndex> qubits) const {
    if (!allocated_) return fail(err::NotAllocated, "backend not allocated");
    if (!validTargets(qubits, n_)) return fail(err::BadTargets, "invalid qubits");
    std::vector<QubitIndex> all;
    if (qubits.empty()) { // an empty request means the whole register in index order
        for (std::uint32_t i = 0; i < n_; ++i) all.push_back(QubitIndex{i});
        qubits = all;
    }
    Probabilities p(std::size_t{1} << qubits.size(), 0.0);
    double leak = 0.0;
    for (std::size_t i = 0; i < D_; ++i) {
        double w = rho_(i, i).real();
        std::size_t idx = 0; bool leaked = false;
        for (std::size_t k = 0; k < qubits.size(); ++k) {
            std::size_t dg = (i / strides_[qubits[k].value]) % dims_[qubits[k].value];
            if (dg > 1) { leaked = true; break; }
            idx |= dg << k;
        }
        if (leaked) { leak += w; continue; }
        p[idx] += w;
    }
    if (leak > 1e-12) { // project onto the computational subspace (spec 07 §5)
        double s = 0; for (auto v : p) s += v;
        if (s > 0) for (auto& v : p) v /= s;
        QXL_LOG_WARN(Sim, "leakage population {:.3e} discarded in measurement projection", leak);
    }
    return p;
}

Result<Counts> DensityMatrixBackend::sample(std::span<const QubitIndex> qubits, std::uint64_t shots, core::Random& rng) const {
    auto p = probabilities(qubits);
    if (!p) return std::unexpected(p.error());
    auto hist = num::sampleCounts(*p, shots, rng);
    const std::size_t nBits = qubits.empty() ? n_ : qubits.size();
    return countsFromHistogram(hist, nBits);
}

Result<Outcome> DensityMatrixBackend::measure(std::span<const QubitIndex> qubits, core::Random& rng) {
    if (!allocated_) return fail(err::NotAllocated, "backend not allocated");
    Outcome o;
    for (auto q : qubits) {
        if (q.value >= n_) return fail(err::BadTargets, "measured qubit out of range");
        double p1 = population(q, 1);
        double p0 = population(q, 0);
        double tot = p0 + p1;
        if (tot <= 0) return fail(ErrorCode::Internal, "no computational population left");
        bool one = rng.uniform() * tot < p1;
        double p = one ? p1 : p0;
        // ρ' = P ρ P / p
        for (std::size_t i = 0; i < D_; ++i) {
            bool ki = ((i / strides_[q.value]) % dims_[q.value]) == (one ? 1u : 0u);
            for (std::size_t j = 0; j < D_; ++j) {
                bool kj = ((j / strides_[q.value]) % dims_[q.value]) == (one ? 1u : 0u);
                rho_(i, j) = (ki && kj) ? rho_(i, j) / p : Complex{};
            }
        }
        o.bits.push_back(one ? 1 : 0);
        o.probability *= p / tot;
        ++ops_;
    }
    return o;
}

Status DensityMatrixBackend::reset(std::span<const QubitIndex> qubits, core::Random&) {
    // ρ ↦ tr_q(ρ) ⊗ |0⟩⟨0|_q
    for (auto q : qubits) {
        if (q.value >= n_) return fail(err::BadTargets, "reset qubit out of range");
        const std::size_t st = strides_[q.value]; const std::uint32_t d = dims_[q.value];
        std::fill(scratch_.data.begin(), scratch_.data.end(), Complex{});
        for (std::size_t i = 0; i < D_; ++i) {
            std::size_t di = (i / st) % d;
            std::size_t i0 = i - di * st;
            for (std::size_t j = 0; j < D_; ++j) {
                std::size_t dj = (j / st) % d;
                if (di != dj) continue;
                scratch_(i0, j - dj * st) += rho_(i, j);
            }
        }
        std::swap(rho_, scratch_);
        ++ops_;
    }
    return {};
}

Result<double> DensityMatrixBackend::expectation(const PauliString& ps) const {
    if (!allocated_) return fail(err::NotAllocated, "backend not allocated");
    if (ps.size() != n_) return fail(err::BadPauli, "Pauli string length mismatch");
    // tr(Pρ) = Σ_i Σ_j P_ij ρ_ji ; P_ij ≠ 0 only for j = i with X-bits flipped (qubit subspace only).
    Complex acc{};
    for (std::size_t i = 0; i < D_; ++i) {
        std::size_t j = i; Complex f = 1.0; bool ok = true;
        for (std::uint32_t q = 0; q < n_ && ok; ++q) {
            std::size_t di = (i / strides_[q]) % dims_[q];
            char c = ps.op(q);
            if (c == 'I') continue;
            if (di > 1) { ok = false; break; }
            if (c == 'X' || c == 'Y') {
                j = di ? j - strides_[q] : j + strides_[q];
                // ⟨i|P|j⟩ with j_q = 1 - i_q: X → 1; Y → ⟨0|Y|1⟩ = −i, ⟨1|Y|0⟩ = i
                if (c == 'Y') f *= di ? Complex(0, 1) : Complex(0, -1);
            } else if (c == 'Z' && di == 1) f = -f;
        }
        if (!ok) continue;
        acc += f * rho_(j, i);
    }
    return (acc * ps.phase()).real();
}

Result<Matrix> DensityMatrixBackend::reducedDensityMatrix(std::span<const QubitIndex> keep) const {
    if (!allocated_) return fail(err::NotAllocated, "backend not allocated");
    if (keep.empty()) return fail(err::BadTargets, "a reduced state needs at least one site");
    if (keep.size() > 8) return fail(err::TooLarge, "reduced states are limited to 8 qubits");
    if (!validTargets(keep, n_)) return fail(err::BadTargets, "invalid or repeated qubits in the reduced-state request");
    const std::vector<std::size_t> dims(dims_.begin(), dims_.end());
    std::vector<std::size_t> requested(keep.size());
    for (std::size_t i = 0; i < requested.size(); ++i) requested[i] = keep[i].value;
    std::vector<std::size_t> sorted = requested;
    std::sort(sorted.begin(), sorted.end());
    Matrix rho = num::partialTrace(rho_, dims, sorted);
    // keep[0] is the least significant index of the result, as for probabilities().
    if (sorted == requested) return rho;
    std::vector<std::size_t> sortedDims(sorted.size());
    for (std::size_t i = 0; i < sorted.size(); ++i) sortedDims[i] = dims_[sorted[i]];
    return detail::reorderSites(rho, sorted, sortedDims, requested);
}

Result<Snapshot> DensityMatrixBackend::snapshot(const SnapshotRequest& req) const {
    if (!allocated_) return fail(err::NotAllocated, "backend not allocated");
    Snapshot s; s.kind = Kind::DensityMatrix; s.nQubits = n_; s.levels = levels_; s.gateIndex = ops_; s.cls = FidelityClass::Exact;
    if (req.amplitudes) s.densityMatrix = rho_;
    if (req.probabilities) {
        std::vector<QubitIndex> all(n_); for (std::uint32_t i = 0; i < n_; ++i) all[i] = QubitIndex{i};
        auto p = probabilities(all); if (!p) return std::unexpected(p.error());
        s.probabilities = std::move(*p);
    }
    if (req.reducedStates)
        for (auto& sub : req.subsystems) { auto r = reducedDensityMatrix(sub); if (!r) return std::unexpected(r.error()); s.reduced.push_back({sub, std::move(*r)}); }
    return s;
}

} // namespace qlab::qsim
