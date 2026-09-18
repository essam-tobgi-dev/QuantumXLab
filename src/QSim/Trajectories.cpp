// Spec 07 §5.1, T04 §7, T11 §7 — Monte-Carlo wave-function backend: model installation and the
// waiting-time jump algorithm averaged over seeded trajectories.
#include "QSim/Trajectories.hpp"
#include "Core/Log.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Matrix.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qsim {

Capabilities TrajectoriesBackend::capabilities() const {
    return Capabilities{Kind::Trajectories, kMaxSites, false, true, true, true, true, true, true};
}

Status TrajectoriesBackend::allocate(std::uint32_t nQubits, std::uint32_t levelsPerSite) {
    if (nQubits == 0 || nQubits > kMaxSites)
        return fail(err::TooLarge, std::format("Trajectories backend supports 1..{} sites, {} requested", kMaxSites, nQubits));
    if (levelsPerSite < 2) return fail(err::Unsupported, "site dimension must be at least 2");
    SystemModel m;
    m.siteDims.assign(nQubits, levelsPerSite);
    if (m.dimension() > kMaxDim)
        return fail(err::TooLarge, std::format("trajectories dimension {} exceeds the cap {}", m.dimension(), kMaxDim));
    m.h0 = Matrix(m.dimension(), m.dimension());
    m.frameFrequenciesHz.assign(nQubits, 0.0);
    return setModel(std::move(m));
}

Status TrajectoriesBackend::setModel(SystemModel model) {
    if (model.siteDims.empty()) return fail(err::BadTargets, "system model has no sites");
    if (model.siteDims.size() > kMaxSites)
        return fail(err::TooLarge, std::format("Trajectories backend supports at most {} sites", kMaxSites));
    // The cost bound is the total dimension below, not the per-site one: an ion chain is qubit
    // sites of dimension 2 plus one motional mode whose dimension is the Fock cutoff (8 by
    // default, spec 09 §5.4), which a per-site cap of 5 would refuse outright.
    for (auto d : model.siteDims) {
        if (d < 2) return fail(err::Unsupported, "site dimension must be at least 2");
        if (d > kMaxDim)
            return fail(err::TooLarge, std::format("site dimension {} exceeds the cap {}", d, kMaxDim));
    }
    const std::size_t D = model.dimension();
    if (D > kMaxDim) return fail(err::TooLarge, std::format("trajectories dimension {} exceeds the cap {}", D, kMaxDim));
    auto square = [D](const Matrix& m) { return m.rows == D && m.cols == D; };
    if (!square(model.h0)) return fail(err::BadTargets, "H0 dimension mismatch");
    if (!num::isHermitian(model.h0, 1e-9)) return fail(err::NotUnitary, "H0 is not Hermitian");
    for (auto& d : model.drives) {
        if (!square(d.inPhase) || !square(d.quadrature))
            return fail(err::BadTargets, std::format("drive '{}' operators do not match the system dimension {}", d.channel, D));
        if (!num::isHermitian(d.inPhase, 1e-9) || !num::isHermitian(d.quadrature, 1e-9))
            return fail(err::NotUnitary, std::format("drive '{}' quadrature operators must be Hermitian", d.channel));
    }
    Matrix dagLSum(D, D); // built aside: a rejected model leaves the installed one intact
    for (auto& c : model.collapse) {
        if (!square(c.op)) return fail(err::BadTargets, std::format("collapse operator '{}' dimension mismatch", c.name));
        dagLSum += num::matmul(num::adjoint(c.op), c.op);
    }
    lDagLSum_ = std::move(dagLSum);
    model_ = std::move(model);
    n_ = static_cast<std::uint32_t>(model_.siteDims.size());
    levels_ = *std::max_element(model_.siteDims.begin(), model_.siteDims.end());
    D_ = D;
    psi_.assign(D_, Complex{});
    psi_[0] = 1.0;
    heffCache_ = Matrix(D_, D_);
    samples_.clear();
    allocated_ = true;
    return {};
}

void TrajectoriesBackend::hamiltonianAt(double t, Matrix& h) const {
    std::copy(model_.h0.data.begin(), model_.h0.data.end(), h.data.begin());
    for (const auto& d : model_.drives) {
        if (!d.envelope) continue;
        Complex omega = d.envelope(t);
        if (omega == Complex{}) continue;
        for (std::size_t i = 0; i < h.data.size(); ++i)
            h.data[i] += omega.real() * d.inPhase.data[i] + omega.imag() * d.quadrature.data[i];
    }
}

