// Spec 07 §5, spec 06 §6, T11 §5–§6 — time integration of the Lindblad equation: fixed-step RK4
// (default), Dormand–Prince 5(4), exponential midpoint (Magnus 2), and the exact propagator e^{L
// Δt} for stretches with a constant Hamiltonian.
#include "Core/Log.hpp"
#include "Numerics/Expm.hpp"
#include "Numerics/Integrators.hpp"
#include "QSim/Lindblad.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qsim {
namespace {
// Dense superoperators are formed only up to this system dimension (a 256 × 256 superoperator);
// spec 06 §6 forbids them for large d, where the selected integrator is kept.
constexpr std::size_t kDenseSuperMaxDim = 16;

// Row-major vectorisation vec(ρ)[i·D + j] = ρ_ij, for which vec(AρB) = (A ⊗ Bᵀ) vec(ρ) (T11 §5.1 in
// its transposed form). L = −i(H ⊗ I − I ⊗ Hᵀ) + Σ_k [L_k ⊗ conj(L_k) − ½ M_k ⊗ I − ½ I ⊗ M_kᵀ],
// M_k = L_k†L_k.
Matrix liouvillian(const Matrix& h, const std::vector<CollapseOp>& collapse,
                   const Matrix& lDagLSum) {
    const std::size_t D = h.rows;
    Matrix sup(D * D, D * D);
    const Complex mi(0, -1);
    for (std::size_t i = 0; i < D; ++i)
        for (std::size_t j = 0; j < D; ++j) {
            const std::size_t row = i * D + j;
            for (std::size_t k = 0; k < D; ++k) {
                sup(row, k * D + j) += mi * h(i, k) - 0.5 * lDagLSum(i, k); // −i(Hρ)_ij − ½(Mρ)_ij
                sup(row, i * D + k) -= mi * h(k, j) + 0.5 * lDagLSum(k, j); // +i(ρH)_ij − ½(ρM)_ij
            }
            for (const auto& c : collapse)
                for (std::size_t k = 0; k < D; ++k)
                    for (std::size_t l = 0; l < D; ++l)
                        sup(row, k * D + l) += c.op(i, k) * std::conj(c.op(j, l));
        }
    return sup;
}

// Segment k (1-based) of the grid tStart + k·grid, the last one ending exactly at tEnd.
double segmentEnd(double tStart, double grid, std::uint64_t k, double tEnd) {
    double t1 = std::min(tEnd, tStart + static_cast<double>(k) * grid);
    if (tEnd - t1 <= 1e-9 * grid)
        t1 = tEnd;
    return t1;
}
} // namespace

Status LindbladBackend::evolve(double durationS) {
    if (!allocated_)
        return fail(err::NotAllocated, "Lindblad backend has no model");
    if (!std::isfinite(durationS) || durationS < 0)
        return fail(ErrorCode::InvalidArgument,
                    "evolution duration must be finite and non-negative");
    if (durationS == 0)
        return {};
    if (!(settings_.stepS > 0) || !(settings_.sampleS > 0))
        return fail(ErrorCode::InvalidArgument,
                    "Lindblad step and sample spacing must be positive");
    const double tEnd = t_ + durationS;
    const bool constantH =
        !hasActiveDrives() && D_ <= kDenseSuperMaxDim &&
        durationS >= static_cast<double>(settings_.constantPropagatorSteps) * settings_.stepS;
    const std::uint64_t opsBefore = ops_;
    Status st = constantH                                            ? evolveExact(tEnd)
                : settings_.integrator == LindbladIntegrator::Dopri5 ? evolveAdaptive(tEnd)
                                                                     : evolveStepped(tEnd);
    if (!st)
        return st;
    // Re-Hermitize against accumulated round-off (the equation itself preserves the trace).
    for (std::size_t i = 0; i < D_; ++i)
        for (std::size_t j = i; j < D_; ++j) {
            Complex avg = 0.5 * (rho_(i, j) + std::conj(rho_(j, i)));
            rho_(i, j) = avg;
            rho_(j, i) = std::conj(avg);
        }
    QXL_LOG_DEBUG(Sim, "Lindblad evolved {:.3f} ns in {} steps ({}), trace {:.12f}",
                  durationS * 1e9, ops_ - opsBefore, constantH ? "exact propagator" : "integrator",
                  stateNorm());
    return {};
}

Status LindbladBackend::run() {
    return evolve(model_.durationS);
}

