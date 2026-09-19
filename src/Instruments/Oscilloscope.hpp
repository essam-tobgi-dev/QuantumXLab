#pragma once
// Spec 12 §7 — oscilloscope: four channels, each attached to a routing node, showing baseband I/Q
// of an AWG channel, a flux pulse, or the digitizer's input and demodulated voltage.
//   view = envelope  the schedule envelope V_fs·s_k of the node, point-sampled on the scope's grid
//                    in integer picoseconds. No front end is applied: it is the schedule — Exact.
//   view = output    the signal actually on the cable through the scope's front end — Model: a
//                    Gaussian response with −3 dB at `bandwidth` (exact for the zero-order-hold
//                    record: a difference of normal CDFs per held sample), then 8-bit quantisation
//                    over the 8 vertical divisions. An RF node is scaled by the response at its
//                    carrier, so a 5 GHz carrier is invisible on a 1 GHz scope.
// Record: 10 divisions of `timebase`, from the trigger point (`run`: start of the schedule; an edge
// on a channel: the first crossing of `trigger_level`, shown one division into the record).
#include "Instruments/InstrumentBase.hpp"

namespace qlab::instr {

class Oscilloscope final : public InstrumentBase {
  public:
    static constexpr std::uint32_t kChannels = 4;
    static constexpr std::size_t kMemoryDepth =
        200000; // samples per channel; longer records sample slower

    explicit Oscilloscope(std::uint32_t index = 0);
    static SettingSchema makeSchema();
    std::optional<double>
    query(std::string_view path) const override; // ch[k]: value at the playhead

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
    bool triggerSourceSet(const SettingValues& values) const override;

  private:
    struct Capture {
        Signal signal;         // the node's record (whole schedule)
        std::string component; // I | Q | magnitude
        bool envelope = true;
    };
    Result<Capture> capture(std::uint32_t k, const SettingValues& v, std::uint64_t seed) const;
    static double componentOf(const Complex& z, const std::string& component);
    // Trigger time in seconds and whether an edge was found.
    Result<std::pair<double, bool>> triggerTime(const SettingValues& v, std::uint64_t seed) const;
};

} // namespace qlab::instr
