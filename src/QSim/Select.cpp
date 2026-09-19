// Spec 07 §7 — backend factory and the selection table. A pinned backend that cannot run the
// program fails with the same diagnostic instead of silently switching.
#include "QSim/DensityMatrix.hpp"
#include "QSim/Lindblad.hpp"
#include "QSim/Stabilizer.hpp"
#include "QSim/StateVector.hpp"
#include "QSim/Trajectories.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qsim {

std::unique_ptr<IBackend> makeBackend(Kind kind) {
    switch (kind) {
    case Kind::StateVector:
        return std::make_unique<StateVectorBackend>();
    case Kind::DensityMatrix:
        return std::make_unique<DensityMatrixBackend>();
    case Kind::Stabilizer:
        return std::make_unique<StabilizerBackend>();
    case Kind::Lindblad:
        return std::make_unique<LindbladBackend>();
    case Kind::Trajectories:
        return std::make_unique<TrajectoriesBackend>();
    }
    return nullptr;
}

namespace {
// Largest site count a dimension-capped backend admits at `levels` levels per site.
std::uint32_t sitesWithin(std::uint32_t levels, double dimensionCap, std::uint32_t siteCap) {
    std::uint32_t n = 0;
    double dim = 1.0;
    while (n < siteCap && dim * levels <= dimensionCap) {
        dim *= levels;
        ++n;
    }
    return n;
}
// Spec 07 §3.1: dimension ≤ 8192 within the 80 % memory rule, which the d = 2 cap already reflects.
std::uint32_t densityMatrixCap(std::uint32_t levels) {
    const std::uint32_t two = DensityMatrixBackend::maxQubits();
    if (levels <= 2)
        return two;
    return sitesWithin(levels, std::ldexp(1.0, static_cast<int>(two)), 13);
}
std::uint32_t lindbladCap(std::uint32_t levels) {
    return sitesWithin(levels, static_cast<double>(LindbladBackend::kMaxDim),
                       LindbladBackend::kMaxSites);
}
std::uint32_t trajectoriesCap(std::uint32_t levels) {
    return sitesWithin(levels, static_cast<double>(TrajectoriesBackend::kMaxDim),
                       TrajectoriesBackend::kMaxSites);
}

struct Rejection {
    ErrorCode code = ErrorCode::Ok;
    std::string why; // empty: this backend can run the program
    bool ok() const { return why.empty(); }
};

// Why `k` cannot run this request at all (capabilities and caps, spec 07 §1–§5).
Rejection rejection(Kind k, const SelectionRequest& r) {
    auto tooLarge = [](std::string msg) { return Rejection{err::TooLarge, std::move(msg)}; };
    auto unsupported = [](std::string msg) { return Rejection{err::Unsupported, std::move(msg)}; };
    switch (k) {
    case Kind::Lindblad: {
        if (!r.pulseLevel)
            return unsupported(
                "the Lindblad backend integrates a pulse schedule; this run is gate-level");
        const std::uint32_t cap = lindbladCap(r.levels);
        if (r.nQubits > cap)
            return tooLarge(std::format("the Lindblad backend fits {} site(s) of {} levels "
                                        "(dimension cap {}), {} requested",
                                        cap, r.levels, LindbladBackend::kMaxDim, r.nQubits));
        return {};
    }
    case Kind::Trajectories: {
        if (!r.pulseLevel)
            return unsupported(
                "the trajectories backend integrates a pulse schedule; this run is gate-level");
        const std::uint32_t cap = trajectoriesCap(r.levels);
        if (r.nQubits > cap)
            return tooLarge(std::format("the trajectories backend fits {} site(s) of {} levels "
                                        "(dimension cap {}), {} requested",
                                        cap, r.levels, TrajectoriesBackend::kMaxDim, r.nQubits));
        return {};
    }
    case Kind::Stabilizer:
        if (r.pulseLevel)
            return unsupported(
                "the stabilizer backend runs Clifford circuits, not pulse schedules");
        if (!r.cliffordOnly)
            return unsupported("the stabilizer backend accepts Clifford gates only");
        if (r.hasNoise && !r.pauliNoiseOnly)
            return unsupported("the stabilizer backend accepts Pauli-twirled noise only");
        if (r.levels != 2)
            return unsupported("the stabilizer backend is two-level only");
        if (r.nQubits > 10000)
            return tooLarge(std::format(
                "the stabilizer backend supports at most 10000 qubits, {} requested", r.nQubits));
        return {};
    case Kind::DensityMatrix: {
        if (r.pulseLevel)
            return unsupported(
                "the density-matrix backend applies gates and channels, not pulse schedules");
        const std::uint32_t cap = densityMatrixCap(r.levels);
        if (r.nQubits > cap)
            return tooLarge(std::format(
                "the density-matrix backend fits {} site(s) of {} levels in memory, {} requested",
                cap, r.levels, r.nQubits));
        return {};
    }
    case Kind::StateVector: {
        if (r.pulseLevel)
            return unsupported("the state-vector backend applies gates, not pulse schedules");
        if (r.levels != 2)
            return unsupported("the state-vector backend is two-level only");
        const std::uint32_t cap = StateVectorBackend::maxQubits();
        if (r.nQubits > cap)
            return tooLarge(std::format(
                "the state-vector backend fits {} qubits in memory, {} requested", cap, r.nQubits));
        return {};
    }
    }
    return unsupported("unknown backend");
}

FidelityClass classOf(Kind k, const SelectionRequest& r) {
    switch (k) {
    case Kind::Lindblad:
        return FidelityClass::Numerical;
    case Kind::Trajectories:
        return FidelityClass::Statistical;
    case Kind::Stabilizer:
    case Kind::StateVector:
        return r.hasNoise ? FidelityClass::Statistical : FidelityClass::Exact;
    case Kind::DensityMatrix:
        return FidelityClass::Exact;
    }
    return FidelityClass::Model;
}
} // namespace

