#pragma once
// Spec 14 §6 — pulse lowering: native gates → `pulse::Schedule` through the device's `pulses.json`
// and the program's own `cal`/`defcal` blocks (which override the device entry for their gate and
// qubit tuple). Virtual Z is a frame phase shift; cx on cross-resonance devices is the echoed CR
// sequence of the pulse library.
#include "Compiler/Schedule.hpp"
#include "Compiler/Types.hpp"
#include "Hardware/Device.hpp"
#include "IR/Circuit.hpp"
#include "Lang/Ast.hpp"
#include "Pulse/Library.hpp"
#include "Pulse/Schedule.hpp"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace qlab::compiler {

// Where the pulses of one invocation come from. The program's calibrations are read from
// `Circuit::meta()["calibrations"]` (re-emittable text with inputs bound, written by Build).
class PulseSource {
  public:
    // `library` may be null: only gates with a program defcal can then be resolved.
    static Result<PulseSource> create(const hw::Device& device, const pulse::PulseLibrary* library,
                                      const ir::Circuit& program);

    const hw::Device& device() const { return *device_; }
    const pulse::PulseLibrary* library() const { return library_; }
    Picoseconds dt() const { return dt_; }
    Picoseconds granule() const { return Picoseconds{dt_.get() * granularity_}; }
    // Nearest point of the device grid (spec 10 §1); `isPlay` applies the minimum pulse length.
    Picoseconds quantise(double seconds, bool isPlay) const;

    bool hasProgramDefcal(std::string_view gate, std::span<const std::uint32_t> qubits) const;
    // Frames declared by the program's `cal` blocks; they precede the device frames in a lowered
    // schedule, so a program frame on a channel overrides the device's declaration of it.
    const std::vector<pulse::FrameDecl>& programFrames() const { return frames_; }

    // The block of one invocation, starting at t = 0. QL4080 when neither the program nor the
    // device calibrates the gate on these qubits.
    Result<pulse::Schedule> gateBlock(const ir::Gate& g) const;
    Result<pulse::Schedule> measureBlock(std::uint32_t qubit, const SourceSpan& span = {}) const;
    Result<pulse::Schedule> resetBlock(std::uint32_t qubit, const SourceSpan& span = {}) const;

  private:
    struct ProgramDefcal {
        std::string gate;
        std::vector<std::uint32_t> qubits;
        std::vector<std::string> paramNames; // "" where the defcal fixes a literal value
        std::vector<std::optional<double>> literals;
        const lang::DefcalStmt* body = nullptr;
    };
    using Bindings = std::map<std::string, double, std::less<>>; // defcal parameter → value
    using ChannelMap = std::map<std::string, pulse::ChannelId, std::less<>>; // frame name → channel
    using WaveformMap = std::map<std::string, const lang::PulseWaveform*, std::less<>>;

    const ProgramDefcal* find(std::string_view gate, std::span<const std::uint32_t> qubits,
                              std::span<const double> params) const;
    Result<pulse::Schedule> lowerProgramDefcal(const ProgramDefcal& d,
                                               std::span<const double> params) const;
    // Registers the `extern port`, `frame` and `waveform` declarations of a cal or defcal body.
    Status declare(const std::vector<lang::PulseStmt>& body, std::vector<pulse::FrameDecl>& frames,
                   ChannelMap& channels, WaveformMap& waveforms) const;
    // Value of an expression of a calibration block; durations are seconds.
    Result<num::Complex> evaluate(const lang::Expr& e, const Bindings& params) const;
    Result<pulse::Waveform> waveform(const lang::PulseWaveform& w, const Bindings& params,
                                     const WaveformMap& local) const;
    Result<pulse::Schedule> fromLibrary(std::string_view gate,
                                        std::span<const std::uint32_t> qubits,
                                        std::span<const double> params,
                                        const SourceSpan& span) const;
    Error missing(std::string_view gate, std::span<const std::uint32_t> qubits,
                  const SourceSpan& span) const; // QL4080

    const hw::Device* device_ = nullptr;
    const pulse::PulseLibrary* library_ = nullptr;
    Picoseconds dt_{222};
    int granularity_ = 16, minPulseSamples_ = 64;
    std::shared_ptr<lang::Ast> ast_; // owns the parsed cal/defcal statements
    std::vector<ProgramDefcal> defcals_;
    std::vector<pulse::FrameDecl> frames_;
    ChannelMap frameChannel_; // frames of the program's cal blocks
    WaveformMap waveforms_;   // `waveform w = …` of the cal blocks
};

// OpenPulse port name → channel: `d0`, `d_0` → d[0]; `u01`, `u0_1` → u[0,1]; `m0`, `a0`/`acq0`,
// `f0`, `r0`, `ms0_1`. Two indices without a separator must be single digits.
Result<pulse::ChannelId> channelOfPort(std::string_view port);

// One gate-level node placed in the pulse schedule.
struct PulseWindow {
    static constexpr std::uint32_t kTopLevel = 0xFFFFFFFFu;
    std::uint32_t node = 0; // index in `topologicalOrder()` of its circuit level
    std::uint32_t parent =
        kTopLevel; // for a node of a conditional arm: top-level index of its Branch
    Picoseconds start{0}, end{0};
    bool conditional = false; // inside a Branch arm: played only when `condition` holds
    std::string condition;    // `ClassicalExpr::text()`
};

struct PulseProgram {
    pulse::Schedule schedule;
    std::vector<PulseWindow> windows;     // top-level nodes and the nodes of conditional arms
    std::vector<pulse::Warning> warnings; // non-fatal findings of `Schedule::verify` (W_DEAD_PHASE)
};

// Lowers a scheduled physical circuit: every node is placed at its start time from `timing`
// (so gate-level and pulse-level timing agree), `rz` becomes `shift_phase`, delays become channel
// delays. The pulse schedule has no branch: a Branch is lowered when its else arm is empty, its
// then arm played after the feedback latency and listed as conditional (the backend applies it on
// the recorded condition, as it does for the corrective x of an active reset); any other classical
// control is `err::Unsupported`. The result is verified against the device.
Result<PulseProgram> lowerToPulses(const ir::Circuit& c, const ScheduleInfo& timing,
                                   const PulseSource& source);

} // namespace qlab::compiler