void TrajectoriesBackend::applyHeff(double t, std::span<const Complex> in, std::span<Complex> out) {
    // dψ/dt = −i H_eff ψ = (−iH − ½ Σ L†L) ψ, with envelopes sampled inside the current grid segment
    // (one-sided at AWG sample boundaries, as in the Lindblad backend).
    hamiltonianAt(std::clamp(t, segLo_, segHi_), heffCache_);
    for (std::size_t i = 0; i < heffCache_.data.size(); ++i)
        heffCache_.data[i] = Complex(0, -1) * heffCache_.data[i] - 0.5 * lDagLSum_.data[i];
    num::matvecInto(heffCache_, in, out);
}

void TrajectoriesBackend::writePopulations(std::span<double> row) const {
    double norm2 = 0;
    for (auto& c : psi_) norm2 += std::norm(c);
    if (norm2 < 1e-300) norm2 = 1;
    std::size_t col = 0;
    for (std::uint32_t site = 0, stride = 1; site < n_; stride *= model_.siteDims[site], ++site)
        for (std::uint32_t lv = 0; lv < model_.siteDims[site]; ++lv, ++col) {
            double p = 0;
            for (std::size_t idx = 0; idx < D_; ++idx)
                if ((idx / stride) % model_.siteDims[site] == lv) p += std::norm(psi_[idx]);
            row[col] = p / norm2;
        }
}

Status TrajectoriesBackend::oneTrajectory(std::span<const double> ends, const std::vector<bool>& recordAt,
                                          core::Random& rng, std::span<double> pops) {
    const std::size_t cols = pops.size() / (1 + static_cast<std::size_t>(std::count(recordAt.begin(), recordAt.end(), true)));
    std::fill(psi_.begin(), psi_.end(), Complex{});
    psi_[0] = 1.0;
    num::Vector k1(D_), k2(D_), k3(D_), k4(D_), tmp(D_);
    double threshold = rng.uniform(); // waiting-time form (T04 §7): jump when ‖ψ‖² falls to r
    writePopulations(pops.subspan(0, cols));
    std::size_t rec = 1;
    double t = 0.0;
    for (std::size_t s = 0; s < ends.size(); ++s) {
        const double a = t, b = ends[s], eps = 1e-9 * (b - a);
        segLo_ = a + eps;
        segHi_ = b - eps;
        const auto steps = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::ceil((b - a) / settings_.stepS - 1e-9)));
        const double h = (b - a) / static_cast<double>(steps);
        for (std::uint64_t i = 0; i < steps; ++i) {
            const double ti = a + static_cast<double>(i) * h;
            // RK4 on the non-Hermitian effective Hamiltonian (the norm decays).
            applyHeff(ti, psi_, k1);
            for (std::size_t j = 0; j < D_; ++j) tmp[j] = psi_[j] + 0.5 * h * k1[j];
            applyHeff(ti + 0.5 * h, tmp, k2);
            for (std::size_t j = 0; j < D_; ++j) tmp[j] = psi_[j] + 0.5 * h * k2[j];
            applyHeff(ti + 0.5 * h, tmp, k3);
            for (std::size_t j = 0; j < D_; ++j) tmp[j] = psi_[j] + h * k3[j];
            applyHeff(ti + h, tmp, k4);
            for (std::size_t j = 0; j < D_; ++j) psi_[j] += (h / 6.0) * (k1[j] + 2.0 * k2[j] + 2.0 * k3[j] + k4[j]);
            double norm2 = 0;
            for (auto& c : psi_) norm2 += std::norm(c);
            if (norm2 > threshold) continue;
            // Jump: channel k with probability ‖L_k ψ‖² / Σ_j ‖L_j ψ‖², then renormalise.
            std::vector<num::Vector> applied;
            std::vector<double> weights;
            double total = 0;
            for (const auto& c : model_.collapse) {
                applied.push_back(num::matvec(c.op, psi_));
                weights.push_back(num::norm2Squared(applied.back()));
                total += weights.back();
            }
            if (total > 1e-300) {
                const double r = rng.uniform() * total;
                std::size_t pick = weights.size() - 1;
                double acc = 0;
                for (std::size_t k = 0; k < weights.size(); ++k) {
                    acc += weights[k];
                    if (r < acc) { pick = k; break; }
                }
                const double nrm = std::sqrt(weights[pick]);
                for (std::size_t j = 0; j < D_; ++j) psi_[j] = applied[pick][j] / nrm;
            }
            threshold = rng.uniform();
        }
        t = b;
        if (recordAt[s]) writePopulations(pops.subspan(cols * rec++, cols));
    }
    // Final renormalisation of the surviving no-jump amplitude.
    const double norm2 = num::norm2Squared(psi_);
    if (norm2 > 1e-300)
        for (auto& c : psi_) c /= std::sqrt(norm2);
    return {};
}