Result<Selection> selectBackend(const SelectionRequest& req) {
    if (req.pinned) {
        const Rejection why = rejection(*req.pinned, req);
        if (!why.ok())
            return fail(why.code, std::format("pinned backend '{}' cannot run this program: {}",
                                              kindName(*req.pinned), why.why));
        Selection s;
        s.kind = *req.pinned;
        s.cls = classOf(s.kind, req);
        s.stochasticUnravelling = s.kind == Kind::StateVector && req.hasNoise;
        s.reason = "pinned by the run settings";
        return s;
    }
    // Rules in the order of spec 07 §7.
    if (req.pulseLevel) {
        if (req.nQubits <= lindbladCap(req.levels))
            return Selection{Kind::Lindblad, FidelityClass::Numerical, false,
                             "pulse-level run within the Lindblad dimension cap"};
        if (req.levels == 2 && req.nQubits <= trajectoriesCap(req.levels))
            return Selection{Kind::Trajectories, FidelityClass::Statistical, false,
                             "pulse-level run above the Lindblad cap, two-level sites"};
        return fail(
            err::TooLarge,
            std::format(
                "no backend can run a pulse-level program on {} site(s) of {} levels: {}; {}",
                req.nQubits, req.levels, rejection(Kind::Lindblad, req).why,
                rejection(Kind::Trajectories, req).why));
    }
    if (req.cliffordOnly && (!req.hasNoise || req.pauliNoiseOnly) && req.levels == 2 &&
        req.nQubits <= 10000)
        return Selection{Kind::Stabilizer,
                         req.hasNoise ? FidelityClass::Statistical : FidelityClass::Exact, false,
                         "all gates Clifford with no noise or Pauli-only noise"};
    const std::uint32_t svCap = StateVectorBackend::maxQubits();
    const std::uint32_t dmCap = densityMatrixCap(req.levels);
    if (!req.hasNoise && req.levels == 2 && req.nQubits <= svCap)
        return Selection{Kind::StateVector, FidelityClass::Exact, false,
                         "no noise model, fits the state-vector cap"};
    if (req.hasNoise && req.nQubits <= dmCap)
        return Selection{Kind::DensityMatrix, FidelityClass::Exact, false,
                         "noise model within the density-matrix cap"};
    if (req.hasNoise && req.levels == 2 && req.nQubits <= svCap)
        return Selection{
            Kind::StateVector, FidelityClass::Statistical, true,
            "noise model above the density-matrix cap: per-shot stochastic unravelling"};
    if (!req.hasNoise && req.levels != 2) // no rule covers multi-level gate-level runs (spec 07 §7)
        return fail(err::Unsupported,
                    std::format("no backend is selected for {} gate-level site(s) of {} levels "
                                "without a noise model: {}; "
                                "the density-matrix backend is selected only with a noise model — "
                                "pin it to run this program",
                                req.nQubits, req.levels, rejection(Kind::StateVector, req).why));
    // Smallest violated cap first, then the largest one the program would have to fit (spec 07 §7).
    std::vector<std::pair<std::uint32_t, std::string>> caps;
    if (req.hasNoise)
        caps.emplace_back(dmCap, rejection(Kind::DensityMatrix, req).why);
    if (req.levels == 2)
        caps.emplace_back(svCap, rejection(Kind::StateVector, req).why);
    std::sort(caps.begin(), caps.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string detail;
    for (const auto& [cap, why] : caps) {
        if (!detail.empty())
            detail += "; ";
        detail += why;
    }
    return fail(err::TooLarge, std::format("no backend can run {} qubits ({}): {}", req.nQubits,
                                           req.hasNoise ? "with noise" : "noise-free", detail));
}

} // namespace qlab::qsim
