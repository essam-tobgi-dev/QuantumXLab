#pragma once
// Spec 21 §3.14 — pulse viewer layout (pure; no GL, no ImGui). One row per `pulse::Channel` of the
// schedule, sharing the time axis in ns: the baseband envelope Re/Im sampled on the device grid,
// frame phase jumps as ticks, frequency changes as annotations, acquisition windows as spans.
// The zoom level sets the decimation (spec 22 §2): above `maxPoints` samples in the window a row
// carries the min/max envelope of each screen column instead of the samples themselves, so a fast
// oscillation still shows its extent rather than aliasing.
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include "Pulse/Schedule.hpp"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::viz::layout {

// One drawn signal. Undecimated: `re`/`im` at `tNs`. Decimated: the same arrays hold the column
// means and `reLo`…`imHi` the column extrema (drawn as a shaded band under the line).
struct PulseTrace {
    std::vector<double> tNs, re, im;
    std::vector<double> reLo, reHi, imLo, imHi;
    bool decimated = false;
    double maxAbs = 0.0;
    std::size_t sourceSamples = 0; // samples before decimation
};

struct FrameMark {
    double tNs = 0.0;
    double phase = 0.0;      // accumulated frame phase after the op (rad, wrapped to (−π, π])
    double deltaPhase = 0.0; // the jump itself
};
struct FrequencyMark {
    double tNs = 0.0;
    double frequencyHz = 0.0; // frame frequency after the op
};
struct AcquireSpan {
    double t0Ns = 0.0, t1Ns = 0.0;
    int memorySlot = 0;
};

struct PulseRow {
    pulse::ChannelId channel;
    std::string label;        // "d[0]", "u[0,1]", "m[2]", "a[2]"
    std::uint32_t qubit = 0;  // channel.primaryQubit()
    bool acquire = false;     // acquisition row: windows only, no envelope
    double frequencyHz = 0.0; // declared carrier of the channel's frame (0 when undeclared)
    PulseTrace trace;
    std::vector<FrameMark> phaseJumps;
    std::vector<FrequencyMark> frequencyChanges;
    std::vector<AcquireSpan> acquisitions;
};

struct PulseLayoutOptions {
    double t0Ns = 0.0, t1Ns = 0.0;      // visible window; t1 ≤ t0 means the whole schedule
    std::size_t maxPoints = 2000;       // decimation target per row (spec 22 §2)
    std::span<const QubitIndex> qubits; // channel filter by qubit selection (empty = every channel)
};

struct PulseModel {
    std::vector<PulseRow> rows;    // channel order: drive, control, flux, measure, acquire
    double durationNs = 0.0;       // whole schedule
    double t0Ns = 0.0, t1Ns = 0.0; // window actually laid out
    double dtNs = 0.0;             // device sample period
    double maxAbs = 0.0;           // largest |envelope| over all rows (shared y scale)
    const PulseRow* row(pulse::ChannelId ch) const;
};

PulseModel buildPulseModel(const pulse::Schedule& schedule, const PulseLayoutOptions& options = {});

// Hover readout of spec 21 §3.14: the sample value, frame phase and channel frequency at `tNs`.
struct PulseReadout {
    double tNs = 0.0;
    double re = 0.0, im = 0.0;
    double framePhase = 0.0;
    double frequencyHz = 0.0;
    bool inAcquisition = false;
};
std::optional<PulseReadout> readoutAt(const PulseModel& model, std::size_t row, double tNs);

} // namespace qlab::viz::layout
