// Spec 07 §5 — pulse-level Lindblad backend: model installation, the master-equation right-hand
// side (5.1), and populations. Time stepping lives in LindbladEvolve.cpp.
#include "QSim/Lindblad.hpp"
#include "Core/Log.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Matrix.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qsim {

Capabilities LindbladBackend::capabilities() const {
    return Capabilities{Kind::Lindblad,       kMaxSites,
                        /*exactNoise*/ true,  /*stochastic*/ false,
                        /*nonClifford*/ true, /*midCircuit*/ true,
                        /*multiLevel*/ true,
                        /*timeDomain*/ true,  /*fullReadback*/ true};
}

Status LindbladBackend::allocate(std::uint32_t nQubits, std::uint32_t levelsPerSite) {
    if (nQubits == 0 || nQubits > kMaxSites)
        return fail(
            err::TooLarge,
            std::format("Lindblad backend supports 1..{} sites, {} requested", kMaxSites, nQubits));
    if (levelsPerSite < 2)
        return fail(err::Unsupported, "site dimension must be at least 2");
    SystemModel m;
    m.siteDims.assign(nQubits, levelsPerSite);
    if (m.dimension() > kMaxDim) // checked before H0 is sized
        return fail(err::TooLarge, std::format("Lindblad dimension {} exceeds the cap {} (3^5)",
                                               m.dimension(), kMaxDim));
    m.h0 = Matrix(m.dimension(), m.dimension());
    m.frameFrequenciesHz.assign(nQubits, 0.0);
    return setModel(std::move(m));
}

Status LindbladBackend::setModel(SystemModel model) {
    if (model.siteDims.empty())
        return fail(err::BadTargets, "system model has no sites");
    if (model.siteDims.size() > kMaxSites)
        return fail(err::TooLarge,
                    std::format("Lindblad backend supports at most {} sites", kMaxSites));
    // The cost bound is the total dimension below, not the per-site one: an ion chain is qubit
    // sites of dimension 2 plus one motional mode whose dimension is the Fock cutoff (8 by
    // default, spec 09 §5.4), which a per-site cap of 5 would refuse outright.
    for (auto d : model.siteDims) {
        if (d < 2)
            return fail(err::Unsupported, "site dimension must be at least 2");
        if (d > kMaxDim)
            return fail(err::TooLarge,
                        std::format("site dimension {} exceeds the cap {}", d, kMaxDim));
    }
    const std::size_t D = model.dimension();
    if (D > kMaxDim)
        return fail(err::TooLarge,
                    std::format("Lindblad dimension {} exceeds the cap {} (3^5)", D, kMaxDim));
    auto square = [D](const Matrix& m) { return m.rows == D && m.cols == D; };
    if (!square(model.h0))
        return fail(err::BadTargets,
                    std::format("H0 is {}x{}, expected {}x{}", model.h0.rows, model.h0.cols, D, D));
    if (!num::isHermitian(model.h0, 1e-9))
        return fail(err::NotUnitary, "H0 is not Hermitian");
    for (auto& d : model.drives) {
        if (!square(d.inPhase) || !square(d.quadrature))
            return fail(err::BadTargets,
                        std::format("drive '{}' operators do not match the system dimension {}",
                                    d.channel, D));
        if (!num::isHermitian(d.inPhase, 1e-9) || !num::isHermitian(d.quadrature, 1e-9))
            return fail(
                err::NotUnitary,
                std::format("drive '{}' quadrature operators must be Hermitian", d.channel));
    }
    // Build the collapse caches aside: a rejected model must leave the installed one fully usable.
    std::vector<Matrix> adj, dagL;
    Matrix dagLSum(D, D);
    for (auto& c : model.collapse) {
        if (!square(c.op))
            return fail(err::BadTargets,
                        std::format("collapse operator '{}' does not match the system dimension {}",
                                    c.name, D));
        adj.push_back(num::adjoint(c.op));
        dagL.push_back(num::matmul(adj.back(), c.op));
        dagLSum += dagL.back();
    }
    lAdj_ = std::move(adj);
    lDagL_ = std::move(dagL);
    lDagLSum_ = std::move(dagLSum);
    model_ = std::move(model);
    n_ = static_cast<std::uint32_t>(model_.siteDims.size());
    levels_ = *std::max_element(model_.siteDims.begin(), model_.siteDims.end());
    D_ = D;
    rho_ = Matrix(D, D);
    rho_(0, 0) = 1.0;
    hCache_ = k1_ = k2_ = k3_ = k4_ = tmp_ = work1_ = work2_ = Matrix(D, D);
    samples_.clear();
    t_ = 0.0;
    allocated_ = true;
    QXL_LOG_INFO(Sim, "Lindblad allocated: {} sites, dim {}, {} drives, {} collapse ops, {} KiB",
                 n_, D, model_.drives.size(), model_.collapse.size(),
                 (D * D * sizeof(Complex)) >> 10);
    if (settings_.recordTrajectory)
        recordSample();
    return {};
}

