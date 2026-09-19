#include "Pulse/Frames.hpp"
#include <algorithm>
#include <numbers>

namespace qlab::pulse {
namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;
} // namespace

FrameTimeline::FrameTimeline(const Schedule& schedule) {
    for (auto const& f : schedule.frames()) {
        if (tracks_.contains(f.channel))
            continue; // the first declaration of a channel wins
        Track t;
        t.declaredHz = f.frequencyHz;
        t.declaredPhase = f.phase;
        t.segments.push_back({0.0, f.phase, f.frequencyHz, 0.0});
        tracks_.emplace(f.channel, std::move(t));
    }
    std::vector<const FrameOp*> ops;
    for (auto const& i : schedule.instructions())
        if (auto* fo = std::get_if<FrameOp>(&i))
            ops.push_back(fo);
    std::stable_sort(ops.begin(), ops.end(),
                     [](const FrameOp* a, const FrameOp* b) { return a->t0 < b->t0; });

    for (const FrameOp* fo : ops) {
        Track& t = tracks_[fo->ch];
        if (t.segments.empty())
            t.segments.push_back({0.0, 0.0, 0.0, 0.0}); // undeclared frame: 0 Hz, 0 rad
        Segment next = t.segments.back();
        const double tOp = secondsOf(fo->t0);
        next.offsetPhase += kTwoPi * (next.frequencyHz - t.declaredHz) * (tOp - next.t0);
        next.t0 = tOp;
        switch (fo->op) {
        case FrameOp::Op::ShiftPhase:
            next.phaseOps += fo->value;
            break;
        case FrameOp::Op::SetPhase:
            next.phaseOps = fo->value;
            break;
        case FrameOp::Op::ShiftFrequency:
            next.frequencyHz += fo->value;
            break;
        case FrameOp::Op::SetFrequency:
            next.frequencyHz = fo->value;
            break;
        }
        t.segments.push_back(next);
    }
}

const FrameTimeline::Track* FrameTimeline::track(ChannelId ch) const {
    auto it = tracks_.find(ch);
    return it == tracks_.end() ? nullptr : &it->second;
}

const FrameTimeline::Segment& FrameTimeline::segmentAt(const Track& t, double tS) {
    // The last segment starting at or before tS; ops at the same instant all apply.
    auto it = std::upper_bound(t.segments.begin() + 1, t.segments.end(), tS,
                               [](double v, const Segment& s) { return v < s.t0; });
    return *(it - 1);
}

double FrameTimeline::declaredFrequencyHz(ChannelId ch) const {
    const Track* t = track(ch);
    return t ? t->declaredHz : 0.0;
}

double FrameTimeline::declaredPhase(ChannelId ch) const {
    const Track* t = track(ch);
    return t ? t->declaredPhase : 0.0;
}

double FrameTimeline::frequencyHz(ChannelId ch, double tS) const {
    const Track* t = track(ch);
    return t ? segmentAt(*t, tS).frequencyHz : 0.0;
}

double FrameTimeline::phaseOps(ChannelId ch, double tS) const {
    const Track* t = track(ch);
    return t ? segmentAt(*t, tS).phaseOps : 0.0;
}

double FrameTimeline::offsetPhase(ChannelId ch, double tS) const {
    const Track* t = track(ch);
    if (!t)
        return 0.0;
    const Segment& s = segmentAt(*t, tS);
    return s.offsetPhase + kTwoPi * (s.frequencyHz - t->declaredHz) * (tS - s.t0);
}

std::vector<ChannelId> FrameTimeline::channels() const {
    std::vector<ChannelId> out;
    out.reserve(tracks_.size());
    for (auto const& [ch, t] : tracks_) {
        (void)t;
        out.push_back(ch);
    }
    return out;
}

} // namespace qlab::pulse
