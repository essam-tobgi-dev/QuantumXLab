#pragma once
// Spec 12 §5 — digitizer / readout ADC. For every shot the readout line delivers
//   v(t) = Re[Ṽ_s(t) e^{i2π f_IF t}] + n(t)        (ReadoutChain.hpp: cavity field, chain gain,
//   T_sys)
// which the ADC samples at `sample_rate` and quantises to `resolution_bits`; the demodulator forms
//   I + iQ = (2/N) Σ_k w_k v_k e^{−i2π f_IF t_k}     (T07 (6.1); the 2 restores the envelope
//   amplitude)
// with boxcar, matched (T07 (6.2)) or custom weights — one IQ point per shot. With `averages` > 1 a
// point is the mean of that many shots, drawn from the exact Gaussian of the integrated noise.
// Clouds keep the prepared/assigned labels; the discriminator (Discriminator.hpp) is trained from a
// `readout_assignment` calibration run. Clouds and matrices are Statistical, the SNR theory Model.
#include "Instruments/Discriminator.hpp"
#include "Instruments/InstrumentBase.hpp"
#include "Instruments/ReadoutChain.hpp"
#include <array>
#include <deque>

namespace qlab::instr {

struct TrainingReport {
    Discriminator discriminator;
    AssignmentEstimate assignment;
    double snrMeasured = 0.0; // |μ_1 − μ_0|/σ of the fitted mixture
    double snrExpected = 0.0; // for the window and weights in use, ring-up included (Model)
    double snrTheory = 0.0;   // T05 (6.7) steady state: |α_1 − α_0| √(2ηκT) (Model)
    ReadoutParams params;
    FidelityClass cls = FidelityClass::Statistical;
};

class Digitizer final : public InstrumentBase, public ISignalSource {
  public:
    static constexpr std::uint32_t kDemodChannels = 8; // multiplexed tones per readout line

    explicit Digitizer(std::uint32_t index = 0);
    static SettingSchema makeSchema();

    // Qubit read by demodulator k: bindings().channels[k] (its a[i] / m[i] index), else k.
    std::uint32_t qubitOfChannel(std::uint32_t k) const;
    // Readout parameters of demodulator k: the override when one is set (test benches, custom
    // resonators), else derived from the Environment; `window_ns`, the schedule's acquire window
    // and the played tone amplitude are applied on top.
    Result<ReadoutParams> readoutParams(std::uint32_t k) const;
    void setReadoutOverride(std::uint32_t k, std::optional<ReadoutParams> params);
    // `integration = custom`: weights on the acquisition grid (resampled when the length differs).
    void setCustomWeights(std::uint32_t k, std::vector<Complex> weights);

    // One IQ point per entry of `states` — the true state of the qubit in each shot (0, 1, or 2
    // with `states: 3`). Points are labelled, classified when a discriminator exists, and appended
    // to the cloud. This is what the runtime calls per shot of a pulse-level run.
    Result<std::vector<IqPoint>> measure(std::uint32_t k, std::span<const std::uint8_t> states,
                                         core::Random& rng);
    // The `readout_assignment` calibration run (spec 10 §9): `shotsPerState` shots of each state,
    // discriminator training, assignment matrix with Wilson σ.
    Result<TrainingReport> calibrate(std::uint32_t k, std::size_t shotsPerState,
                                     std::uint64_t seed);
    // Training from shots taken elsewhere (the App's own calibration program).
    Result<TrainingReport> train(std::uint32_t k, std::span<const IqPoint> labelled);
    std::optional<Discriminator> discriminator(std::uint32_t k) const;
    std::optional<TrainingReport> lastTraining(std::uint32_t k) const;
    // Bit for one IQ point; −1 before training.
    int classify(std::uint32_t k, double i, double q) const;
    std::vector<IqPoint> cloud(std::uint32_t k) const;
    void clearCloud(std::uint32_t k);

    // Ports "in" (the ADC input record of demodulator `scope_channel`, real, baseband reference)
    // and "demod" (v(t) e^{−i2π f_IF t} smoothed over one IF period).
    Result<Signal> signal(std::string_view port, const SignalRequest& request) const override;
    double sampleRateHz(std::string_view port) const override;
    std::optional<double>
    query(std::string_view path) const override; // ch[k].iq(.i|.q), trigger, snr

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
    std::string conflict(const SettingValues& values) const override;

  private:
    struct Synth; // everything precomputed for one demodulator configuration
    Result<Synth> prepare(std::uint32_t k, const SettingValues& v) const;
    Result<std::vector<double>> shotRecord(const Synth& s, int state, core::Random& rng) const;
    Result<Trace> cloudTrace(const ChannelDesc& channel, AcquireContext& ctx, std::uint32_t k);
    Result<Trace> histogramTrace(const ChannelDesc& channel, AcquireContext& ctx);
    Result<Trace> recordTrace(const ChannelDesc& channel, AcquireContext& ctx);

    struct Demod {
        std::optional<ReadoutParams> override;
        std::vector<Complex> customWeights;
        std::deque<IqPoint> cloud;
        std::optional<Discriminator> discriminator;
        std::optional<TrainingReport> training;
        std::uint64_t lastShot = ~std::uint64_t{0};
        std::uint64_t alternate = 0;
    };
    mutable std::mutex demodMu_;
    std::array<Demod, kDemodChannels> demod_;
};

} // namespace qlab::instr
