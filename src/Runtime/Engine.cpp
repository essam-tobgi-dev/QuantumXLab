// Spec 15 §3 — the gate-level engine: backend allocation under the memory budget, channel binding
// to simulator indices, readout models, snapshots and terminal Born sampling.
#include "Runtime/Engine.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::runtime::detail {
namespace {
const RunOptions& optionsOf(const ExecutionInput& in) {
    static const RunOptions kDefault;
    return in.options ? *in.options : kDefault;
}
} // namespace

Engine::Engine(const ExecutionInput& in, const ProgramPlan& plan)
    : in_(in), plan_(plan), circuit_(in.program->circuit), options_(optionsOf(in)) {
    idle_ = idleGaps(circuit_, in.program->timing);
    const auto order = circuit_.topologicalOrder();
    layerOf_.assign(order.size(), 0);
    std::map<std::uint32_t, std::uint32_t> layerByNode;
    const auto layers = circuit_.layers();
    for (std::uint32_t l = 0; l < layers.size(); ++l)
        for (ir::NodeId id : layers[l]) layerByNode[id.get()] = l;
    for (std::size_t i = 0; i < order.size(); ++i)
        if (auto it = layerByNode.find(order[i].get()); it != layerByNode.end()) layerOf_[i] = it->second;

    cbitOfMeasured_ = detail::classicalBitOfMeasured(plan_);
    cadence_ = resolveCadence(options_.cadence, plan_.nQubits());
    perShotDrift_ = in_.backend == qsim::Kind::StateVector || in_.backend == qsim::Kind::Stabilizer;
}

std::size_t Engine::stateBytes() const {
    const double n = plan_.nQubits();
    const double levels = options_.levels;
    const double dim = std::pow(levels, n);
    switch (in_.backend) {
    case qsim::Kind::DensityMatrix:
    case qsim::Kind::Lindblad: return static_cast<std::size_t>(std::min(1e18, 16.0 * dim * dim));
    case qsim::Kind::Stabilizer: return static_cast<std::size_t>(std::max(64.0, n * n / 4.0));
    default: return static_cast<std::size_t>(std::min(1e18, 16.0 * dim));
    }
}

Status Engine::allocate() {
    backend_ = qsim::makeBackend(in_.backend);
    if (!backend_) return fail(err::NoBackend, "no such backend");
    const std::size_t bytes = stateBytes();
    const std::size_t budget = qsim::availableMemoryBytes();
    if (bytes > budget) { // QL5012: refuse rather than swap to disk (spec 15 §3.3)
        auto d = diagnostic("QL5012", SourceSpan{},
                            std::format("{} qubits need {:.1f} GiB on the {} backend, {:.1f} GiB available",
                                        plan_.nQubits(), bytes / 1073741824.0, qsim::kindName(in_.backend),
                                        budget / 1073741824.0));
        return std::unexpected(error(d));
    }
    QXL_TRY(backend_->allocate(plan_.nQubits(), options_.levels));
    apply_.twirlNonPauli = options_.twirlNonPauli;
    return {};
}

std::vector<noise::AttachedChannel> Engine::remap(std::vector<noise::AttachedChannel> v) const {
    for (auto& ac : v)
        for (auto& q : ac.qubits) {
            const std::int32_t s = q.get() < plan_.toSim.size() ? plan_.toSim[q.get()] : -1;
            q = QubitIndex{static_cast<std::uint32_t>(std::max<std::int32_t>(s, 0))};
        }
    return v;
}

Status Engine::applyChannels(std::span<const noise::AttachedChannel> channels, ShotContext& ctx) {
    if (channels.empty()) return {};
    apply_.shotDetuningHz = perShotDrift_ ? std::span<const double>(detuning_) : std::span<const double>{};
    return noise::applyChannels(*backend_, channels, *ctx.rng, apply_, report_);
}

Status Engine::prepare(ShotContext& ctx) {
    std::vector<QubitIndex> all;
    for (std::uint32_t q = 0; q < plan_.nQubits(); ++q) all.push_back(QubitIndex{q});
    QXL_TRY(backend_->reset(all, *ctx.rng));
    ctx.measured.assign(plan_.nQubits(), -1);
    ctx.bits.assign(plan_.layout.bits, 0);
    idleCursor_ = 0;
    lastLayer_ = 0xFFFFFFFFu;
    gateIndex_ = 0;
    if (!in_.noise) return {};
    // Per-shot quasi-static detuning (spec 08 §2.6), in simulator index space.
    if (perShotDrift_) {
        const std::vector<double> perDevice = in_.noise->drawShotDetunings(*ctx.rng);
        detuning_.assign(plan_.nQubits(), 0.0);
        for (std::size_t s = 0; s < plan_.qubits.size(); ++s)
            if (plan_.qubits[s] < perDevice.size()) detuning_[s] = perDevice[plan_.qubits[s]];
    }
    std::vector<QubitIndex> phys;
    for (std::uint32_t q : plan_.qubits) phys.push_back(QubitIndex{q});
    return applyChannels(remap(in_.noise->preparationChannels(phys)), ctx);
}

Result<const noise::ReadoutModel*> Engine::readoutFor(std::uint32_t simQubit) {
    if (auto it = readout1q_.find(simQubit); it != readout1q_.end()) return &it->second;
    const std::vector<QubitIndex> sim{QubitIndex{simQubit}};
    const std::vector<QubitIndex> phys{QubitIndex{plan_.qubits[simQubit]}};
    noise::ReadoutModel m = noise::ReadoutModel::ideal(sim);
    if (in_.noise) {
        QXL_TRY_ASSIGN(noise::ReadoutModel device, in_.noise->readout(phys));
        std::vector<noise::ReadoutFactor> factors(device.factors().begin(), device.factors().end());
        QXL_TRY_ASSIGN(m, noise::ReadoutModel::make(sim, std::move(factors)));
    }
    return &readout1q_.emplace(simQubit, std::move(m)).first->second;
}

