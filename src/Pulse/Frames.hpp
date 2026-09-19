#pragma once
// Spec 10 §3, §8.4; spec 14 §6 — frame bookkeeping over a schedule: the phase and frequency of
// every channel's frame as a function of time. A frame op at time t_op acts on every sample at or
// after t_op (zero duration). Envelope convention (T05 (7.1), T07 (1.2)): a sample s with frame
// phase φ drives the qubit about the equatorial axis at angle arg(s) + φ, which is what makes
// spec 10 §3's virtual Rz(θ) = shift_phase(−θ) exact; a frame at frequency f seen from a frame at
// f_ref rotates as e^{−i2π(f − f_ref)t}.
#include "Pulse/Schedule.hpp"
#include <map>
#include <vector>

namespace qlab::pulse {

class FrameTimeline {
  public:
    explicit FrameTimeline(const Schedule& schedule);

    // Carrier the schedule declares for the channel (its FrameDecl); 0 when none is declared.
    double declaredFrequencyHz(ChannelId ch) const;
    double declaredPhase(ChannelId ch) const;
    // Instantaneous frame frequency: the declared value changed by set/shift_frequency.
    double frequencyHz(ChannelId ch, double tS) const;
    // Accumulated phase instructions (declared phase + set/shift_phase): the virtual-Z phase the
    // pulse viewer shows per gate (spec 14 §6).
    double phaseOps(ChannelId ch, double tS) const;
    // 2π∫₀ᵗ (f(t') − f_declared) dt': the phase a frequency change has accumulated (0 without one).
    double offsetPhase(ChannelId ch, double tS) const;
    // Phase every baseband sample of the channel carries relative to its declared frame:
    // phaseOps − offsetPhase.
    double basebandPhase(ChannelId ch, double tS) const {
        return phaseOps(ch, tS) - offsetPhase(ch, tS);
    }
    // Channels with a declared frame or at least one frame op.
    std::vector<ChannelId> channels() const;

  private:
    struct Segment {
        double t0 = 0.0;          // start of the segment [s]
        double phaseOps = 0.0;    // accumulated phase instructions on the segment
        double frequencyHz = 0.0; // frame frequency on the segment
        double offsetPhase = 0.0; // 2π∫₀^{t0} (f − f_declared) dt'
    };
    struct Track {
        double declaredHz = 0.0;
        double declaredPhase = 0.0;
        std::vector<Segment> segments; // segments[0] starts at −∞ (t0 is ignored)
    };
    const Track* track(ChannelId ch) const;
    static const Segment& segmentAt(const Track& t, double tS);
    std::map<ChannelId, Track> tracks_;
};

} // namespace qlab::pulse
