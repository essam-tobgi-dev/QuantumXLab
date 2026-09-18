// Spec 07 §5.1 — remaining IBackend surface of the trajectories backend. Measurement and sampling
// act on the current trajectory's state, projected onto the computational subspace; ensemble
// quantities come from samples().
#include "Numerics/Sampling.hpp"
#include "Numerics/Tensor.hpp"
#include "QSim/Detail.hpp"
#include "QSim/Trajectories.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace qlab::qsim {
namespace {
std::size_t computationalIndex(std::span<const std::uint32_t> dims, std::size_t bits) {
    std::size_t idx = 0, stride = 1;
    for (std::size_t s = 0; s < dims.size(); ++s) {
        idx += ((bits >> s) & 1) * stride;
        stride *= dims[s];
    }
    return idx;
}
} // namespace

Status TrajectoriesBackend::reset(std::span<const QubitIndex> qubits, core::Random& rng) {
    if (!allocated_) return fail(err::NotAllocated, "Trajectories backend has no model");
    for (auto q : qubits) {
        if (q.get() >= n_) return fail(err::BadTargets, "reset qubit out of range");
        // Measure-then-flip (spec 07 §6) on a pure state.
        std::array<QubitIndex, 1> one{q};
        auto o = measure(one, rng);
        if (!o) return std::unexpected(o.error());
        if (o->bits[0] == 1) {
            const std::uint32_t site = q.get();
            std::size_t stride = 1;
            for (std::uint32_t s = 0; s < site; ++s) stride *= model_.siteDims[s];
            const std::uint32_t d = model_.siteDims[site];
            num::Vector out(D_, Complex{});
            for (std::size_t idx = 0; idx < D_; ++idx) {
                std::uint32_t lv = static_cast<std::uint32_t>((idx / stride) % d);
                if (lv == 1) out[idx - stride] = psi_[idx];
                else if (lv != 0) out[idx] = psi_[idx];
            }
            psi_ = std::move(out);
        }
    }
    return {};
}

Status TrajectoriesBackend::applyGate(const Matrix&, std::span<const QubitIndex>) {
    return fail(err::Unsupported, "the trajectories backend evolves a pulse schedule, not gates");
}
Status TrajectoriesBackend::applyControlled(const Matrix&, std::span<const QubitIndex>,
                                            std::span<const QubitIndex>) {
    return fail(err::Unsupported, "the trajectories backend evolves a pulse schedule, not gates");
}
Status TrajectoriesBackend::applyChannel(const Kraus&, std::span<const QubitIndex>) {
    return fail(err::Unsupported, "noise enters as collapse operators in the SystemModel");
}

Result<Probabilities> TrajectoriesBackend::probabilities(std::span<const QubitIndex> qubits) const {
    if (!allocated_) return fail(err::NotAllocated, "Trajectories backend has no model");
    if (!validTargets(qubits, n_)) return fail(err::BadTargets, "invalid or repeated qubits");
    std::vector<std::uint32_t> qs;
    for (auto q : qubits) qs.push_back(q.get());
    if (qs.empty()) for (std::uint32_t q = 0; q < n_; ++q) qs.push_back(q); // whole register, index order
    Probabilities p(std::size_t{1} << qs.size(), 0.0);
    const std::size_t dim = std::size_t{1} << n_;
    double total = 0;
    for (std::size_t bits = 0; bits < dim; ++bits) {
        double amp = std::norm(psi_[computationalIndex(model_.siteDims, bits)]);
        std::size_t out = 0;
        for (std::size_t k = 0; k < qs.size(); ++k) out |= ((bits >> qs[k]) & 1) << k;
        p[out] += amp;
        total += amp;
    }
    if (total > 1e-300) for (auto& v : p) v /= total;
    return p;
}

