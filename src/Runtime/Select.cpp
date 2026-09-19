// Spec 15 §2 — backend selection.
#include "Runtime/Select.hpp"
#include "Data/Fidelity.hpp"
#include "QSim/Lindblad.hpp"
#include <format>

namespace qlab::runtime {
namespace {

std::optional<qsim::Kind> pinnedKind(BackendChoice c) {
    switch (c) {
    case BackendChoice::StateVector:
        return qsim::Kind::StateVector;
    case BackendChoice::DensityMatrix:
        return qsim::Kind::DensityMatrix;
    case BackendChoice::Stabilizer:
        return qsim::Kind::Stabilizer;
    case BackendChoice::Lindblad:
        return qsim::Kind::Lindblad;
    case BackendChoice::Auto:
        break;
    }
    return std::nullopt;
}

// Largest number of sites the Lindblad backend holds at `levels` levels (spec 07 §5 cap 3^5).
std::uint32_t lindbladSites(std::uint32_t levels) {
    std::uint32_t n = 0;
    double dim = 1.0;
    while (n < qsim::LindbladBackend::kMaxSites &&
           dim * levels <= static_cast<double>(qsim::LindbladBackend::kMaxDim)) {
        dim *= levels;
        ++n;
    }
    return n;
}
} // namespace

Result<qsim::Selection> chooseBackend(const BackendRequest& req) {
    if (!req.plan)
        return fail(err::NoBackend, "backend selection needs a program plan");
    const std::uint32_t n = req.plan->nQubits();
    qsim::SelectionRequest sr;
    sr.nQubits = n;
    sr.hasNoise = req.hasNoise;
    sr.pauliNoiseOnly = req.pauliNoiseOnly;
    sr.pulseLevel = req.pulseLevel;
    sr.levels = req.levels;
    sr.pinned = pinnedKind(req.choice);
    // Row 2 of the §2 table: the stabilizer is preferred only above 28 qubits (or when nothing else
    // holds the state); below that an exact amplitude backend is both fast enough and more useful.
    sr.cliffordOnly =
        req.plan->cliffordOnly && (n > kStabilizerThreshold || n > qsim::maxQubitsFor(16, false));

    if (req.pulseLevel && n > lindbladSites(req.levels)) { // QL5010
        auto d = diagnostic("QL5010", SourceSpan{}, lindbladSites(req.levels), req.levels);
        return std::unexpected(error(d));
    }
    Result<qsim::Selection> selection = qsim::selectBackend(sr);
    if (!selection) {
        if (sr.pinned)
            return selection; // a pinned backend that cannot run: spec 15 §2, verbatim
        auto d = diagnostic("QL5011", SourceSpan{}, n,
                            qsim::maxQubitsFor(req.hasNoise ? 16 : 16, req.hasNoise));
        d.error.withNote(selection.error().message);
        return std::unexpected(error(d));
    }
    if (sr.pinned)
        return selection;
    // Rows 3–4: one density-matrix evolution is worth it from 256 shots and up to 12 qubits;
    // otherwise trajectories on the state vector, one per shot (spec 15 §2).
    if (selection->kind == qsim::Kind::DensityMatrix && req.levels == 2 &&
        (req.shots < kDensityMatrixShots || n > kDensityMatrixQubits) &&
        n <= qsim::maxQubitsFor(16, false)) {
        qsim::SelectionRequest sv = sr;
        sv.pinned = qsim::Kind::StateVector;
        if (auto alt = qsim::selectBackend(sv)) {
            alt->reason = req.shots < kDensityMatrixShots
                              ? std::format("noise model with {} shots (< {}): per-shot "
                                            "trajectories are cheaper than "
                                            "one density-matrix evolution",
                                            req.shots, kDensityMatrixShots)
                              : std::format("noise model above {} qubits: Monte-Carlo noise "
                                            "trajectories, one per shot",
                                            kDensityMatrixQubits);
            alt->cls = data::FidelityClass::Statistical;
            alt->stochasticUnravelling = true;
            return alt;
        }
    }
    return selection;
}

} // namespace qlab::runtime
