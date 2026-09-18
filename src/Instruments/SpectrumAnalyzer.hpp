#pragma once
// Spec 12 §7 — spectrum analyzer: Hann-windowed FFT of the routed node's signal over `span` around
// `center`, displayed as power in dBm per resolution bandwidth. The RBW is the window's equivalent
// noise bandwidth, RBW = ENBW_bins · f_s / N (num::windowEnbw), which fixes the record length N;
// amplitudes are corrected by the coherent gain so a CW tone reads its true power, and the FFT is
// zero-padded so the peak detector's scalloping loss stays below 0.03 dB. The noise floor is
// (k_B T_node + DANL) · RBW. Shows the carrier, the pulse-envelope sidebands, and the LO leakage
// and image of §4. Class Model.
#include "Instruments/InstrumentBase.hpp"

namespace qlab::instr {

struct SpectrumPeak {
    double frequencyHz = 0.0;
    double powerDbm = 0.0;
};

class SpectrumAnalyzer final : public InstrumentBase {
public:
    // Longest record analysed; a smaller RBW than this allows is widened and reported by the
    // "rbw_effective" marker.
    static constexpr std::size_t kMaxRecord = std::size_t{1} << 18;

    explicit SpectrumAnalyzer(std::uint32_t index = 0);
    static SettingSchema makeSchema();

    // Zero-span marker reading: power (dBm) of the Hann-windowed record at exactly `frequencyHz`,
    // plus the expected noise-floor power in the RBW. Deterministic (no noise draw) and free of
    // scalloping, which is what the mixer-calibration tool minimises (spec 12 §13).
    Result<double> markerPowerDbm(double frequencyHz) const;
    // Local maxima of a displayed trace at least `prominenceDb` above both neighbours' valleys,
    // strongest first.
    static std::vector<SpectrumPeak> findPeaks(const Trace& trace, std::size_t maxPeaks = 8, double prominenceDb = 6.0);
    std::optional<double> query(std::string_view path) const override; // trace / peak: strongest line of the last sweep (dBm)

protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;

private:
    struct Record {
        Signal signal;
        double rbwHz = 0.0;       // effective
        double floorWatts = 0.0;  // (node + DANL) noise power in the RBW
    };
    Result<Record> record(const SettingValues& v, std::uint64_t noiseSeed) const;
    Result<Trace> sweep(const ChannelDesc& channel, AcquireContext& ctx) const;
    mutable std::atomic<double> lastPeakDbm_{std::numeric_limits<double>::quiet_NaN()};
    mutable std::atomic<double> lastPeakHz_{std::numeric_limits<double>::quiet_NaN()};
};

} // namespace qlab::instr
