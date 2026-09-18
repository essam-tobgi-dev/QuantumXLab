#pragma once
// Spec 10 §1, spec 12 §3 — a schedule as per-channel complex baseband samples: the AWG model, the
// oscilloscope and the pulse viewer (spec 21 §3.14) read this record.
#include "Pulse/Frames.hpp"
#include "Pulse/Schedule.hpp"
#include <string>
#include <vector>

namespace qlab::pulse {

// Half-open range of sample indices [start, start + count).
struct SampleSpan {
    std::size_t start = 0;
    std::size_t count = 0;
};

struct ChannelSamples {
    ChannelId channel;
    double frameFrequencyHz = 0.0;  // declared carrier the baseband is referred to (0 if none)
    // I + iQ = A·e(t_k − t0)·e^{iψ(t_k)} with ψ the frame's baseband phase (FrameTimeline), held on
    // [t_k, t_{k+1}); |s| ≤ 1 for a verified schedule.
    std::vector<Complex> samples;
    std::vector<SampleSpan> plays;  // the samples each Play covers, in time order
};

struct AcquireWindow {
    ChannelId channel;
    SampleSpan span;
    std::string weights;
    AcquireKind kind = AcquireKind::Integrate;
    int memorySlot = 0;
};

struct SampledSchedule {
    double sampleRateHz = 0.0;  // as requested
    Picoseconds dt{0};          // sample period on the integer-ps time base: round(10¹²/f_s) ps
    int granularity = 16;
    std::size_t length = 0;     // samples per channel: the schedule duration rounded up to whole granules
    std::vector<ChannelSamples> channels;     // every channel with a Play, sorted by ChannelId
    std::vector<AcquireWindow> acquisitions;  // in time order
    const ChannelSamples* find(ChannelId ch) const;
    double sampleTimeS(std::size_t k) const { return static_cast<double>(k) * secondsOf(dt); }
};

// Samples the schedule at `sampleRateHz` (dt quantised to whole ps). Sample k stands for
// [k·dt, (k+1)·dt) (zero-order hold, spec 10 §1) and holds the envelope of every Play with
// t0 ≤ k·dt < t0 + T, evaluated at k·dt. At the device rate a verified schedule puts every Play on
// a whole granule, so each span starts at a multiple of `granularity`; at another rate the
// envelope is point-sampled at the new instants. Errors (kErrSampling): a rate that is not
// positive and finite or finer than 1 ps, granularity < 1, a record above 2³¹ samples.
Result<SampledSchedule> sampleSchedule(const Schedule& schedule, double sampleRateHz, int granularity = 16);

} // namespace qlab::pulse
