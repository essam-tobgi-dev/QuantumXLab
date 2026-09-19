// Spec 16 §6 — one decoded shot of a memory experiment: sample faults, run the engine, turn the
// record into detection events, decode, and compare the corrected logical readout with the
// prepared value.
#include "QEC/Experiment.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qec {

Result<ExperimentRunner> ExperimentRunner::create(const StabilizerCode& code,
                                                  const ExtractionOptions& extraction,
                                                  const NoiseParams& noise,
                                                  std::string_view decoder,
                                                  UnionFindOptions unionFind) {
    QXL_TRY_ASSIGN(MemoryExperiment experiment, planMemoryExperiment(code, extraction));
    QXL_TRY_ASSIGN(NoisePlan plan, planNoise(experiment, noise));
    return fromPlan(code, std::move(experiment), std::move(plan), decoder, unionFind);
}

Result<ExperimentRunner> ExperimentRunner::fromPlan(const StabilizerCode& code,
                                                    MemoryExperiment experiment, NoisePlan noise,
                                                    std::string_view decoder,
                                                    UnionFindOptions unionFind) {
    if (!experiment.options.finalDataMeasurement || experiment.observables.empty())
        return fail(err::BadOptions,
                    "a logical-error experiment needs the final data readout (spec 16 §6)");
    if (experiment.nData != code.n || experiment.nAncilla != code.checkCount())
        return fail(err::BadOptions,
                    std::format("the experiment was not planned for code '{}'", code.id));
    ExperimentRunner r;
    r.code_ = code;
    r.experiment_ = std::move(experiment);
    r.noise_ = std::move(noise);
    r.decoderName_ = decoder.empty() ? code.defaultDecoder : std::string(decoder);
    if (r.decoderName_ == "union_find") {
        if (!code.isMatchable())
            return fail(
                err::NotMatchable,
                std::format("code '{}' is not matching-type; use the lookup decoder", code.id));
        QXL_TRY_ASSIGN(DecodingGraph g, buildDecodingGraph(r.experiment_, r.noise_));
        r.graph_ = g;
        r.prototype_ = std::make_shared<UnionFindDecoder>(std::move(g), unionFind);
    } else if (r.decoderName_ == "lookup") {
        QXL_TRY_ASSIGN(LookupDecoder d, LookupDecoder::create(code));
        r.prototype_ = std::make_shared<LookupDecoder>(std::move(d));
    } else {
        return fail(
            ErrorCode::Unsupported,
            std::format("decoder '{}' is not available (lookup, union_find)", r.decoderName_));
    }
    QXL_TRY_ASSIGN(TableauSampler t, TableauSampler::create(r.experiment_.schedule));
    r.tableau_ = std::move(t);

    // The fault-free run must fire no detector and read the prepared logical value: this is what
    // lets a shot without faults skip the simulation, and it rejects detector sets that are not
    // deterministic for a user-supplied code.
    core::Random rng(0xC0DE5EEDull);
    std::vector<std::uint8_t> bits;
    QXL_TRY(r.tableau_->run({}, rng, bits));
    const auto events = r.experiment_.detectionEvents(bits),
               logical = r.experiment_.observableValues(bits);
    const auto isSet = [](std::uint8_t b) { return b != 0; };
    if (std::any_of(events.begin(), events.end(), isSet) ||
        std::any_of(logical.begin(), logical.end(), isSet))
        return fail(
            ErrorCode::Internal,
            std::format("code '{}': the fault-free experiment fires a detector or flips the "
                        "logical readout",
                        code.id));
    return r;
}

ExperimentRunner::Worker ExperimentRunner::worker() const {
    return Worker(*this, prototype_->clone());
}

ExperimentRunner::Worker::Worker(const ExperimentRunner& runner, std::unique_ptr<IDecoder> decoder)
    : runner_(&runner), decoder_(std::move(decoder)), frame_(runner.experiment_.schedule) {}

Result<ShotResult> ExperimentRunner::Worker::shot(const core::Random& master, std::uint64_t index,
                                                  SampleEngine engine) {
    ShotResult s;
    core::Random noiseRng = master.stream(2 * index), measurementRng = master.stream(2 * index + 1);
    drawFaults(runner_->noise_, noiseRng, s.faults);
    QXL_TRY(finish(s, engine, measurementRng));
    return s;
}

Result<ShotResult> ExperimentRunner::Worker::shotWithFaults(std::vector<FaultEvent> faults,
                                                            core::Random& measurementRng,
                                                            SampleEngine engine) {
    ShotResult s;
    s.faults = std::move(faults);
    QXL_TRY(finish(s, engine, measurementRng));
    return s;
}

Result<bool> ExperimentRunner::Worker::failed(const core::Random& master, std::uint64_t index,
                                              SampleEngine engine, bool* decoderRejected) {
    core::Random noiseRng = master.stream(2 * index), measurementRng = master.stream(2 * index + 1);
    drawFaults(runner_->noise_, noiseRng, scratch_.faults);
    if (decoderRejected)
        *decoderRejected = false;
    if (scratch_.faults.empty())
        return false; // fault-free: checked once in ExperimentRunner::create
    QXL_TRY(finish(scratch_, engine, measurementRng));
    if (decoderRejected)
        *decoderRejected = scratch_.decoderRejected;
    return scratch_.failure;
}

Status ExperimentRunner::Worker::finish(ShotResult& s, SampleEngine engine,
                                        core::Random& measurementRng) {
    const MemoryExperiment& ex = runner_->experiment_;
    if (engine == SampleEngine::PauliFrame) {
        QXL_TRY(frame_.run(s.faults, s.bits));
    } else {
        QXL_TRY(runner_->tableau_->run(s.faults, measurementRng, s.bits));
    }
    s.events = ex.detectionEvents(s.bits);
    s.logical = ex.observableValues(s.bits);
    s.correction = Correction{};
    s.correction.pauli = PauliString::identity(ex.nData);
    s.predictedFlips = 0;
    s.decoderRejected = false;
    if (std::any_of(s.events.begin(), s.events.end(), [](std::uint8_t e) { return e != 0; })) {
        const bool lookup = runner_->decoderName_ == "lookup";
        auto decoded = decoder_->decode(lookup ? SyndromeLattice::foldLayers(ex, s.events)
                                               : SyndromeLattice{s.events});
        if (!decoded) {
            s.decoderRejected = true;
        } else {
            s.correction = std::move(*decoded);
            s.predictedFlips = s.correction.observableMask;
            if (lookup) // the table returns a Pauli: it flips readout l iff it anticommutes with
                        // logical l
                for (std::uint32_t l = 0; l < runner_->code_.k && l < 64; ++l)
                    if (!s.correction.pauli.commutesWith(
                            runner_->code_.logical(ex.options.basis, l)))
                        s.predictedFlips |= std::uint64_t{1} << l;
        }
    }
    s.failure = s.decoderRejected;
    for (std::size_t l = 0; l < s.logical.size() && l < 64; ++l)
        s.failure = s.failure || (s.logical[l] != ((s.predictedFlips >> l) & 1u));
    return {};
}

double perRoundErrorRate(double pL, std::uint32_t rounds) {
    if (rounds == 0)
        return 0.0;
    if (pL >= 0.5)
        return 0.5;
    return 0.5 * (1.0 - std::pow(1.0 - 2.0 * pL, 1.0 / double(rounds)));
}

} // namespace qlab::qec
