#pragma once
// Spec 12 §2 — microwave signal generator `sg_mw`: the continuous-wave LO of an up/down-conversion
// chain. s(t) = √(2 P Z0) cos(2π f t + φ_n(t)) with φ_n drawn from a 1/f² + white phase-noise
// spectrum matching `phase_noise_dbc` at 10 kHz offset (T07 §7). A source: its "trace" is the
// phase-noise mask; the carrier is seen on the spectrum analyzer and scope through the routing.
#include "Instruments/InstrumentBase.hpp"

namespace qlab::instr {

// Single-sideband phase noise L(f) = L_10k (10 kHz / f)² + L_floor, linear ratios per Hz.
struct PhaseNoiseMask {
    double at10kHzDbc = -125.0;
    double floorDbc = -150.0;
    double dbcPerHz(double offsetHz) const;
    // φ_n[k] at sample rate fs: a random walk (the 1/f² part: one-sided S_φ = 2 L, step variance
    // σ_w² = L_10k (2π·10⁴)² / fs) plus white phase noise of variance L_floor·fs.
    std::vector<double> synthesize(std::size_t n, double sampleRateHz, core::Random& rng) const;
};

class Generator final : public InstrumentBase, public ISignalSource {
public:
    explicit Generator(std::uint32_t index = 0);
    static SettingSchema makeSchema();

    PhaseNoiseMask mask() const;
    // Carrier actually produced: the set frequency, offset by the free-running timebase error
    // (5·10⁻⁸ relative, Model) when `ref` is `internal`.
    double carrierHz() const;
    bool outputOn() const;
    double powerDbm() const;

    // Port "rf": √(2 P Z0)·e^{iφ_n(t)} referred to carrierHz(); zeros when the output is off.
    Result<Signal> signal(std::string_view port, const SignalRequest& request) const override;
    double sampleRateHz(std::string_view) const override { return 0.0; } // CW: follows the request
    std::optional<double> query(std::string_view path) const override; // f, P_dBm, on

protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
    bool triggerSourceSet(const SettingValues&) const override { return false; }
};

} // namespace qlab::instr
