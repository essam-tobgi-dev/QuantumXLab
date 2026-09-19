// Spec 07 §5 — IBackend surface of the Lindblad backend: measurement, sampling, snapshots, and
// projection onto the computational subspace.
#include "Core/Log.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Sampling.hpp"
#include "Numerics/Tensor.hpp"
#include "QSim/Detail.hpp"
#include "QSim/Lindblad.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <format>

namespace qlab::qsim {
namespace {
// Index of the computational basis state `bits` (little-endian) inside the mixed-radix space.
std::size_t computationalIndex(std::span<const std::uint32_t> dims, std::size_t bits) {
    std::size_t idx = 0, stride = 1;
    for (std::size_t s = 0; s < dims.size(); ++s) {
        idx += ((bits >> s) & 1) * stride;
        stride *= dims[s];
    }
    return idx;
}
} // namespace

Result<Matrix> LindbladBackend::computationalSubspace() const {
    if (!allocated_)
        return fail(err::NotAllocated, "Lindblad backend has no model");
    const std::size_t dim = std::size_t{1} << n_;
    Matrix out(dim, dim);
    for (std::size_t a = 0; a < dim; ++a)
        for (std::size_t b = 0; b < dim; ++b)
            out(a, b) = rho_(computationalIndex(model_.siteDims, a),
                             computationalIndex(model_.siteDims, b));
    Complex tr = 0;
    for (std::size_t a = 0; a < dim; ++a)
        tr += out(a, a);
    const double leak = leakage();
    if (leak > 1e-9)
        QXL_LOG_WARN(
            Sim,
            "projecting Lindblad state onto the computational subspace discards P_leak = {:.3e}",
            leak);
    if (tr.real() > 1e-12)
        for (auto& v : out.data)
            v /= tr.real();
    return out;
}

Status LindbladBackend::setRho(const Matrix& rho) {
    if (!allocated_)
        return fail(err::NotAllocated, "Lindblad backend has no model");
    if (rho.rows != D_ || rho.cols != D_)
        return fail(err::BadTargets, "density matrix dimension mismatch");
    rho_ = rho;
    if (settings_
            .recordTrajectory) { // the sample at the current time must show the installed state
        if (!samples_.empty() && samples_.back().timeS == t_)
            samples_.pop_back();
        recordSample();
    }
    return {};
}

Status LindbladBackend::setPure(std::span<const Complex> psi) {
    if (!allocated_)
        return fail(err::NotAllocated, "Lindblad backend has no model");
    Matrix rho(D_, D_);
    if (psi.size() == D_) {
        for (std::size_t i = 0; i < D_; ++i)
            for (std::size_t j = 0; j < D_; ++j)
                rho(i, j) = psi[i] * std::conj(psi[j]);
        return setRho(rho);
    }
    if (psi.size() == (std::size_t{1} << n_)) { // computational-subspace state
        for (std::size_t a = 0; a < psi.size(); ++a)
            for (std::size_t b = 0; b < psi.size(); ++b)
                rho(computationalIndex(model_.siteDims, a),
                    computationalIndex(model_.siteDims, b)) = psi[a] * std::conj(psi[b]);
        return setRho(rho);
    }
    return fail(err::BadTargets,
                "state length matches neither the full nor the computational dimension");
}

Status LindbladBackend::reset(std::span<const QubitIndex> qubits, core::Random&) {
    if (!allocated_)
        return fail(err::NotAllocated, "Lindblad backend has no model");
    // Ideal reset: trace out the site and re-prepare |0⟩ (spec 07 §6).
    for (auto q : qubits) {
        if (q.get() >= n_)
            return fail(err::BadTargets, "reset qubit out of range");
        const std::uint32_t site = q.get();
        std::size_t stride = 1;
        for (std::uint32_t k = 0; k < site; ++k)
            stride *= model_.siteDims[k];
        const std::uint32_t d = model_.siteDims[site];
        Matrix out(D_, D_);
        auto levelOf = [&](std::size_t idx) {
            return static_cast<std::uint32_t>((idx / stride) % d);
        };
        auto withLevel = [&](std::size_t idx, std::uint32_t lv) {
            return idx + (static_cast<std::size_t>(lv) - levelOf(idx)) * stride;
        };
        for (std::size_t i = 0; i < D_; ++i)
            for (std::size_t j = 0; j < D_; ++j) {
                if (levelOf(i) != levelOf(j))
                    continue; // off-diagonal in the traced site is discarded
                out(withLevel(i, 0), withLevel(j, 0)) += rho_(i, j);
            }
        rho_ = std::move(out);
    }
    return {};
}

Status LindbladBackend::applyGate(const Matrix& u, std::span<const QubitIndex> targets) {
    return applyControlled(u, {}, targets);
}

Status LindbladBackend::applyControlled(const Matrix&, std::span<const QubitIndex>,
                                        std::span<const QubitIndex>) {
    return fail(
        err::Unsupported,
        "the Lindblad backend evolves a pulse schedule; gate-level application is not defined "
        "(use a gate-level backend, or lower the circuit to pulses first)");
}

Status LindbladBackend::applyChannel(const Kraus&, std::span<const QubitIndex>) {
    return fail(err::Unsupported, "noise enters the Lindblad backend as collapse operators in the "
                                  "SystemModel, not as Kraus maps");
}

Result<Probabilities> LindbladBackend::probabilities(std::span<const QubitIndex> qubits) const {
    auto sub = computationalSubspace();
    if (!sub)
        return std::unexpected(sub.error());
    if (!validTargets(qubits, n_))
        return fail(err::BadTargets, "invalid or repeated qubits");
    std::vector<std::uint32_t> qs;
    for (auto q : qubits)
        qs.push_back(q.get());
    if (qs.empty())
        for (std::uint32_t q = 0; q < n_; ++q)
            qs.push_back(q);
    Probabilities p(std::size_t{1} << qs.size(), 0.0);
    const std::size_t dim = std::size_t{1} << n_;
    for (std::size_t idx = 0; idx < dim; ++idx) {
        std::size_t out = 0;
        for (std::size_t k = 0; k < qs.size(); ++k)
            out |= ((idx >> qs[k]) & 1) << k;
        p[out] += (*sub)(idx, idx).real();
    }
    return p;
}

Result<Outcome> LindbladBackend::measure(std::span<const QubitIndex> qubits, core::Random& rng) {
    if (!allocated_)
        return fail(err::NotAllocated, "Lindblad backend has no model");
    if (!validTargets(qubits, n_))
        return fail(err::BadTargets, "invalid or repeated measured qubits");
    if (qubits.empty())
        return Outcome{}; // nothing measured: probability 1, state untouched
    auto probs = probabilities(qubits);
    if (!probs)
        return std::unexpected(probs.error());
    double r = rng.uniform(), acc = 0;
    std::size_t pick = probs->size() - 1;
    for (std::size_t i = 0; i < probs->size(); ++i) {
        acc += (*probs)[i];
        if (r < acc) {
            pick = i;
            break;
        }
    }
    Outcome o;
    o.probability = (*probs)[pick];
    for (std::size_t k = 0; k < qubits.size(); ++k)
        o.bits.push_back(static_cast<std::uint8_t>((pick >> k) & 1));
    // Collapse: project the full ρ onto the measured computational outcome of those sites.
    std::vector<std::uint32_t> qs;
    for (auto q : qubits)
        qs.push_back(q.get());
    for (std::size_t i = 0; i < D_; ++i) {
        for (std::size_t j = 0; j < D_; ++j) {
            bool keep = true;
            for (std::size_t k = 0; k < qs.size() && keep; ++k) {
                std::size_t stride = 1;
                for (std::uint32_t s = 0; s < qs[k]; ++s)
                    stride *= model_.siteDims[s];
                const std::uint32_t d = model_.siteDims[qs[k]];
                std::uint32_t li = static_cast<std::uint32_t>((i / stride) % d);
                std::uint32_t lj = static_cast<std::uint32_t>((j / stride) % d);
                std::uint32_t want = o.bits[k];
                if (li != want || lj != want)
                    keep = false;
            }
            if (!keep)
                rho_(i, j) = 0;
        }
    }
    double tr = stateNorm();
    if (tr > 1e-15)
        for (auto& v : rho_.data)
            v /= tr;
    ++ops_;
    return o;
}

Result<Counts> LindbladBackend::sample(std::span<const QubitIndex> qubits, std::uint64_t shots,
                                       core::Random& rng) const {
    auto probs = probabilities(qubits);
    if (!probs)
        return std::unexpected(probs.error());
    // The sampler every amplitude/density backend shares, so one distribution and one seed give the
    // same counts whichever backend holds the state (T11 §2.7).
    const auto hist = num::sampleCounts(*probs, shots, rng);
    return countsFromHistogram(hist, qubits.empty() ? n_ : qubits.size());
}

Result<double> LindbladBackend::expectation(const PauliString& p) const {
    auto sub = computationalSubspace();
    if (!sub)
        return std::unexpected(sub.error());
    if (p.size() != n_)
        return fail(err::BadPauli, "Pauli string length does not match the qubit count");
    const std::size_t dim = std::size_t{1} << n_;
    Complex acc = 0;
    // Tr(ρP) = Σ_j ⟨j|ρP|j⟩ = Σ_j c_j ρ_{j, j⊕x} with P|j⟩ = c_j |j ⊕ x⟩, c_j carrying i^{n_Y} (T11
    // (2.3)).
    for (std::size_t j = 0; j < dim; ++j)
        acc += detail::pauliCoefficient(p, j) * (*sub)(j, j ^ p.xMask());
    return acc.real();
}

Result<Snapshot> LindbladBackend::snapshot(const SnapshotRequest& req) const {
    if (!allocated_)
        return fail(err::NotAllocated, "Lindblad backend has no model");
    Snapshot s;
    s.kind = Kind::Lindblad;
    s.nQubits = n_;
    s.levels = levels_;
    s.gateIndex = ops_;
    s.simTimePs = t_ * 1e12;
    s.cls = FidelityClass::Numerical;
    if (req.probabilities) {
        auto p = probabilities({});
        if (!p)
            return std::unexpected(p.error());
        s.probabilities = std::move(*p);
    }
    if (req.amplitudes)
        s.densityMatrix = rho_;
    if (req.reducedStates) {
        auto sub = computationalSubspace();
        if (!sub)
            return std::unexpected(sub.error());
        const std::vector<std::size_t> dims(n_, 2);
        for (auto& set : req.subsystems) {
            if (set.size() > 8)
                return fail(err::TooLarge, "reduced states are limited to 8 qubits");
            if (!validTargets(set, n_))
                return fail(err::BadTargets,
                            "invalid or repeated qubits in the reduced-state request");
            std::vector<std::size_t> keep, sorted;
            for (auto q : set)
                keep.push_back(q.get());
            sorted = keep;
            std::sort(sorted.begin(), sorted.end());
            const Matrix r = num::partialTrace(*sub, dims, sorted);
            s.reduced.push_back(
                {set, detail::reorderSites(r, sorted, std::vector<std::size_t>(sorted.size(), 2),
                                           keep)});
        }
    }
    return s;
}

std::unique_ptr<IBackend> LindbladBackend::clone() const {
    return std::make_unique<LindbladBackend>(*this);
}

} // namespace qlab::qsim
