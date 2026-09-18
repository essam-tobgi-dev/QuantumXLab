#pragma once
// Spec 12 §3 — arbitrary waveform generator `awg`: takes the pulse::Schedule of its channels,
// samples it at `sample_rate`, enforces the 16-sample granularity, places the envelope on the
// intermediate frequency, applies the mixer-calibration predistortion of §13, and quantises I and Q
// to `resolution_bits` (Δ = 2 V_fs / 2^N). A record longer than `memory_samples` is a Fault carrying
// the sample count — the reason long pulse-level programs must be looped (spec 14 §7).
//
// Sign convention. The drive is s(t) = Re[A e(t) e^{−i(2πft − φ)}] (spec 10 §3) and the mixer forms
// Re[(I + iQ) e^{+i2π f_LO t}] (T07 (5.1), spec 12 §4), so the DAC streams are
//   I + iQ = V_fs · conj(s_k) · e^{+i2π f_IF t_k},
// which lands the pulse at f_LO + f_IF with the DRAG notch on the f01 + α side. The oscilloscope's
// envelope view shows V_fs·s_k itself (class Exact: it is the schedule).
#include "Instruments/InstrumentBase.hpp"
#include "Pulse/Sampling.hpp"
#include <cstdint>

namespace qlab::instr {

// Per-channel predistortion written by the mixer-calibration tool (spec 12 §13).
struct IqCorrection {
    double offsetI = 0.0, offsetQ = 0.0; // volts added to the streams (nulls LO leakage)
    double gainRatio = 1.0;              // a: Q-path amplitude relative to I
    double phaseSkewRad = 0.0;           // φ: I' = I − a Q sin φ, Q' = a Q cos φ (nulls the image)
    bool identity() const { return offsetI == 0.0 && offsetQ == 0.0 && gainRatio == 1.0 && phaseSkewRad == 0.0; }
};

// One channel's DAC record over the whole schedule.
struct AwgStreams {
    double sampleRateHz = 0.0;
    double dtS = 0.0;
    double fullScaleV = 0.0;
    int bits = 14;
    std::vector<Complex> envelope;  // V_fs · s_k: the schedule envelope in volts (Exact)
    std::vector<Complex> ideal;     // IF-modulated, predistorted, before the DAC
    std::vector<Complex> output;    // quantised I + iQ in volts
    std::vector<std::int32_t> codeI, codeQ;
    std::size_t memorySamples = 0;  // record length = memory used per channel
};

class Awg final : public InstrumentBase, public ISignalSource {
public:
    static constexpr std::uint32_t kChannels = 4; // per unit; a device needs ⌈N_lines / 4⌉ units

    explicit Awg(std::uint32_t index = 0);
    static SettingSchema makeSchema();

    // Schedule channel played by output port k: bindings().channels[k], or — when none are bound —
    // the k-th drive-like channel of the schedule in ChannelId order.
    std::optional<pulse::ChannelId> channelOfPort(std::uint32_t port, const pulse::Schedule& schedule) const;
    // The DAC record of one port for the bound schedule. Errors: NotBound (no run or schedule),
    // MemoryOverflow (also raises the Fault), UnknownChannel.
    Result<AwgStreams> render(std::uint32_t port) const;
    // The schedule envelope after N-bit quantisation, s_q = Q(V_fs s)/V_fs, for the Lindblad backend
    // when `apply_quantization` is on (spec 12 §3).
    Result<std::vector<Complex>> quantisedEnvelope(std::uint32_t port) const;

    IqCorrection correction(std::uint32_t port) const;
    // Used by the mixer-calibration tool; values are clamped to the schema like any set().
    Result<void> setCorrection(std::uint32_t port, const IqCorrection& c);
    double intermediateFrequencyHz() const;
    double fullScaleVolts() const;
    // What the DAC holds between pulses: the quantised DC offsets, I + iQ in volts.
    Complex idleOutput(std::uint32_t port) const;

    // Ports "ch[k]". `envelopeView` gives V_fs·s_k (Exact); otherwise the quantised DAC output.
    Result<Signal> signal(std::string_view port, const SignalRequest& request) const override;
    double sampleRateHz(std::string_view port) const override;
    std::optional<double> query(std::string_view path) const override; // running, ch[k].waveform, seq.shot

protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
    void settingChanged(std::string_view key) override;

private:
    struct Sampled {
        std::shared_ptr<const pulse::Schedule> schedule; // keeps the key address alive
        double rateHz = 0.0;
        std::shared_ptr<const pulse::SampledSchedule> record;
    };
    Result<std::shared_ptr<const pulse::SampledSchedule>> sampled(const std::shared_ptr<const pulse::Schedule>& s,
                                                                  double rateHz) const;
    Result<AwgStreams> renderWith(std::uint32_t port, const SettingValues& v, const RunView& run) const;
    mutable std::mutex cacheMu_;
    mutable Sampled cache_;
};

// N-bit mid-tread quantiser over ±V_fs: code = clamp(round(v/Δ), −2^(N−1), 2^(N−1) − 1).
std::int32_t quantiseCode(double volts, double fullScaleV, int bits);
inline double lsbVolts(double fullScaleV, int bits) { return 2.0 * fullScaleV / static_cast<double>(1u << bits); }
// Port index of "ch[k]" / "ch[k].waveform"; nullopt when the text is not of that form.
std::optional<std::uint32_t> parsePortIndex(std::string_view text);

} // namespace qlab::instr
