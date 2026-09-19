// Spec 15 §7 (ii) / T12 (4.4) — the accurate fidelity: the same circuit without noise, compared
// with the noisy run through the classical (Hellinger) fidelity of the outcome distributions and,
// when both states are available, the state fidelity ⟨ψ_ideal|ρ_noisy|ψ_ideal⟩ before measurement.
#include "Data/Fidelity.hpp"
#include "Runtime/Perform.hpp"
#include "Runtime/Select.hpp"
#include <cmath>

namespace qlab::runtime::detail {
namespace {

// Distribution over the classical bits: the exact one when the backend holds it, else the counts.
std::vector<double> distribution(const RunResult& r) {
    if (r.exact)
        return *r.exact;
    std::vector<double> p(std::size_t{1} << r.layout.bits, 0.0);
    const double total = static_cast<double>(r.counts.total());
    if (total <= 0.0)
        return p;
    for (const auto& [label, count] : r.counts.all()) {
        const std::uint64_t index = data::Histogram::indexFromLabel(label);
        if (index < p.size())
            p[index] = static_cast<double>(count) / total;
    }
    return p;
}

// ⟨ψ|ρ|ψ⟩ with |ψ⟩ the ideal amplitudes and ρ the noisy density matrix (T10 §1, squared
// convention).
std::optional<double> stateFidelity(const RunResult& ideal, const RunResult& noisy) {
    if (!ideal.finalState || !noisy.finalState)
        return std::nullopt;
    const auto& psi = ideal.finalState->amplitudes;
    const auto& rho = noisy.finalState->densityMatrix;
    if (!psi || !rho || rho->rows != psi->size() || rho->cols != psi->size())
        return std::nullopt;
    num::Complex f = 0.0;
    for (std::size_t i = 0; i < psi->size(); ++i)
        for (std::size_t j = 0; j < psi->size(); ++j)
            f += std::conj((*psi)[i]) * (*rho)(i, j) * (*psi)[j];
    return std::clamp(f.real(), 0.0, 1.0);
}
} // namespace

Result<SimulatedFidelity> simulatedFidelity(const RunContext& ctx, const RunResult& noisy) {
    if (!ctx.noise)
        return fail(err::Unsupported, "an ideal run has nothing to compare against");
    RunContext ideal = ctx;
    ideal.noise = nullptr;
    ideal.options.noise = NoiseSource::Ideal;
    ideal.options.computeEstimate = false;
    ideal.options.accurateFidelity = false;
    ideal.options.cadence = SnapshotCadence::End;
    ideal.options.sweep.reset();
    ideal.bus = nullptr;
    // The ideal reference is exact wherever the state fits; the noisy run chose its own backend.
    if (ctx.choice == BackendChoice::DensityMatrix)
        ideal.choice = BackendChoice::Auto;
    QXL_TRY_ASSIGN(const RunResult reference, performRun(ideal));

    const std::vector<double> p = distribution(reference), q = distribution(noisy);
    SimulatedFidelity out;
    double overlap = 0.0;
    for (std::size_t i = 0; i < p.size() && i < q.size(); ++i)
        overlap += std::sqrt(std::max(0.0, p[i] * q[i]));
    out.classical = std::clamp(overlap * overlap, 0.0, 1.0); // F_c = (Σ √(p q))²   T12 (4.4)
    out.hellinger = std::sqrt(std::max(0.0, 1.0 - std::sqrt(out.classical)));
    out.state = stateFidelity(reference, noisy);
    // Class: exact distributions make it Numerical, sampled ones Statistical (spec 15 §7).
    out.cls = (reference.exact && noisy.exact) ? data::FidelityClass::Numerical
                                               : data::FidelityClass::Statistical;
    return out;
}

} // namespace qlab::runtime::detail
