// Spec 16 §6 — seeded Monte-Carlo sweeps: p_L(p) with Wilson 95 % intervals for one code, and the
// threshold plot data (several distances on one p grid, crossing estimate, reference value).
#include "Core/JobSystem.hpp"
#include "QEC/Experiment.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <mutex>

namespace qlab::qec {
namespace {

std::uint32_t roundsOf(const LogicalErrorExperiment& e) {
    if (e.rounds != 0)
        return e.rounds;
    return e.noise == NoiseSetting::CodeCapacity
               ? 1u
               : std::max<std::uint32_t>(1u, e.code.d); // T09 §5.3: d rounds
}

Result<LogicalErrorPoint> runPoint(const LogicalErrorExperiment& e, std::size_t index, double p) {
    ExtractionOptions extraction;
    extraction.rounds = roundsOf(e);
    extraction.basis = e.basis;
    NoiseParams noise;
    noise.setting = e.noise;
    noise.p = p;
    noise.q = e.q;
    noise.dataError = e.dataError;
    noise.pIdle = e.noise == NoiseSetting::CircuitLevel ? e.idleRatio * p : 0.0;
    QXL_TRY_ASSIGN(const ExperimentRunner runner,
                   ExperimentRunner::create(e.code, extraction, noise, e.decoder, e.unionFind));

    const core::Random master = core::Random(e.seed).stream(index);
    std::atomic<std::uint32_t> failures{0}, rejected{0};
    std::mutex errorMutex;
    std::optional<Error> firstError;
    auto body = [&](std::size_t begin, std::size_t end) {
        ExperimentRunner::Worker worker = runner.worker();
        std::uint32_t localFailures = 0, localRejected = 0;
        for (std::size_t i = begin; i < end; ++i) {
            bool decoderRejected = false;
            auto failed = worker.failed(master, i, e.engine, &decoderRejected);
            if (!failed) {
                std::lock_guard lock(errorMutex);
                if (!firstError)
                    firstError = std::move(failed.error());
                return;
            }
            localFailures += *failed ? 1u : 0u;
            localRejected += decoderRejected ? 1u : 0u;
        }
        failures += localFailures;
        rejected += localRejected;
    };
    if (e.parallel)
        core::JobSystem::global().parallelFor(e.samples, 256, body);
    else
        body(0, e.samples);
    if (firstError)
        return std::unexpected(std::move(*firstError));

    LogicalErrorPoint point;
    point.p = p;
    point.failures = failures.load();
    point.samples = e.samples;
    point.rounds = extraction.rounds;
    point.decoderFailures = rejected.load();
    point.pL = double(point.failures) / double(point.samples);
    const Interval ci = wilsonInterval(point.failures, point.samples, kZ95);
    point.pL_lo = ci.lo;
    point.pL_hi = ci.hi;
    point.perRound = perRoundErrorRate(point.pL, point.rounds);
    return point;
}

// p at which two curves on a common grid intersect, interpolating log p_L linearly in log p.
std::optional<double> crossingOf(const std::vector<LogicalErrorPoint>& small,
                                 const std::vector<LogicalErrorPoint>& large) {
    const std::size_t n = std::min(small.size(), large.size());
    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (small[i].pL <= 0 || large[i].pL <= 0 || small[i + 1].pL <= 0 || large[i + 1].pL <= 0)
            continue;
        if (small[i].p <= 0 || small[i + 1].p <= small[i].p)
            continue;
        const double g0 = std::log(large[i].pL / small[i].pL),
                     g1 = std::log(large[i + 1].pL / small[i + 1].pL);
        if (g0 < 0.0 && g1 >= 0.0) { // below threshold the larger code wins, above it loses
            const double t = g0 / (g0 - g1);
            return std::exp(std::log(small[i].p) +
                            t * (std::log(small[i + 1].p) - std::log(small[i].p)));
        }
    }
    return std::nullopt;
}

} // namespace

Result<std::vector<LogicalErrorPoint>> runLogicalErrorExperiment(const LogicalErrorExperiment& e) {
    if (e.samples == 0)
        return fail(err::BadOptions,
                    "a logical-error experiment needs at least one sample per point");
    if (e.p.empty())
        return fail(err::BadOptions,
                    "a logical-error experiment needs at least one physical error rate");
    std::vector<LogicalErrorPoint> points;
    for (std::size_t i = 0; i < e.p.size(); ++i) {
        QXL_TRY_ASSIGN(LogicalErrorPoint point, runPoint(e, i, e.p[i]));
        points.push_back(point);
    }
    return points;
}

Result<ThresholdResult> runThresholdSweep(const ThresholdSweep& sweep) {
    if (sweep.codes.size() < 2)
        return fail(err::BadOptions, "a threshold sweep needs at least two code distances");
    ThresholdResult result;
    for (const StabilizerCode& code : sweep.codes) {
        LogicalErrorExperiment e = sweep.settings;
        e.code = code;
        QXL_TRY_ASSIGN(std::vector<LogicalErrorPoint> points, runLogicalErrorExperiment(e));
        result.curves.push_back({code.id, code.d, std::move(points)});
    }
    std::sort(result.curves.begin(), result.curves.end(),
              [](const auto& a, const auto& b) { return a.d < b.d; });
    double sum = 0.0;
    std::uint32_t found = 0;
    for (std::size_t c = 0; c + 1 < result.curves.size(); ++c)
        if (const auto x = crossingOf(result.curves[c].points, result.curves[c + 1].points)) {
            sum += *x;
            ++found;
        }
    if (found > 0)
        result.crossing = sum / double(found);

    // Reference values of spec 16 §6 / T09 §6 (MWPM; union-find sits slightly below).
    switch (sweep.settings.noise) {
    case NoiseSetting::CodeCapacity:
        result.reference = 0.103;
        result.referenceNote =
            "code capacity, independent X/Z flips: MWPM ≈ 10.3 %, union-find ≈ 9.9 %";
        if (sweep.settings.dataError == DataErrorKind::Depolarizing) {
            result.reference *= 1.5; // one memory basis sees the X (or Z) component only: 2p/3
            result.referenceNote += "; depolarizing data errors put 2p/3 on each component, so the "
                                    "crossing sits at 1.5× in p";
        }
        break;
    case NoiseSetting::Phenomenological:
        result.reference = 0.029;
        result.referenceNote = "phenomenological, q = p, independent X/Z flips: MWPM ≈ 2.9 %";
        break;
    case NoiseSetting::CircuitLevel:
        result.reference = 0.0075;
        result.referenceNote =
            "circuit-level depolarizing: ≈ 0.5–1 % depending on the noise model details";
        break;
    }
    return result;
}

} // namespace qlab::qec