std::vector<std::int64_t> classicalBitOfMeasured(const ProgramPlan& plan) {
    // The LAST measurement of a qubit decides which classical bit carries its outcome.
    std::vector<std::int64_t> out(plan.measuredQubits.size(), -1);
    for (const MeasuredBit& m : plan.measurements) {
        auto it = std::find(plan.measuredQubits.begin(), plan.measuredQubits.end(), m.qubit);
        if (it == plan.measuredQubits.end()) continue;
        out[static_cast<std::size_t>(it - plan.measuredQubits.begin())] =
            m.bit == ir::kNoBit ? -1 : static_cast<std::int64_t>(m.bit.index);
    }
    return out;
}

Result<noise::ReadoutModel> terminalReadoutFor(const ExecutionInput& in, const ProgramPlan& plan) {
    std::vector<QubitIndex> sim, phys;
    for (std::uint32_t q : plan.measuredQubits) {
        sim.push_back(QubitIndex{q});
        phys.push_back(QubitIndex{plan.qubits[q]});
    }
    if (!in.noise) return noise::ReadoutModel::ideal(sim);
    // The factors index positions inside the model, so they survive the relabelling.
    QXL_TRY_ASSIGN(noise::ReadoutModel device, in.noise->readout(phys));
    std::vector<noise::ReadoutFactor> factors(device.factors().begin(), device.factors().end());
    return noise::ReadoutModel::make(std::move(sim), std::move(factors));
}

Result<noise::ReadoutModel> Engine::terminalReadout() const { return terminalReadoutFor(in_, plan_); }

std::shared_ptr<const qsim::Snapshot> Engine::capture(double timeS, std::uint64_t gateIndex) {
    qsim::SnapshotRequest req;
    req.probabilities = true;
    // `amplitudes` asks an amplitude backend for |ψ⟩ and a density-matrix one for ρ; the views and
    // the §7 (ii) state fidelity need whichever the backend holds (spec 15 §4, T10 §1).
    switch (backend_->kind()) {
    case qsim::Kind::StateVector:
    case qsim::Kind::DensityMatrix:
    case qsim::Kind::Lindblad: req.amplitudes = true; break;
    case qsim::Kind::Stabilizer: req.tableau = true; break;
    default: break;
    }
    auto s = backend_->snapshot(req);
    if (!s) return nullptr;
    s->simTimePs = timeS * 1e12;
    s->gateIndex = gateIndex;
    return std::make_shared<const qsim::Snapshot>(std::move(*s));
}

void writeMeasuredBits(const std::string& key, std::span<const std::int64_t> cbits, std::span<std::uint8_t> bits) {
    // `key` is MSB-first over the measured qubits: key[0] belongs to the LAST qubit of the list.
    const std::size_t n = cbits.size();
    for (std::size_t k = 0; k < n && k < key.size(); ++k) {
        const std::int64_t bit = cbits[n - 1 - k];
        if (bit < 0 || static_cast<std::size_t>(bit) >= bits.size()) continue;
        bits[static_cast<std::size_t>(bit)] = key[k] == '1' ? 1 : 0;
    }
}

Status Engine::sampleTerminal(std::uint32_t shots, core::Random& rng, std::vector<ShotRecord>& memory,
                              std::optional<std::vector<double>>& exact) {
    if (plan_.measuredQubits.empty()) {
        memory.assign(shots, ShotRecord{std::vector<std::uint8_t>(plan_.layout.bits, 0), {}, false});
        return {};
    }
    QXL_TRY_ASSIGN(noise::ReadoutModel readout, terminalReadout());
    std::vector<QubitIndex> sim(readout.qubits().begin(), readout.qubits().end());
    // Exact Born distribution over the classical bits (Simulator-only, spec 15 §4). It is the
    // OBSERVED distribution: the assignment map of the readout acts on it exactly as it acts on the
    // sampled shots below (spec 08 §7.1), so counts and theory overlay the same quantity.
    if (plan_.layout.bits > 0 && plan_.layout.bits <= 20) {
        QXL_TRY_ASSIGN(const qsim::Probabilities born, backend_->probabilities(sim));
        QXL_TRY_ASSIGN(const std::vector<double> p, readout.applyToProbabilities(born));
        std::vector<double> dist(std::size_t{1} << plan_.layout.bits, 0.0);
        for (std::size_t i = 0; i < p.size(); ++i) {
            std::size_t index = 0;
            for (std::size_t k = 0; k < cbitOfMeasured_.size(); ++k)
                if (cbitOfMeasured_[k] >= 0 && ((i >> k) & 1))
                    index |= std::size_t{1} << static_cast<std::size_t>(cbitOfMeasured_[k]);
            dist[index] += p[i];
        }
        exact = std::move(dist);
    }
    QXL_TRY_ASSIGN(const qsim::Counts counts, noise::sampleWithReadout(*backend_, readout, shots, rng));
    memory.reserve(shots);
    // The shots of model (a) are i.i.d., so their order carries no information; materialising them
    // in key order makes the memory bit-identical for a given seed (spec 15 §4, 04 §6).
    const std::map<std::string, std::uint64_t> ordered(counts.begin(), counts.end());
    for (const auto& [key, n] : ordered) {
        ShotRecord r;
        r.bits.assign(plan_.layout.bits, 0);
        writeMeasuredBits(key, cbitOfMeasured_, r.bits);
        for (std::uint64_t i = 0; i < n; ++i) memory.push_back(r);
    }
    return {};
}

} // namespace qlab::runtime::detail
