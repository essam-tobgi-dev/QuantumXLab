#pragma once
// Spec 10 §3, §5 — frames, schedule instructions, and the time-ordered schedule.
#include "Core/StrongType.hpp"
#include "Pulse/Channel.hpp"
#include "Pulse/Errors.hpp"
#include "Pulse/Waveform.hpp"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace qlab::hw {
struct Device;
}

namespace qlab::pulse {

// Integer picosecond time base (spec 10 §1; `pulse::Duration` in spec 13 §2 and spec 14).
using Duration = Picoseconds;

inline Picoseconds psFromSeconds(double s) {
    return Picoseconds{static_cast<std::int64_t>(std::llround(s * 1e12))};
}
inline constexpr double secondsOf(Picoseconds p) {
    return static_cast<double>(p.value) * 1e-12;
}

// Spec 10 §1 — the sample grid. A duration is n·dt with n a multiple of `granularity`
// (16 by default, the AWG memory alignment of spec 12 §3); `minSamples` raises the result,
// which is how `Play` meets the `min_pulse_samples` floor. Rounding is to nearest, so the
// quantised value never differs from the requested one by more than half a granule (a positive
// request never quantises to zero). Used when calibrated ns values become defcal pulses;
// schedule instructions themselves are never rounded — `verify` rejects them (E_PULSE_ALIGN).
std::int64_t quantiseSamples(double seconds, Picoseconds dt, int granularity,
                             std::int64_t minSamples = 0);
Picoseconds quantise(double seconds, Picoseconds dt, int granularity, std::int64_t minSamples = 0);

// A rotating frame (f, φ) attached to a channel (spec 10 §3).
struct FrameDecl {
    std::string name;
    ChannelId channel;
    double frequencyHz = 0.0;
    double phase = 0.0;
};

// ---- instructions -------------------------------------------------------------------

struct Play {
    ChannelId ch;
    Waveform wf;
    Picoseconds t0{0};
    Picoseconds duration() const { return psFromSeconds(wf.duration); }
};

struct FrameOp {
    enum class Op { SetFrequency, ShiftFrequency, SetPhase, ShiftPhase };
    ChannelId ch;
    Picoseconds t0{0};
    Op op = Op::ShiftPhase;
    double value = 0.0; // Hz for frequency ops, rad for phase ops
};

enum class AcquireKind { Integrate, PhotonCount };

struct Acquire {
    ChannelId ch; // a[i]
    Picoseconds t0{0};
    Picoseconds length{0};
    std::string weights = "matched"; // matched | boxcar
    AcquireKind kind = AcquireKind::Integrate;
    int memorySlot = 0;
};

struct Barrier {
    std::vector<ChannelId> chs;
    Picoseconds t0{0};
};

struct Delay {
    ChannelId ch;
    Picoseconds t0{0};
    Picoseconds length{0};
};

using Instruction = std::variant<Play, FrameOp, Acquire, Barrier, Delay>;

ChannelId instructionChannel(const Instruction& i); // Barrier returns its first channel
Picoseconds instructionStart(const Instruction& i);
Picoseconds instructionDuration(const Instruction& i);
void setInstructionStart(Instruction& i, Picoseconds t);

// Placement policy for appended blocks (spec 10 §5, spec 14 §7).
enum class AlignMode { Left, Sequential, Right };

// ---- schedule -----------------------------------------------------------------------

class Schedule {
  public:
    Schedule() = default;
    explicit Schedule(Picoseconds dt) : dt_(dt) {}

    Picoseconds dt() const { return dt_; }
    void setDt(Picoseconds v) { dt_ = v; }
    const std::vector<Instruction>& instructions() const { return instrs_; }
    const std::vector<FrameDecl>& frames() const { return frames_; }
    void addFrame(FrameDecl f) { frames_.push_back(std::move(f)); }

    // Append at the current end of every channel the instruction touches (ASAP within the block).
    void append(Instruction i);
    // Place at an explicit time.
    void insert(Instruction i, Picoseconds t0);
    // Append a whole block, aligning its channels per `mode`.
    void appendBlock(const Schedule& block, AlignMode mode = AlignMode::Left);
    // Shift every instruction by `by`.
    void shift(Picoseconds by);
    // Merge another schedule at absolute times (no re-timing); frames are unioned by name.
    void merge(const Schedule& other);
    // Instructions fully inside [t0, t1), re-based to 0.
    Schedule slice(Picoseconds t0, Picoseconds t1) const;

    Picoseconds duration() const;
    Picoseconds channelEnd(ChannelId ch) const;
    std::set<ChannelId> channels() const;
    std::size_t size() const { return instrs_.size(); }
    bool empty() const { return instrs_.empty(); }

    // Align the listed channels (or all of them) to their common maximum end time.
    void barrier(std::vector<ChannelId> chs = {});

    // Spec 10 §1/§4/§5: alignment to dt·granularity, minimum pulse length, no overlap on a
    // channel, channel exists on the device. Non-fatal findings land in `warnings`.
    Result<void> verify(const hw::Device& dev, std::vector<Warning>* warnings = nullptr) const;
    // Device-independent checks (used when no device is at hand).
    Result<void> verifyTiming(int granularitySamples, int minPulseSamples,
                              std::vector<Warning>* warnings = nullptr) const;

    void sort();

  private:
    Picoseconds dt_{222};
    std::vector<Instruction> instrs_;
    std::vector<FrameDecl> frames_;
    std::map<ChannelId, Picoseconds> channelEnd_;

    void touch(const Instruction& i);
};

} // namespace qlab::pulse