void LindbladBackend::hamiltonianAt(double t, Matrix& h) const {
    std::copy(model_.h0.data.begin(), model_.h0.data.end(), h.data.begin());
    for (const auto& d : model_.drives) {
        if (!d.envelope)
            continue;
        Complex omega = d.envelope(t);
        const double re = omega.real(), im = omega.imag();
        if (re == 0.0 && im == 0.0)
            continue;
        for (std::size_t i = 0; i < h.data.size(); ++i)
            h.data[i] += re * d.inPhase.data[i] + im * d.quadrature.data[i];
    }
}

bool LindbladBackend::hasActiveDrives() const {
    return std::any_of(model_.drives.begin(), model_.drives.end(),
                       [](const DriveTerm& d) { return bool(d.envelope); });
}

void LindbladBackend::enterSegment(double a, double b) {
    // Envelopes are sampled strictly inside the grid segment [a, b]: an AWG envelope that steps at
    // a sample boundary is seen as its one-sided limit, so no stage reads the neighbouring sample
    // and the integrators keep their order (a 1e-9 fraction of the segment is far below any
    // envelope scale).
    const double eps = 1e-9 * (b - a);
    segLo_ = a + eps;
    segHi_ = b - eps;
}

void LindbladBackend::derivative(double t, const Matrix& rho, Matrix& out) {
    // (5.1) with ħ = 1 as dρ/dt = Gρ + (Gρ)† + Σ_k L_k ρ L_k†, G = −iH − ½ Σ_k L_k†L_k (ρ
    // Hermitian).
    hamiltonianAt(std::clamp(t, segLo_, segHi_), hCache_);
    const std::size_t n2 = D_ * D_;
    for (std::size_t i = 0; i < n2; ++i)
        hCache_.data[i] = Complex(0, -1) * hCache_.data[i] - 0.5 * lDagLSum_.data[i];
    num::matmulInto(hCache_, rho, work1_);
    const Complex* w = work1_.data.data();
    Complex* o = out.data.data();
    for (std::size_t i = 0; i < D_; ++i)
        for (std::size_t j = 0; j < D_; ++j)
            o[i * D_ + j] = w[i * D_ + j] + std::conj(w[j * D_ + i]);
    for (std::size_t k = 0; k < model_.collapse.size(); ++k) {
        num::matmulInto(model_.collapse[k].op, rho, work1_);
        num::matmulInto(work1_, lAdj_[k], work2_);
        const Complex* l = work2_.data.data();
        for (std::size_t i = 0; i < n2; ++i)
            o[i] += l[i];
    }
}

void LindbladBackend::vecDerivative(double t, std::span<const Complex> y, std::span<Complex> dy) {
    // Row-major vec(ρ) is ρ's own storage order.
    std::copy(y.begin(), y.end(), tmp_.data.begin());
    derivative(t, tmp_, k4_);
    std::copy(k4_.data.begin(), k4_.data.end(), dy.begin());
}

void LindbladBackend::recordSample() {
    TimeSample s;
    s.timeS = t_;
    s.trace = stateNorm();
    for (std::uint32_t site = 0; site < n_; ++site)
        for (std::uint32_t lv = 0; lv < model_.siteDims[site]; ++lv)
            s.populations.push_back(population(site, lv));
    s.leakage = leakage();
    samples_.push_back(std::move(s));
}

double LindbladBackend::stateNorm() const {
    Complex tr = 0;
    for (std::size_t i = 0; i < D_; ++i)
        tr += rho_(i, i);
    return tr.real();
}

double LindbladBackend::population(std::uint32_t site, std::uint32_t level) const {
    if (site >= n_ || level >= model_.siteDims[site])
        return 0.0;
    std::size_t stride = 1;
    for (std::uint32_t k = 0; k < site; ++k)
        stride *= model_.siteDims[k];
    const std::uint32_t d = model_.siteDims[site];
    double p = 0;
    for (std::size_t idx = 0; idx < D_; ++idx)
        if (static_cast<std::uint32_t>((idx / stride) % d) == level)
            p += rho_(idx, idx).real();
    return p;
}

double LindbladBackend::leakage() const {
    double inside = 0;
    for (std::size_t idx = 0; idx < D_; ++idx) {
        std::size_t rest = idx;
        bool computational = true;
        for (std::uint32_t s = 0; s < n_; ++s) {
            if (rest % model_.siteDims[s] > 1) {
                computational = false;
                break;
            }
            rest /= model_.siteDims[s];
        }
        if (computational)
            inside += rho_(idx, idx).real();
    }
    return std::max(0.0, 1.0 - inside);
}

} // namespace qlab::qsim
