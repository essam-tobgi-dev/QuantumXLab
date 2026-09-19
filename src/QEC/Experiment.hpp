#pragma once
// Spec 16 §6 — logical-error-rate experiments: seeded Monte-Carlo over memory experiments on the
// stabilizer backend, Wilson intervals, per-round rates and threshold sweeps over p and d.
#include "QEC/Decoder.hpp"
#include "QEC/Sampler.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace qlab::qec {

// `Stabilizer` runs the shots on qsim::StabilizerBackend (spec 16 §6, the default). `PauliFrame`
// propagates only the sampled faults; it yields the same detection events and logical outcomes
// shot for shot (same seed → same failures) at a small fraction of the cost, which is what makes
// 10^5-sample sweeps at d = 7 interactive (spec 16 §5). With either engine the Monte-Carlo loop
// does not simulate a shot in which no fault was drawn: `ExperimentRunner::create` has checked on
// the backend that the fault-free run fires no detector and reads the prepared logical value.
enum class SampleEngine : std::uint8_t { Stabilizer, PauliFrame };

struct LogicalErrorExperiment {
    StabilizerCode code;
    NoiseSetting noise = NoiseSetting::CircuitLevel;
    std::vector<double> p;    // physical error rates to sweep
    std::uint32_t rounds = 0; // 0 = one logical cycle, d rounds (T09 §5.3); 1 for code capacity
    std::uint32_t samples = 10000; // per point (spec 16 §6 default)
    std::uint64_t seed = 0x5EED0016ull;
    std::string decoder; // "lookup" | "union_find"; empty = the code's default
    LogicalBasis basis = LogicalBasis::Z;
    DataErrorKind dataError = DataErrorKind::Depolarizing;
    double q = -1.0;        // phenomenological syndrome flip; negative = p
    double idleRatio = 0.0; // circuit level: idle depolarizing = idleRatio · p (T09 §5.4: 1)
    UnionFindOptions unionFind;
    SampleEngine engine = SampleEngine::Stabilizer;
    // Shots spread over core::JobSystem::global(); the result does not depend on it. Set false when
    // the call itself runs inside a JobSystem job (nested fork-join can starve the pool).
    bool parallel = true;
};

struct LogicalErrorPoint {
    double p = 0.0, pL = 0.0, pL_lo = 0.0, pL_hi = 1.0; // Wilson 95 % interval
    std::uint32_t failures = 0, samples = 0;
    std::uint32_t rounds = 0;
    double perRound = 0.0;             // ε_L of spec 16 §6
    std::uint32_t decoderFailures = 0; // shots the decoder rejected (counted as failures)
    FidelityClass cls = FidelityClass::Statistical;
};

// ε_L = ½ (1 − (1 − 2 p_L)^{1/r}); ½ when p_L ≥ ½.
double perRoundErrorRate(double pL, std::uint32_t rounds);

// Each sample: run the memory experiment with injected noise, decode the detection events, apply
// the correction to the logical readout and count a failure when it differs from the prepared
// value — i.e. when error · correction anticommutes with the memory-basis logical operator.
// Shot i of point j uses `Random(seed).stream(j)` → `.stream(2i)` for the faults and
// `.stream(2i + 1)` for the measurement outcomes, so a point is reproducible on its own.
Result<std::vector<LogicalErrorPoint>>
runLogicalErrorExperiment(const LogicalErrorExperiment& experiment);

// One decoded shot with everything the lattice views need (spec 16 §8).
struct ShotResult {
    std::vector<FaultEvent> faults;
    std::vector<std::uint8_t>
        bits; // measurement record (Stabilizer engine) or its flips (PauliFrame)
    std::vector<std::uint8_t> events;  // per detector of the experiment
    std::vector<std::uint8_t> logical; // raw logical readouts, one per logical qubit
    Correction correction;
    std::uint64_t predictedFlips = 0; // logical readouts the correction flips
    bool decoderRejected = false;
    bool failure = false;
};

