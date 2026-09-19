// Spec 08 §7 — per-backend application of attached channels and of the readout map.
#include "Noise/Apply.hpp"
#include "Numerics/Sampling.hpp"
#include "QSim/DensityMatrix.hpp"
#include "QSim/Stabilizer.hpp"
#include "QSim/StateVector.hpp"
#include <algorithm>
#include <format>

namespace qlab::noise {
namespace {
using qlab::FidelityClass;

void noteClass(ApplyReport& r, FidelityClass c) {
    r.cls = qlab::weakest(r.cls, c);
}

// Index drawn from a probability vector; round-off slack falls into the last positive entry.
std::size_t draw(std::span<const double> p, core::Random& rng) {
    const double u = rng.uniform();
    double acc = 0.0;
    std::size_t last = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (!(p[i] > 0.0))
            continue;
        last = i;
        acc += p[i];
        if (u < acc)
            return i;
    }
    return last;
}

// The attachment's context, with this shot's detuning bound to `detuning_drift` (spec 08 §2.6).
Result<Context> contextFor(const AttachedChannel& ac, const ApplyOptions& o, bool perShot) {
    Context c = ac.context;
    if (perShot && !o.shotDetuningHz.empty() && ac.channel->id() == id::DetuningDrift) {
        const std::uint32_t q = ac.qubits.front().get();
        if (q >= o.shotDetuningHz.size())
            return fail(err::UnknownTarget,
                        std::format("no shot detuning given for qubit {} ({} values)", q,
                                    o.shotDetuningHz.size()));
        c.detuningHz = o.shotDetuningHz[q];
        c.perShotDrift = true;
    }
    return c;
}

Status applyDensityMatrix(qsim::IBackend& b, const AttachedChannel& ac, const Context& ctx,
                          ApplyReport& rep) {
    QXL_TRY_ASSIGN(Kraus k, ac.channel->kraus(ctx));
    if (k.isIdentity()) {
        ++rep.skipped;
        return {};
    }
    if (k.levels != 2) { // leakage: every target must be a d-level site (spec 08 §2.5)
        const auto* dm = dynamic_cast<const qsim::DensityMatrixBackend*>(&b);
        for (auto q : ac.qubits)
            if (!dm || dm->dims()[q.get()] != k.levels)
                return fail(
                    err::LevelsMismatch,
                    std::format("'{}' acts on {}-level sites but qubit {} is not one; allocate the "
                                "density matrix with {} levels",
                                ac.channel->id(), k.levels, q.get(), k.levels));
    }
    QXL_TRY(b.applyChannel(k.ops, ac.qubits));
    ++rep.applied;
    return {};
}

Status applyStateVector(qsim::IBackend& b, const AttachedChannel& ac, const Context& ctx,
                        core::Random& rng, ApplyReport& rep) {
    if (ac.channel->levels() != 2)
        return fail(
            err::UnsupportedBackend,
            std::format("'{}' needs {}-level sites: use the DensityMatrix or Lindblad backend "
                        "(spec 08 §2.5)",
                        ac.channel->id(), ac.channel->levels()));
    QXL_TRY_ASSIGN(Kraus k, ac.channel->kraus(ctx));
    noteClass(rep, FidelityClass::Statistical);
    if (k.isIdentity()) {
        ++rep.skipped;
        return {};
    }
    ++rep.applied;
    if (k.isPauli) { // weights known in advance: no probability pass (spec 08 §7.2)
        const std::uint32_t p = k.pauliIndices[draw(k.pauliWeights, rng)];
        if (p == 0)
            return {};
        return b.applyGate(pauliStringMatrix(p, k.arity), ac.qubits);
    }
    if (k.ops.size() == 1)
        return b.applyGate(k.ops.front(), ac.qubits); // a lone Kraus operator is unitary
    auto* sv = dynamic_cast<qsim::StateVectorBackend*>(&b);
    if (!sv)
        return fail(err::UnsupportedBackend,
                    "stochastic unravelling needs qsim::StateVectorBackend");
    // p_k = ‖K_k ψ‖², draw k, ψ ← K_k ψ/√p_k.
    return sv->applyChannelStochastic(k.ops, ac.qubits, rng);
}

Status applyStabilizer(qsim::IBackend& b, const AttachedChannel& ac, const Context& ctx,
                       const ApplyOptions& o, core::Random& rng, ApplyReport& rep) {
    const IChannel& ch = *ac.channel;
    if (ch.levels() != 2)
        return fail(
            err::UnsupportedBackend,
            std::format("the stabilizer backend cannot represent '{}' (T04 §9.3)", ch.id()));
    auto* st = dynamic_cast<qsim::StabilizerBackend*>(&b);
    if (!st)
        return fail(err::UnsupportedBackend, "Pauli-frame noise needs qsim::StabilizerBackend");
    const auto twirl = ch.twirled(ctx);
    if (!twirl) {
        QXL_TRY(ch.kraus(ctx)); // report the parameter error when that is the cause
        return fail(err::NotPauli, std::format("'{}' has no Pauli twirl", ch.id()));
    }
    noteClass(rep, FidelityClass::Statistical);
    if (!twirl->exact) {
        if (!o.twirlNonPauli)
            return fail(
                err::NotPauli,
                std::format(
                    "'{}' is not a Pauli channel; the stabilizer backend applies its Pauli twirl "
                    "only "
                    "with twirl_non_pauli enabled, which makes the run class Model (spec 08 §7.3)",
                    ch.id()));
        noteClass(rep, FidelityClass::Model);
        if (std::find(rep.twirled.begin(), rep.twirled.end(), ch.id()) == rep.twirled.end())
            rep.twirled.emplace_back(ch.id());
    }
    if (twirl->pI() >= 1.0) {
        ++rep.skipped;
        return {};
    }
    ++rep.applied;
    const std::size_t index = draw(twirl->probs, rng);
    if (index == 0)
        return {};
    static constexpr char kLetters[] = {'I', 'X', 'Y', 'Z'};
    std::vector<std::pair<QubitIndex, char>> terms;
    for (std::size_t k = 0; k < ac.qubits.size(); ++k)
        if (const char c = kLetters[(index >> (2 * k)) & 3u]; c != 'I')
            terms.emplace_back(ac.qubits[k], c);
    return st->applyPauli(qsim::PauliString::fromQubits(st->nQubits(), terms));
}
} // namespace

