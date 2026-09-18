#pragma once
// Spec 12 §10 — timing and control unit (`controller`): the FPGA that sequences the AWGs and
// digitizers. Settings: `clock_ref`, `feedforward_latency_ns` (spec 10 §10), `shot_loop` (the
// repetition delay) and `sequencer_memory`. Its trace channel is the run timeline — every
// instruction of the schedule with its start and duration, and which one is executing at the
// playhead of a live run — read by the 3D pulse-flow animation (spec 17 §8). Class Exact.
// The rack's 10 MHz reference, clock distribution and trigger unit are descriptors of this class
// (channels `locked` and `rate`).
#include "Instruments/InstrumentBase.hpp"

namespace qlab::instr {

// One row of the timeline.
struct TimelineEntry {
    std::size_t index = 0;   // position in time order
    std::string label;       // "play d[0] drag", "shift_phase d[1]", "acquire a[0]"
    std::string channel;     // "d[0]"
    int kind = 0;            // 0 play, 1 frame op, 2 acquire, 3 delay, 4 barrier
    double t0S = 0.0, durationS = 0.0;
};

class Controller final : public InstrumentBase {
public:
    explicit Controller(std::uint32_t index = 0);
    static SettingSchema makeSchema();

    // The schedule of the bound run in time order (stable for equal start times).
    static std::vector<TimelineEntry> timeline(const pulse::Schedule& schedule);
    // Entry executing at `timeS`: the latest-starting instruction whose window contains it;
    // zero-duration frame operations count only at their exact start. nullopt between instructions.
    static std::optional<std::size_t> executingAt(const std::vector<TimelineEntry>& entries, double timeS);
    // Shot repetition rate 1 / (schedule duration + shot_loop), in Hz.
    double triggerRateHz() const;
    // 1σ of the shot period: trigger jitter in quadrature with the reference's fractional stability
    // over one period. Unlocked (`clock_ref = internal`) the free-running oscillator's 1e-8 applies.
    double periodSigmaS() const;
    std::optional<double> query(std::string_view path) const override; // locked, rate, shot, instruction, playhead

protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
    bool triggerSourceSet(const SettingValues&) const override { return false; }
};

} // namespace qlab::instr
