#include "Pulse/Schedule.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::pulse {

std::int64_t quantiseSamples(double seconds, Picoseconds dt, int granularity,
                             std::int64_t minSamples) {
    if (dt.value <= 0 || !(seconds > 0.0)) return 0;
    const std::int64_t g = std::max(1, granularity);
    const double samples = seconds * 1e12 / static_cast<double>(dt.value);
    std::int64_t granules = std::llround(samples / static_cast<double>(g));
    if (seconds > 0.0 && granules < 1) granules = 1;
    std::int64_t n = granules * g;
    if (n < minSamples) n = ((minSamples + g - 1) / g) * g;
    return n;
}

Picoseconds quantise(double seconds, Picoseconds dt, int granularity, std::int64_t minSamples) {
    return Picoseconds{quantiseSamples(seconds, dt, granularity, minSamples) * dt.value};
}

ChannelId instructionChannel(const Instruction& i) {
    return std::visit(
        [](auto const& v) -> ChannelId {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Barrier>)
                return v.chs.empty() ? ChannelId{} : v.chs.front();
            else
                return v.ch;
        },
        i);
}

Picoseconds instructionStart(const Instruction& i) {
    return std::visit([](auto const& v) { return v.t0; }, i);
}

Picoseconds instructionDuration(const Instruction& i) {
    return std::visit(
        [](auto const& v) -> Picoseconds {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Play>) return v.duration();
            else if constexpr (std::is_same_v<T, Acquire>) return v.length;
            else if constexpr (std::is_same_v<T, Delay>) return v.length;
            else return Picoseconds{0};
        },
        i);
}

void setInstructionStart(Instruction& i, Picoseconds t) {
    std::visit([t](auto& v) { v.t0 = t; }, i);
}

namespace {
std::vector<ChannelId> touchedChannels(const Instruction& i) {
    if (auto* b = std::get_if<Barrier>(&i)) return b->chs;
    return {instructionChannel(i)};
}
} // namespace

void Schedule::touch(const Instruction& i) {
    const Picoseconds end = instructionStart(i) + instructionDuration(i);
    for (auto ch : touchedChannels(i)) {
        auto& e = channelEnd_[ch];
        if (end > e) e = end;
    }
}

void Schedule::append(Instruction i) {
    Picoseconds t0{0};
    for (auto ch : touchedChannels(i)) {
        auto it = channelEnd_.find(ch);
        if (it != channelEnd_.end() && it->second > t0) t0 = it->second;
    }
    setInstructionStart(i, t0);
    touch(i);
    instrs_.push_back(std::move(i));
}

void Schedule::insert(Instruction i, Picoseconds t0) {
    setInstructionStart(i, t0);
    touch(i);
    instrs_.push_back(std::move(i));
    sort();
}

void Schedule::shift(Picoseconds by) {
    for (auto& i : instrs_) setInstructionStart(i, instructionStart(i) + by);
    for (auto& [ch, e] : channelEnd_) e = e + by;
}

void Schedule::merge(const Schedule& other) {
    for (auto const& i : other.instrs_) {
        instrs_.push_back(i);
        touch(i);
    }
    for (auto const& f : other.frames_) {
        auto it = std::find_if(frames_.begin(), frames_.end(),
                               [&](const FrameDecl& d) { return d.name == f.name; });
        if (it == frames_.end()) frames_.push_back(f);
    }
    sort();
}

void Schedule::appendBlock(const Schedule& block, AlignMode mode) {
    if (block.empty()) return;
    Picoseconds base{0};
    if (mode == AlignMode::Sequential) {
        base = duration();
    } else {
        // Left/Right: start after the latest end among the channels the block touches.
        for (auto ch : block.channels()) {
            auto it = channelEnd_.find(ch);
            if (it != channelEnd_.end() && it->second > base) base = it->second;
        }
    }
    Schedule shifted = block;
    shifted.shift(base);
    if (mode == AlignMode::Right) {
        // Push each channel's instructions as late as the block allows (ALAP inside the block).
        const Picoseconds blockEnd = base + block.duration();
        std::map<ChannelId, Picoseconds> lag;
        for (auto ch : block.channels()) lag[ch] = blockEnd - (base + block.channelEnd(ch));
        for (auto& i : shifted.instrs_) {
            auto ch = instructionChannel(i);
            auto it = lag.find(ch);
            if (it != lag.end()) setInstructionStart(i, instructionStart(i) + it->second);
        }
        shifted.channelEnd_.clear();
        for (auto const& i : shifted.instrs_) shifted.touch(i);
    }
    merge(shifted);
}

Schedule Schedule::slice(Picoseconds t0, Picoseconds t1) const {
    Schedule out(dt_);
    out.frames_ = frames_;
    for (auto const& i : instrs_) {
        const Picoseconds s = instructionStart(i);
        const Picoseconds e = s + instructionDuration(i);
        if (s < t0 || e > t1) continue;
        Instruction copy = i;
        setInstructionStart(copy, s - t0);
        out.instrs_.push_back(copy);
        out.touch(copy);
    }
    out.sort();
    return out;
}

Picoseconds Schedule::duration() const {
    Picoseconds m{0};
    for (auto const& [ch, e] : channelEnd_) {
        (void)ch;
        if (e > m) m = e;
    }
    return m;
}

Picoseconds Schedule::channelEnd(ChannelId ch) const {
    auto it = channelEnd_.find(ch);
    return it == channelEnd_.end() ? Picoseconds{0} : it->second;
}

std::set<ChannelId> Schedule::channels() const {
    std::set<ChannelId> out;
    for (auto const& i : instrs_)
        for (auto ch : touchedChannels(i)) out.insert(ch);
    return out;
}

void Schedule::barrier(std::vector<ChannelId> chs) {
    if (chs.empty()) {
        auto all = channels();
        chs.assign(all.begin(), all.end());
    }
    if (chs.empty()) return;
    Picoseconds t{0};
    for (auto ch : chs) t = std::max(t, channelEnd(ch));
    Barrier b;
    b.chs = chs;
    b.t0 = t;
    for (auto ch : chs) channelEnd_[ch] = t;
    instrs_.emplace_back(std::move(b));
    sort();
}

void Schedule::sort() {
    std::stable_sort(instrs_.begin(), instrs_.end(), [](const Instruction& a, const Instruction& b) {
        return instructionStart(a) < instructionStart(b);
    });
}

} // namespace qlab::pulse