Status LindbladBackend::evolveExact(double tEnd) {
    const Matrix sup = liouvillian(model_.h0, model_.collapse, lDagLSum_);
    const double tStart = t_, span = tEnd - tStart;
    const double grid = settings_.recordTrajectory ? settings_.sampleS : span;
    // Whole grid segments share one propagator; only a final partial segment needs its own.
    auto whole = static_cast<std::uint64_t>(std::floor(span / grid + 1e-9));
    double rest = span - static_cast<double>(whole) * grid;
    if (rest <= 1e-9 * grid)
        rest = 0.0;
    num::Vector next(rho_.data.size());
    auto advance = [&](const Matrix& prop, double t1) {
        num::matvecInto(prop, rho_.data, next);
        std::swap(rho_.data, next);
        t_ = t1;
        ++ops_;
        if (settings_.recordTrajectory)
            recordSample();
    };
    if (whole > 0) {
        QXL_TRY_ASSIGN(const Matrix prop, num::expm(num::scale(sup, Complex(grid, 0.0))));
        for (std::uint64_t k = 1; k <= whole; ++k)
            advance(prop,
                    (k == whole && rest == 0.0) ? tEnd : tStart + static_cast<double>(k) * grid);
    }
    if (rest > 0.0) {
        QXL_TRY_ASSIGN(const Matrix prop, num::expm(num::scale(sup, Complex(rest, 0.0))));
        advance(prop, tEnd);
    }
    return {};
}

void LindbladBackend::rk4Step(double t, double h) {
    derivative(t, rho_, k1_);
    for (std::size_t i = 0; i < tmp_.data.size(); ++i)
        tmp_.data[i] = rho_.data[i] + 0.5 * h * k1_.data[i];
    derivative(t + 0.5 * h, tmp_, k2_);
    for (std::size_t i = 0; i < tmp_.data.size(); ++i)
        tmp_.data[i] = rho_.data[i] + 0.5 * h * k2_.data[i];
    derivative(t + 0.5 * h, tmp_, k3_);
    for (std::size_t i = 0; i < tmp_.data.size(); ++i)
        tmp_.data[i] = rho_.data[i] + h * k3_.data[i];
    derivative(t + h, tmp_, k4_);
    for (std::size_t i = 0; i < rho_.data.size(); ++i)
        rho_.data[i] +=
            (h / 6.0) * (k1_.data[i] + 2.0 * k2_.data[i] + 2.0 * k3_.data[i] + k4_.data[i]);
}

void LindbladBackend::magnus2Step(double t, double h) {
    // num::magnus2Step exponentiates with a 30-vector Krylov space and no error control, so the
    // step is split until ‖L(t_mid)‖·h ≤ 1, with ‖L‖₂ ≤ 2‖H‖₁ + 2Σ‖L_k†L_k‖₁ for Hermitian H and
    // L_k†L_k.
    hamiltonianAt(std::clamp(t + 0.5 * h, segLo_, segHi_), hCache_);
    double bound = 2.0 * num::norm1(hCache_);
    for (const auto& m : lDagL_)
        bound += 2.0 * num::norm1(m);
    const auto pieces =
        std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::ceil(bound * h)));
    const double hs = h / static_cast<double>(pieces);
    const auto applyL = [this](double tm, std::span<const Complex> x, std::span<Complex> out) {
        vecDerivative(tm, x, out);
    };
    for (std::uint64_t p = 0; p < pieces; ++p)
        num::magnus2Step(rho_.data.size(), applyL, rho_.data, t + static_cast<double>(p) * hs, hs);
}

Status LindbladBackend::evolveStepped(double tEnd) {
    const double tStart = t_;
    for (std::uint64_t k = 1; t_ < tEnd; ++k) {
        const double t1 = segmentEnd(tStart, settings_.sampleS, k, tEnd);
        const double a = t_, span = t1 - a;
        enterSegment(a, t1);
        // Equal steps no longer than stepS that land exactly on the segment end.
        const auto steps = std::max<std::uint64_t>(
            1, static_cast<std::uint64_t>(std::ceil(span / settings_.stepS - 1e-9)));
        const double h = span / static_cast<double>(steps);
        for (std::uint64_t s = 0; s < steps; ++s) {
            const double t = a + static_cast<double>(s) * h;
            if (settings_.integrator == LindbladIntegrator::Magnus2)
                magnus2Step(t, h);
            else
                rk4Step(t, h);
            ++ops_;
        }
        t_ = t1;
        if (settings_.recordTrajectory)
            recordSample();
    }
    return {};
}

Status LindbladBackend::evolveAdaptive(double tEnd) {
    const num::OdeRhs rhs = [this](double t, std::span<const Complex> y, std::span<Complex> dy) {
        vecDerivative(t, y, dy);
    };
    const double tStart = t_;
    double hInit = 0.0;
    for (std::uint64_t k = 1; t_ < tEnd; ++k) {
        const double t1 = segmentEnd(tStart, settings_.sampleS, k, tEnd);
        enterSegment(t_, t1);
        const auto res =
            num::dopri5(rhs, rho_.data, t_, t1, settings_.rtol, settings_.atol, hInit, t1 - t_);
        if (!res.ok)
            return fail(
                ErrorCode::Internal,
                std::format("Dormand–Prince step control failed at t = {:.6e} s (rtol {}, atol {})",
                            t_, settings_.rtol, settings_.atol));
        hInit = res.lastStep;
        ops_ += res.steps;
        t_ = t1;
        if (settings_.recordTrajectory)
            recordSample();
    }
    return {};
}

} // namespace qlab::qsim
