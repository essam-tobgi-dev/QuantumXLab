// Spec 07 §2 — state-vector backend (allocation, gate dispatch, measurement, sampling, snapshots).
#include "QSim/StateVector.hpp"
#include "Core/JobSystem.hpp"
#include "Core/Log.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Sampling.hpp"
#include "Numerics/Tensor.hpp"
#include "QSim/Detail.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qsim {
using num::Mat2;
using num::Mat4;

std::uint32_t StateVectorBackend::maxQubits() {
    return std::min<std::uint32_t>(maxQubitsFor(sizeof(Complex), false), 34);
}

Capabilities StateVectorBackend::capabilities() const {
    return {Kind::StateVector, maxQubits(), false, true, true, true, false, false, true};
}

Status StateVectorBackend::allocate(std::uint32_t n, std::uint32_t levels) {
    if (levels != 2)
        return fail(err::Unsupported, "state-vector backend supports 2 levels per site");
    if (n > maxQubits())
        return fail(err::TooLarge,
                    std::format("{} qubits need {} GiB; cap on this machine is {} qubits", n,
                                double(sizeof(Complex)) * std::ldexp(1.0, int(n)) / (1ull << 30),
                                maxQubits()));
    n_ = n;
    psi_.assign(std::size_t{1} << n, Complex{});
    psi_[0] = 1.0;
    allocated_ = true;
    ops_ = 0;
    QXL_LOG_INFO(Sim, "StateVector allocated: n={} bytes={} threads={}", n, bytesAllocated(),
                 core::JobSystem::global().workerCount());
    return {};
}

Status StateVectorBackend::setAmplitudes(std::span<const Complex> psi) {
    if (psi.size() != psi_.size())
        return fail(err::BadTargets, "amplitude count mismatch");
    std::copy(psi.begin(), psi.end(), psi_.begin());
    return {};
}

double StateVectorBackend::stateNorm() const {
    return std::sqrt(num::norm2Squared(psi_));
}

std::unique_ptr<IBackend> StateVectorBackend::clone() const {
    return std::make_unique<StateVectorBackend>(*this);
}

Status StateVectorBackend::applyImpl(const Matrix& u, std::span<const QubitIndex> targets,
                                     std::uint64_t ctrl, GateClass cls) {
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    if (!validTargets(targets, n_))
        return fail(err::BadTargets, "invalid or repeated target qubits");
    const std::size_t k = targets.size();
    if (k == 0 || k > 6)
        return fail(err::BadTargets, "gates act on 1–6 qubits; decompose larger gates");
    if (u.rows != (std::size_t{1} << k) || u.cols != u.rows)
        return fail(err::BadTargets, "matrix dimension does not match target count");
#ifdef QXL_DEV
    if (!num::isUnitary(u, 1e-10))
        return fail(err::NotUnitary, "gate matrix is not unitary (tol 1e-10)");
#endif
    ++ops_;
    if (k == 1) {
        const std::uint32_t t = targets[0].value;
        switch (cls) {
        case GateClass::Identity:
            return {};
        case GateClass::Diagonal:
            kernelDiag1(psi_, t, u(0, 0), u(1, 1), ctrl);
            return {};
        case GateClass::PauliX:
            kernelX(psi_, t, ctrl);
            return {};
        case GateClass::PauliZ:
            kernelZ(psi_, t, ctrl);
            return {};
        default:
            kernel1(psi_, n_, t, Mat2::fromMatrix(u), ctrl);
            return {};
        }
    }
    if (k == 2) {
        const std::uint32_t t0 = targets[0].value, t1 = targets[1].value;
        switch (cls) {
        case GateClass::Cnot:
            kernelCnot(psi_, t0, t1, ctrl);
            return {};
        case GateClass::Cz:
            kernelCz(psi_, t0, t1, ctrl);
            return {};
        case GateClass::Swap:
            kernelSwap(psi_, t0, t1, ctrl);
            return {};
        case GateClass::Diagonal2:
            kernelDiag2(psi_, t0, t1, Mat4::fromMatrix(u), ctrl);
            return {};
        default:
            kernel2(psi_, n_, t0, t1, Mat4::fromMatrix(u), ctrl);
            return {};
        }
    }
    std::vector<std::uint32_t> t(k);
    for (std::size_t i = 0; i < k; ++i)
        t[i] = targets[i].value;
    kernelK(psi_, n_, t, u, ctrl);
    return {};
}

Status StateVectorBackend::applyGate(const Matrix& u, std::span<const QubitIndex> targets) {
    return applyImpl(u, targets, 0, GateClass::Generic);
}