Result<Outcome> TrajectoriesBackend::measure(std::span<const QubitIndex> qubits, core::Random& rng) {
    if (!allocated_) return fail(err::NotAllocated, "Trajectories backend has no model");
    if (!validTargets(qubits, n_)) return fail(err::BadTargets, "invalid or repeated measured qubits");
    if (qubits.empty()) return Outcome{}; // nothing measured: probability 1, state untouched
    auto probs = probabilities(qubits);
    if (!probs) return std::unexpected(probs.error());
    double r = rng.uniform(), acc = 0;
    std::size_t pick = probs->size() - 1;
    for (std::size_t i = 0; i < probs->size(); ++i) {
        acc += (*probs)[i];
        if (r < acc) { pick = i; break; }
    }
    Outcome o;
    o.probability = (*probs)[pick];
    for (std::size_t k = 0; k < qubits.size(); ++k) o.bits.push_back(static_cast<std::uint8_t>((pick >> k) & 1));
    std::vector<std::uint32_t> qs;
    for (auto q : qubits) qs.push_back(q.get());
    double norm2 = 0;
    for (std::size_t idx = 0; idx < D_; ++idx) {
        bool keep = true;
        for (std::size_t k = 0; k < qs.size() && keep; ++k) {
            std::size_t stride = 1;
            for (std::uint32_t s = 0; s < qs[k]; ++s) stride *= model_.siteDims[s];
            std::uint32_t lv = static_cast<std::uint32_t>((idx / stride) % model_.siteDims[qs[k]]);
            if (lv != o.bits[k]) keep = false;
        }
        if (!keep) psi_[idx] = 0;
        else norm2 += std::norm(psi_[idx]);
    }
    if (norm2 > 1e-300) {
        double s = 1.0 / std::sqrt(norm2);
        for (auto& c : psi_) c *= s;
    }
    ++ops_;
    return o;
}

Result<double> TrajectoriesBackend::expectation(const PauliString& p) const {
    if (!allocated_) return fail(err::NotAllocated, "Trajectories backend has no model");
    if (p.size() != n_) return fail(err::BadPauli, "Pauli string length does not match the qubit count");
    // ⟨ψ|P|ψ⟩ = Σ_j conj(ψ_{j⊕x}) c_j ψ_j over the computational subspace (renormalised like
    // probabilities()), with c_j carrying i^{n_Y} (T11 (2.3)).
    const std::size_t dim = std::size_t{1} << n_;
    Complex acc = 0;
    double norm2 = 0;
    for (std::size_t j = 0; j < dim; ++j) {
        const Complex amp = psi_[computationalIndex(model_.siteDims, j)];
        norm2 += std::norm(amp);
        acc += std::conj(psi_[computationalIndex(model_.siteDims, j ^ p.xMask())]) * detail::pauliCoefficient(p, j) * amp;
    }
    return norm2 > 1e-300 ? acc.real() / norm2 : 0.0;
}

Result<Counts> TrajectoriesBackend::sample(std::span<const QubitIndex> qubits, std::uint64_t shots,
                                           core::Random& rng) const {
    auto probs = probabilities(qubits);
    if (!probs) return std::unexpected(probs.error());
    const auto hist = num::sampleCounts(*probs, shots, rng); // the sampler shared with SV/DM/Lindblad (T11 §2.7)
    return countsFromHistogram(hist, qubits.empty() ? n_ : qubits.size());
}

Result<Snapshot> TrajectoriesBackend::snapshot(const SnapshotRequest& req) const {
    if (!allocated_) return fail(err::NotAllocated, "Trajectories backend has no model");
    Snapshot s;
    s.kind = Kind::Trajectories;
    s.nQubits = n_;
    s.levels = levels_;
    s.gateIndex = ops_;
    s.cls = FidelityClass::Statistical;
    if (req.amplitudes) s.amplitudes = std::vector<Complex>(psi_.begin(), psi_.end());
    if (req.probabilities) {
        auto p = probabilities({});
        if (!p) return std::unexpected(p.error());
        s.probabilities = std::move(*p);
    }
    return s;
}

} // namespace qlab::qsim
