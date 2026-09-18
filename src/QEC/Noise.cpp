// Spec 16 §4, T09 §5.4 — noise sites of the three QEC noise settings, per-shot fault sampling and
// the enumeration of elementary faults used to build the decoding graph (spec 16 §5.1).
#include "QEC/Noise.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qec {
namespace {
constexpr char kLetters[4] = {'I', 'X', 'Y', 'Z'};

Status validate(const NoiseParams& n) {
    auto probability = [](double v) { return std::isfinite(v) && v >= 0.0 && v <= 1.0; };
    if (!probability(n.p)) return fail(err::BadOptions, std::format("physical error rate p = {} is not a probability", n.p));
    if (!probability(n.measurementFlip()))
        return fail(err::BadOptions, std::format("syndrome flip probability q = {} is not a probability", n.q));
    if (!probability(n.pIdle)) return fail(err::BadOptions, std::format("idle error rate {} is not a probability", n.pIdle));
    return {};
}

NoiseSite dataError(std::uint32_t afterOp, std::uint32_t qubit, const NoiseParams& n) {
    NoiseSite s{afterOp, SiteKind::Pauli1, qubit, 0};
    switch (n.dataError) {
    case DataErrorKind::Depolarizing: s.px = s.py = s.pz = n.p / 3.0; break;   // spec 16 §4
    case DataErrorKind::BitFlip: s.px = n.p; break;
    case DataErrorKind::PhaseFlip: s.pz = n.p; break;
    }
    return s;
}

NoiseSite depolarize1(std::uint32_t afterOp, std::uint32_t qubit, double p) {
    return {afterOp, SiteKind::Pauli1, qubit, 0, p / 3.0, p / 3.0, p / 3.0};
}

// T09 §5.4 circuit-level depolarizing model on a flat schedule.
void circuitSites(const Schedule& s, const NoiseParams& n, std::vector<NoiseSite>& sites) {
    std::vector<std::uint8_t> touched(s.qubits, 0);
    bool momentHasOps = false;
    auto closeMoment = [&](std::uint32_t lastOp) {
        if (n.pIdle > 0.0 && momentHasOps)
            for (std::uint32_t q = 0; q < s.qubits; ++q)
                if (!touched[q]) sites.push_back(depolarize1(lastOp, q, n.pIdle));
        std::fill(touched.begin(), touched.end(), std::uint8_t{0});
        momentHasOps = false;
    };
    for (std::uint32_t i = 0; i < s.ops.size(); ++i) {
        const Op& op = s.ops[i];
        if (op.marker()) {
            if (i > 0) closeMoment(i - 1);
            continue;
        }
        momentHasOps = true;
        if (op.a < s.qubits) touched[op.a] = 1;
        if (op.twoQubit() && op.b < s.qubits) touched[op.b] = 1;
        if (n.p <= 0.0) continue;
        if (op.twoQubit()) sites.push_back({i, SiteKind::Depolarize2, op.a, op.b, n.p});
        else if (op.kind == OpKind::Reset) sites.push_back({i, SiteKind::Pauli1, op.a, 0, n.p});   // prepared in |1⟩
        else if (op.kind == OpKind::Measure) {
            if (op.bit != kNoIndex) sites.push_back({i, SiteKind::RecordFlip, op.bit, 0, n.p});
        } else sites.push_back(depolarize1(i, op.a, n.p));
    }
    if (!s.ops.empty()) closeMoment(static_cast<std::uint32_t>(s.ops.size() - 1));
}

} // namespace

double krausDepolarizingParameter(double p, std::uint32_t qubits) {
    const double dim2 = std::pow(4.0, double(qubits));
    return p * dim2 / (dim2 - 1.0);
}

Result<NoisePlan> planCircuitNoise(const Schedule& schedule, const NoiseParams& params) {
    QXL_TRY(validate(params));
    NoisePlan plan{params, {}};
    plan.params.setting = NoiseSetting::CircuitLevel;
    circuitSites(schedule, params, plan.sites);
    return plan;
}

Result<NoisePlan> planNoise(const MemoryExperiment& ex, const NoiseParams& params) {
    if (params.setting == NoiseSetting::CircuitLevel) return planCircuitNoise(ex.schedule, params);
    QXL_TRY(validate(params));
    NoisePlan plan{params, {}};
    const bool everyRound = params.setting == NoiseSetting::Phenomenological;
    const auto& ops = ex.schedule.ops;
    for (std::uint32_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        if (op.kind == OpKind::RoundStart && (everyRound || op.a == 0) && params.p > 0.0)
            for (std::uint32_t q = 0; q < ex.nData; ++q) plan.sites.push_back(dataError(i, q, params));
        // Syndrome bits only: the transversal data readout stays perfect (spec 16 §4).
        if (everyRound && op.kind == OpKind::Measure && op.a >= ex.nData && op.bit != kNoIndex &&
            params.measurementFlip() > 0.0)
            plan.sites.push_back({i, SiteKind::RecordFlip, op.bit, 0, params.measurementFlip()});
    }
    return plan;
}

void drawFaults(const NoisePlan& plan, core::Random& rng, std::vector<FaultEvent>& out) {
    out.clear();
    for (std::uint32_t i = 0; i < plan.sites.size(); ++i) {
        const NoiseSite& s = plan.sites[i];
        const double u = rng.uniform();
        if (u >= s.total()) continue;
        FaultEvent f;
        f.afterOp = s.afterOp;
        f.site = i;
        switch (s.kind) {
        case SiteKind::Pauli1:
            f.qubitA = s.a;
            f.pauliA = u < s.px ? 'X' : (u < s.px + s.py ? 'Y' : 'Z');
            break;
        case SiteKind::Depolarize2: {
            // u/p is uniform on [0,1) given u < p: pick one of the 15 non-identity pairs.
            const auto index = std::min<std::uint32_t>(14u, static_cast<std::uint32_t>(u / s.px * 15.0)) + 1u;
            f.qubitA = s.a;
            f.qubitB = s.b;
            f.pauliA = kLetters[index & 3u];
            f.pauliB = kLetters[index >> 2];
            break;
        }
        case SiteKind::RecordFlip: f.flipBit = s.a; break;
        }
        out.push_back(f);
    }
}

std::vector<FaultEvent> enumerateFaults(const NoisePlan& plan) {
    std::vector<FaultEvent> faults;
    for (std::uint32_t i = 0; i < plan.sites.size(); ++i) {
        const NoiseSite& s = plan.sites[i];
        FaultEvent f;
        f.afterOp = s.afterOp;
        f.site = i;
        switch (s.kind) {
        case SiteKind::Pauli1: {
            const double probs[3] = {s.px, s.py, s.pz};
            for (int l = 0; l < 3; ++l) {
                if (probs[l] <= 0.0) continue;
                f.qubitA = s.a;
                f.pauliA = kLetters[l + 1];
                f.probability = probs[l];
                faults.push_back(f);
            }
            break;
        }
        case SiteKind::Depolarize2:
            for (std::uint32_t index = 1; index < 16 && s.px > 0.0; ++index) {
                f.qubitA = s.a;
                f.qubitB = s.b;
                f.pauliA = kLetters[index & 3u];
                f.pauliB = kLetters[index >> 2];
                f.probability = s.px / 15.0;
                faults.push_back(f);
            }
            break;
        case SiteKind::RecordFlip:
            if (s.px > 0.0) {
                f.flipBit = s.a;
                f.probability = s.px;
                faults.push_back(f);
            }
            break;
        }
    }
    return faults;
}

} // namespace qlab::qec