Status StateVectorBackend::applyControlled(const Matrix& u, std::span<const QubitIndex> controls,
                                           std::span<const QubitIndex> targets) {
    std::uint64_t mask = 0;
    for (auto c : controls) {
        if (c.value >= n_)
            return fail(err::BadTargets, "control qubit out of range");
        for (auto t : targets)
            if (t == c)
                return fail(err::BadTargets, "control overlaps target");
        mask |= std::uint64_t{1} << c.value;
    }
    return applyImpl(u, targets, mask, GateClass::Generic);
}

Status StateVectorBackend::apply(const GateOp& op) {
    std::uint64_t mask = 0;
    for (auto c : op.controls) {
        if (c.value >= n_)
            return fail(err::BadTargets, "control qubit out of range");
        mask |= std::uint64_t{1} << c.value;
    }
    auto r = applyImpl(op.matrix, op.targets, mask, op.cls);
    if (!r)
        r.error().notes.push_back(std::format("gate '{}' (op #{})", op.name, op.opIndex));
    return r;
}

Status StateVectorBackend::applyChannel(const Kraus&, std::span<const QubitIndex>) {
    return fail(err::Unsupported, "state-vector backend cannot apply Kraus maps exactly; use "
                                  "applyChannelStochastic or the density-matrix backend");
}

Status StateVectorBackend::applyChannelStochastic(const Kraus& kraus,
                                                  std::span<const QubitIndex> targets,
                                                  core::Random& rng) {
    if (kraus.empty())
        return fail(err::BadKraus, "empty Kraus set");
    // Probabilities p_k = ‖K_k ψ‖² computed by trial application on a copy; sequential O(|K| 2^n).
    double r = rng.uniform(), acc = 0.0;
    num::Vector saved = psi_;
    for (std::size_t k = 0; k < kraus.size(); ++k) {
        psi_ = saved;
        std::vector<std::uint32_t> t(targets.size());
        for (std::size_t i = 0; i < t.size(); ++i)
            t[i] = targets[i].value;
        if (targets.size() == 1)
            kernel1(psi_, n_, t[0], Mat2::fromMatrix(kraus[k]), 0);
        else if (targets.size() == 2)
            kernel2(psi_, n_, t[0], t[1], Mat4::fromMatrix(kraus[k]), 0);
        else
            kernelK(psi_, n_, t, kraus[k], 0);
        double p = num::norm2Squared(psi_);
        acc += p;
        if (r < acc || k + 1 == kraus.size()) {
            if (p <= 0) {
                psi_ = saved;
                return {};
            }
            double s = 1.0 / std::sqrt(p);
            for (auto& a : psi_)
                a *= s;
            ++ops_;
            return {};
        }
    }
    return {};
}

double StateVectorBackend::probOne(std::uint32_t q) const {
    const std::uint64_t m = std::uint64_t{1} << q;
    double p = 0.0;
    for (std::size_t i = 0; i < psi_.size(); ++i)
        if (i & m)
            p += std::norm(psi_[i]);
    return p;
}

void StateVectorBackend::collapse(std::uint32_t q, bool one, double p) {
    const std::uint64_t m = std::uint64_t{1} << q;
    const double s = 1.0 / std::sqrt(p);
    forRange(psi_.size(), [&](std::size_t b0, std::size_t e0) {
        for (std::size_t i = b0; i < e0; ++i) {
            bool bit = (i & m) != 0;
            psi_[i] = (bit == one) ? psi_[i] * s : Complex{};
        }
    });
}

Result<Outcome> StateVectorBackend::measure(std::span<const QubitIndex> qubits, core::Random& rng) {
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    Outcome o;
    for (auto q : qubits) {
        if (q.value >= n_)
            return fail(err::BadTargets, "measured qubit out of range");
        double p1 = probOne(q.value);
        bool one = rng.uniform() < p1;
        double p = one ? p1 : 1.0 - p1;
        if (p <= 0) {
            one = !one;
            p = 1.0 - p;
        }
        collapse(q.value, one, p);
        o.bits.push_back(one ? 1 : 0);
        o.probability *= p;
        ++ops_;
    }
    return o;
}

Status StateVectorBackend::reset(std::span<const QubitIndex> qubits, core::Random& rng) {
    for (auto q : qubits) {
        QubitIndex one[] = {q};
        auto r = measure(one, rng);
        if (!r)
            return std::unexpected(r.error());
        if (r->bits[0])
            kernelX(psi_, q.value, 0);
    }
    return {};
}

