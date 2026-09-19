// State sources shared by the amplitude and density-matrix views (see StateSource.hpp).
#include "Viz/Views/StateSource.hpp"
#include "Numerics/Matrix.hpp"
#include "Viz/Math/DensityPlot.hpp"
#include "Viz/Math/Reduced.hpp"
#include <algorithm>

namespace qlab::viz {

DensitySource densityFor(const ViewInput& in, std::span<const QubitIndex> subset) {
    DensitySource out;
    if (!in.snapshot) {
        out.note = "Run a program to see the state";
        return out;
    }
    const qsim::Snapshot& snap = *in.snapshot;
    const std::uint32_t n = snap.nQubits;
    const bool whole = subset.empty() || subset.size() == n;
    if (whole) {
        for (std::uint32_t q = 0; q < n; ++q)
            out.qubits.emplace_back(q);
    } else {
        out.qubits.assign(subset.begin(), subset.end());
        out.reduced = true;
    }
    if (out.qubits.size() > math::kDensityPlotMaxQubits) {
        out.qubits.clear();
        out.note = "More than 8 qubits: pick a qubit subset (spec 21 §3.5)";
        return out;
    }
    if (whole) {
        // Up to 8 qubits the full matrix is at most 256 × 256: an outer product on the UI thread.
        if (snap.densityMatrix && !snap.densityMatrix->empty()) {
            if (auto block = math::computationalBlock(*snap.densityMatrix, n,
                                                      std::max<std::uint32_t>(2, snap.levels)))
                out.rho = std::move(*block);
        } else if (snap.amplitudes && snap.amplitudes->size() == (std::size_t{1} << n)) {
            out.rho = num::projector(*snap.amplitudes);
        }
        if (!out.rho)
            out.note = "The snapshot carries no amplitudes or density matrix";
        return out;
    }
    // A subset is a partial trace: the run's job (spec 21 §2.3).
    if (in.reductions)
        if (const qsim::ReducedState* r = in.reductions->subset(out.qubits)) {
            out.rho = r->rho;
            return out;
        }
    for (const auto& r : snap.reduced)
        if (r.qubits == out.qubits && r.rho.rows == (std::size_t{1} << out.qubits.size())) {
            out.rho = r.rho;
            return out;
        }
    out.needsReduction = true;
    out.note = "Waiting for the reduced state from the run";
    return out;
}

AmplitudeSource amplitudesFor(const ViewInput& in, const math::AmplitudeFilter& filter) {
    AmplitudeSource out;
    if (!in.snapshot) {
        out.note = "Run a program to see the amplitudes";
        return out;
    }
    const qsim::Snapshot& snap = *in.snapshot;
    if (snap.amplitudes && !snap.amplitudes->empty() &&
        snap.nQubits <= math::kFullAmplitudeQubits) {
        out.selection = math::selectAmplitudes(*snap.amplitudes, filter);
        return out;
    }
    if (in.reductions &&
        in.reductions->topAmplitudes) { // above 20 qubits: the run's top-k with its mass
        math::AmplitudeSelection sel = *in.reductions->topAmplitudes;
        std::erase_if(sel.entries, [&](const math::BasisEntry& e) {
            return filter.threshold > 0.0 && e.probability <= filter.threshold;
        });
        if (filter.maxEntries > 0 && sel.entries.size() > filter.maxEntries)
            sel.entries.resize(filter.maxEntries);
        if (filter.order == math::AmplitudeOrder::ByIndex)
            std::sort(sel.entries.begin(), sel.entries.end(),
                      [](const auto& a, const auto& b) { return a.index < b.index; });
        sel.shownProbability = 0.0;
        for (const auto& e : sel.entries)
            sel.shownProbability += e.probability;
        out.selection = std::move(sel);
        out.fromRun = true;
        return out;
    }
    out.note = snap.amplitudes ? "More than 20 qubits: waiting for the top amplitudes from the run"
                               : "Amplitudes need a state-vector snapshot";
    return out;
}

std::optional<SingleReduction> singleReductionFor(const ViewInput& in, QubitIndex q,
                                                  std::uint32_t inlineLimit) {
    if (in.reductions)
        if (const SingleReduction* s = in.reductions->single(q))
            return *s;
    if (!in.snapshot)
        return std::nullopt;
    const qsim::Snapshot& snap = *in.snapshot;
    std::optional<num::Matrix> rho;
    for (const auto& r : snap.reduced)
        if (r.qubits.size() == 1 && r.qubits.front() == q)
            rho = r.rho;
    if (!rho && snap.nQubits <= inlineLimit) {
        const QubitIndex keep[1] = {q};
        if (snap.amplitudes) {
            if (auto r = math::reducedSingle(*snap.amplitudes, snap.nQubits, q.get()))
                rho = std::move(*r);
        } else if (snap.densityMatrix && snap.nQubits <= 6) {
            // A dense partial trace is 4^n work: only the smallest registers on the UI thread.
            if (auto r = math::reducedFromDensity(*snap.densityMatrix, snap.nQubits,
                                                  std::max<std::uint32_t>(2, snap.levels), keep))
                rho = std::move(*r);
        }
    }
    if (!rho)
        return std::nullopt;
    auto block = math::computationalBlock(*rho, 1, static_cast<std::uint32_t>(rho->rows));
    if (!block)
        return std::nullopt;
    auto bloch = math::blochVector(*block);
    if (!bloch)
        return std::nullopt;
    SingleReduction s;
    s.qubit = q;
    s.bloch = *bloch;
    s.purity = math::purity(*rho);
    s.entropyBits = math::entropyBits(*rho);
    s.leakage = std::max(0.0, 1.0 - (*block)(0, 0).real() - (*block)(1, 1).real());
    s.rho = std::move(*rho);
    return s;
}

std::string subsetCaption(std::span<const QubitIndex> qubits) {
    std::string s;
    for (std::size_t k = qubits.size();
         k-- > 0;) { // most significant first, as the kets are written
        s += "q" + std::to_string(qubits[k].get());
        if (k)
            s += " ";
    }
    return s;
}

} // namespace qlab::viz