// A planned experiment with its noise plan, decoding graph and decoder prototype.
class ExperimentRunner {
  public:
    // `decoder` empty = the code's default. Union-find decodes the detector graph of
    // `buildDecodingGraph`; the lookup table decodes the net syndrome (`foldLayers`), which assumes
    // reliable syndromes.
    static Result<ExperimentRunner> create(const StabilizerCode& code,
                                           const ExtractionOptions& extraction,
                                           const NoiseParams& noise, std::string_view decoder = {},
                                           UnionFindOptions unionFind = {});
    // The same from a planned experiment and an explicit noise plan — the entry point for noise
    // that is not one of the three uniform settings, e.g. per-gate rates of a device calibration
    // (spec 16 §4, spec 08 §6): the caller fills `NoisePlan::sites` for `experiment.schedule`.
    static Result<ExperimentRunner> fromPlan(const StabilizerCode& code,
                                             MemoryExperiment experiment, NoisePlan noise,
                                             std::string_view decoder = {},
                                             UnionFindOptions unionFind = {});
    const StabilizerCode& code() const { return code_; }
    const MemoryExperiment& experiment() const { return experiment_; }
    const NoisePlan& noise() const { return noise_; }
    const DecodingGraph* graph() const { return graph_ ? &*graph_ : nullptr; }
    std::string_view decoderName() const { return decoderName_; }

    // Per-thread state: its own decoder instance, frame propagator and scratch buffers. The runner
    // must outlive its workers.
    class Worker {
      public:
        // Shot `index` of a seeded series.
        Result<ShotResult> shot(const core::Random& master, std::uint64_t index,
                                SampleEngine engine);
        // A shot with explicit faults (ordered by `afterOp`) instead of sampled ones.
        Result<ShotResult> shotWithFaults(std::vector<FaultEvent> faults,
                                          core::Random& measurementRng, SampleEngine engine);
        // Failure flag of shot `index` only — the Monte-Carlo inner loop. Same streams as `shot`,
        // so the two agree; a shot without faults returns false without being simulated.
        Result<bool> failed(const core::Random& master, std::uint64_t index, SampleEngine engine,
                            bool* decoderRejected = nullptr);

      private:
        friend class ExperimentRunner;
        Worker(const ExperimentRunner& runner, std::unique_ptr<IDecoder> decoder);
        Status finish(ShotResult& shot, SampleEngine engine, core::Random& measurementRng);
        const ExperimentRunner* runner_;
        std::unique_ptr<IDecoder> decoder_;
        FrameSampler frame_;
        ShotResult scratch_;
    };
    Worker worker() const;

  private:
    StabilizerCode code_;
    MemoryExperiment experiment_;
    NoisePlan noise_;
    std::optional<DecodingGraph> graph_;
    std::optional<TableauSampler> tableau_;
    std::shared_ptr<const IDecoder> prototype_;
    std::string decoderName_;
};

struct ThresholdSweep {
    std::vector<StabilizerCode> codes; // e.g. surface_rot_3, _5, _7
    LogicalErrorExperiment settings;   // `code` is ignored; everything else applies to each code
};
struct ThresholdCurve {
    std::string codeId;
    std::uint32_t d = 0;
    std::vector<LogicalErrorPoint> points;
};
struct ThresholdResult {
    std::vector<ThresholdCurve> curves;
    std::optional<double> crossing; // estimated p where the curves of consecutive distances cross
    double reference = 0.0;         // T09 §6 / spec 16 §6 reference value for the noise setting
    std::string referenceNote;
    FidelityClass cls = FidelityClass::Statistical;
};
// p_L(p) for every code on one grid (spec 16 §6 threshold plot). The crossing is the mean, over
// consecutive code pairs, of the log-log interpolated p at which their curves intersect.
Result<ThresholdResult> runThresholdSweep(const ThresholdSweep& sweep);

} // namespace qlab::qec