Result<Probabilities> StateVectorBackend::probabilities(std::span<const QubitIndex> qubits) const {
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    if (!validTargets(qubits, n_))
        return fail(err::BadTargets, "invalid qubits");
    std::vector<QubitIndex> all;
    if (qubits.empty()) { // an empty request means the whole register in index order
        for (std::uint32_t i = 0; i < n_; ++i)
            all.push_back(QubitIndex{i});
        qubits = all;
    }
    Probabilities p(std::size_t{1} << qubits.size(), 0.0);
    for (std::size_t i = 0; i < psi_.size(); ++i) {
        double w = std::norm(psi_[i]);
        if (w == 0.0)
            continue;
        std::size_t idx = 0;
        for (std::size_t k = 0; k < qubits.size(); ++k)
            if (i & (std::uint64_t{1} << qubits[k].value))
                idx |= std::size_t{1} << k;
        p[idx] += w;
    }
    return p;
}

Result<Counts> StateVectorBackend::sample(std::span<const QubitIndex> qubits, std::uint64_t shots,
                                          core::Random& rng) const {
    auto p = probabilities(qubits);
    if (!p)
        return std::unexpected(p.error());
    auto hist = num::sampleCounts(*p, shots, rng);
    const std::size_t nBits = qubits.empty() ? n_ : qubits.size();
    return countsFromHistogram(hist, nBits);
}

Result<double> StateVectorBackend::expectation(const PauliString& ps) const {
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    if (ps.size() != n_)
        return fail(err::BadPauli,
                    std::format("Pauli string has {} qubits, state has {}", ps.size(), n_));
    // ⟨ψ|P|ψ⟩ with P = phase · Π: P|i⟩ = phase · (−1)^{popcount(i & z)} · i^{#Y} … handled via
    // per-qubit factors.
    const std::uint64_t x = ps.xMask(), z = ps.zMask();
    Complex acc{};
    for (std::size_t i = 0; i < psi_.size(); ++i) {
        std::size_t j = i ^ x;
        // ⟨i|P|j⟩ where P|j⟩ ∝ |j ^ x⟩ = |i⟩. Factor: Π_q (X,Y,Z action on bit j_q).
        Complex f = 1.0;
        for (std::uint32_t q = 0; q < n_; ++q) {
            const std::uint64_t m = std::uint64_t{1} << q;
            bool bx = x & m, bz = z & m, bit = j & m;
            if (bx && bz)
                f *= bit ? Complex(0, -1) : Complex(0, 1); // Y|0⟩ = i|1⟩, Y|1⟩ = −i|0⟩
            else if (bz && bit)
                f = -f;
        }
        acc += std::conj(psi_[i]) * f * psi_[j];
    }
    acc *= ps.phase();
    return acc.real();
}

Result<Matrix> StateVectorBackend::reducedDensityMatrix(std::span<const QubitIndex> keep) const {
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    if (keep.empty())
        return fail(err::BadTargets, "a reduced state needs at least one qubit");
    if (keep.size() > 8)
        return fail(err::TooLarge, "reduced states are limited to 8 qubits");
    if (!validTargets(keep, n_))
        return fail(err::BadTargets, "invalid or repeated qubits in the reduced-state request");
    std::vector<std::size_t> requested(keep.size());
    for (std::size_t i = 0; i < requested.size(); ++i)
        requested[i] = keep[i].value;
    std::vector<std::size_t> sorted = requested;
    std::sort(sorted.begin(), sorted.end());
    Matrix rho = num::reducedState(psi_, n_, sorted);
    // keep[0] is the least significant index of the result, as for probabilities().
    if (sorted == requested)
        return rho;
    return detail::reorderSites(rho, sorted, std::vector<std::size_t>(sorted.size(), 2), requested);
}

Result<Snapshot> StateVectorBackend::snapshot(const SnapshotRequest& req) const {
    if (!allocated_)
        return fail(err::NotAllocated, "backend not allocated");
    Snapshot s;
    s.kind = Kind::StateVector;
    s.nQubits = n_;
    s.gateIndex = ops_;
    s.cls = FidelityClass::Exact;
    if (req.amplitudes) {
        if (n_ > 24)
            return fail(err::TooLarge, "amplitude snapshots are refused above 24 qubits");
        s.amplitudes = std::vector<Complex>(psi_.begin(), psi_.end());
    }
    if (req.probabilities)
        s.probabilities = num::probabilities(psi_);
    if (req.reducedStates)
        for (auto& sub : req.subsystems) {
            auto r = reducedDensityMatrix(sub);
            if (!r)
                return std::unexpected(r.error());
            s.reduced.push_back({sub, std::move(*r)});
        }
    return s;
}

} // namespace qlab::qsim