Status applyChannel(qsim::IBackend& backend, const AttachedChannel& ac, core::Random& rng,
                    const ApplyOptions& options, ApplyReport& report) {
    if (!ac.channel)
        return fail(err::InvalidParameter, "attached channel has no channel object");
    if (ac.qubits.size() != ac.channel->arity())
        return fail(err::BadDimensions,
                    std::format("'{}' acts on {} qubit(s), {} given", ac.channel->id(),
                                ac.channel->arity(), ac.qubits.size()));
    for (std::size_t i = 0; i < ac.qubits.size(); ++i) {
        if (ac.qubits[i].get() >= backend.nQubits())
            return fail(err::UnknownTarget,
                        std::format("'{}' targets qubit {} of a {}-qubit backend", ac.channel->id(),
                                    ac.qubits[i].get(), backend.nQubits()));
        for (std::size_t j = 0; j < i; ++j)
            if (ac.qubits[i] == ac.qubits[j])
                return fail(err::BadDimensions, std::format("'{}' targets qubit {} twice",
                                                            ac.channel->id(), ac.qubits[i].get()));
    }
    Status s;
    switch (backend.kind()) {
    case qsim::Kind::DensityMatrix: {
        QXL_TRY_ASSIGN(const Context c, contextFor(ac, options, true));
        s = applyDensityMatrix(backend, ac, c, report);
        break;
    }
    case qsim::Kind::StateVector: {
        QXL_TRY_ASSIGN(const Context c, contextFor(ac, options, true));
        s = applyStateVector(backend, ac, c, rng, report);
        break;
    }
    case qsim::Kind::Stabilizer: {
        QXL_TRY_ASSIGN(const Context c, contextFor(ac, options, false));
        s = applyStabilizer(backend, ac, c, options, rng, report);
        break;
    }
    case qsim::Kind::Lindblad:
    case qsim::Kind::Trajectories:
        return fail(
            err::UnsupportedBackend,
            std::format("'{}' is a gate-level channel; the {} backend takes its noise as collapse "
                        "operators from NoiseModel::lindbladOperators (spec 08 §7.4)",
                        ac.channel->id(), qsim::kindName(backend.kind())));
    }
    if (!s)
        s.error().notes.push_back(
            std::format("while applying '{}' ({})", ac.channel->id(), placementName(ac.placement)));
    return s;
}

Status applyChannels(qsim::IBackend& backend, std::span<const AttachedChannel> channels,
                     core::Random& rng, const ApplyOptions& options, ApplyReport& report) {
    for (const auto& ac : channels)
        QXL_TRY(applyChannel(backend, ac, rng, options, report));
    return {};
}

Result<qsim::Counts> sampleWithReadout(const qsim::IBackend& backend, const ReadoutModel& readout,
                                       std::uint64_t shots, core::Random& rng) {
    const auto qubits = readout.qubits();
    if (qubits.empty())
        return fail(err::InvalidParameter, "readout of zero qubits");
    if (backend.kind() == qsim::Kind::DensityMatrix || backend.kind() == qsim::Kind::Lindblad) {
        QXL_TRY_ASSIGN(const auto exact, backend.probabilities(qubits));
        QXL_TRY_ASSIGN(const auto observed,
                       readout.applyToProbabilities(exact)); // q = Mᵀp before sampling
        const auto histogram = num::sampleCounts(observed, static_cast<std::size_t>(shots), rng);
        return qsim::countsFromHistogram(histogram, qubits.size());
    }
    QXL_TRY_ASSIGN(auto counts, backend.sample(qubits, shots, rng));
    if (readout.isIdeal())
        return counts;
    return readout.applyToCounts(counts, rng);
}

Result<qsim::Outcome> measureWithReadout(qsim::IBackend& backend, const ReadoutModel& readout,
                                         core::Random& rng) {
    QXL_TRY_ASSIGN(qsim::Outcome outcome, backend.measure(readout.qubits(), rng));
    QXL_TRY(readout.applyToBits(outcome.bits, rng));
    return outcome;
}

} // namespace qlab::noise