Status TrajectoriesBackend::runEnsemble(double durationS, core::Random& rng) {
    if (!allocated_) return fail(err::NotAllocated, "Trajectories backend has no model");
    if (!std::isfinite(durationS) || durationS < 0) return fail(ErrorCode::InvalidArgument, "run duration must be finite and non-negative");
    if (!(settings_.stepS > 0) || !(settings_.sampleS > 0))
        return fail(ErrorCode::InvalidArgument, "trajectory step and sample spacing must be positive");
    // Segments on the sampleS grid, the last ending exactly at durationS, which is always recorded.
    std::vector<double> ends;
    if (durationS > 0) {
        const auto whole = static_cast<std::uint64_t>(std::floor(durationS / settings_.sampleS + 1e-9));
        double rest = durationS - static_cast<double>(whole) * settings_.sampleS;
        if (rest <= 1e-9 * settings_.sampleS) rest = 0.0;
        for (std::uint64_t k = 1; k <= whole; ++k)
            ends.push_back(k == whole && rest == 0.0 ? durationS : static_cast<double>(k) * settings_.sampleS);
        if (rest > 0.0) ends.push_back(durationS);
    }
    std::vector<bool> recordAt(ends.size(), settings_.recordAverages);
    if (!recordAt.empty()) recordAt.back() = true;
    std::vector<double> times{0.0};
    for (std::size_t s = 0; s < ends.size(); ++s) if (recordAt[s]) times.push_back(ends[s]);
    std::size_t cols = 0;
    for (auto d : model_.siteDims) cols += d;
    const std::size_t cells = times.size() * cols;
    // Welford accumulation: stable at any trajectory count, and exactly zero spread when the
    // trajectories agree (zero jump rates), unlike Σx² − n x̄².
    std::vector<double> mean(cells, 0.0), m2(cells, 0.0), pops(cells, 0.0);
    const std::uint64_t N = std::max<std::uint64_t>(1, settings_.trajectories);
    for (std::uint64_t traj = 0; traj < N; ++traj) {
        core::Random sub = rng.stream(traj);
        QXL_TRY(oneTrajectory(ends, recordAt, sub, pops));
        for (std::size_t c = 0; c < cells; ++c) {
            const double delta = pops[c] - mean[c];
            mean[c] += delta / static_cast<double>(traj + 1);
            m2[c] += delta * (pops[c] - mean[c]);
        }
        ++ops_;
    }
    samples_.clear();
    const double n = static_cast<double>(N);
    for (std::size_t s = 0; s < times.size(); ++s) {
        AveragedSample as;
        as.timeS = times[s];
        for (std::size_t c = 0; c < cols; ++c) {
            // Unbiased sample variance; the reported error is σ/√N (spec 07 §5.1).
            const double var = N > 1 ? m2[s * cols + c] / (n - 1.0) : 0.0;
            as.populations.push_back(mean[s * cols + c]);
            as.stderrs.push_back(std::sqrt(var / n));
        }
        std::size_t col = 0;
        for (std::uint32_t site = 0; site < n_; ++site)
            for (std::uint32_t lv = 0; lv < model_.siteDims[site]; ++lv, ++col)
                if (lv >= 2) as.leakage += as.populations[col];
        samples_.push_back(std::move(as));
    }
    QXL_LOG_INFO(Sim, "Trajectories: {} runs over {:.3f} ns, {} samples", N, durationS * 1e9, samples_.size());
    return {};
}

double TrajectoriesBackend::population(std::uint32_t site, std::uint32_t level) const {
    if (samples_.empty() || site >= n_ || level >= model_.siteDims[site]) return 0.0;
    std::size_t col = level;
    for (std::uint32_t s = 0; s < site; ++s) col += model_.siteDims[s];
    return samples_.back().populations[col];
}

double TrajectoriesBackend::populationStdErr(std::uint32_t site, std::uint32_t level) const {
    if (samples_.empty() || site >= n_ || level >= model_.siteDims[site]) return 0.0;
    std::size_t col = level;
    for (std::uint32_t s = 0; s < site; ++s) col += model_.siteDims[s];
    return samples_.back().stderrs[col];
}

double TrajectoriesBackend::stateNorm() const { return std::sqrt(num::norm2Squared(psi_)); }

std::unique_ptr<IBackend> TrajectoriesBackend::clone() const { return std::make_unique<TrajectoriesBackend>(*this); }

} // namespace qlab::qsim
