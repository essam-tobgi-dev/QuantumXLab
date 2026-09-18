#include "Pulse/Errors.hpp"
#include "Pulse/Sampling.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace qlab::pulse {
namespace {
constexpr std::int64_t kMaxSamples = std::int64_t{1} << 31;

std::int64_t ceilDiv(std::int64_t a, std::int64_t b) { return a >= 0 ? (a + b - 1) / b : -((-a) / b); }
} // namespace

const ChannelSamples* SampledSchedule::find(ChannelId ch) const {
    auto it = std::lower_bound(channels.begin(), channels.end(), ch,
                               [](const ChannelSamples& c, ChannelId id) { return c.channel < id; });
    return it != channels.end() && it->channel == ch ? &*it : nullptr;
}

Result<SampledSchedule> sampleSchedule(const Schedule& schedule, double sampleRateHz, int granularity) {
    if (!(sampleRateHz > 0.0) || !std::isfinite(sampleRateHz))
        return fail(kErrSampling, std::format("sample rate {} Hz must be positive and finite", sampleRateHz));
    if (granularity < 1) return fail(kErrSampling, std::format("granularity {} must be at least 1", granularity));
    const std::int64_t dtPs = std::llround(1e12 / sampleRateHz);
    if (dtPs < 1)
        return fail(kErrSampling, std::format("sample rate {:.3g} Hz is finer than the 1 ps time base", sampleRateHz));

    SampledSchedule out;
    out.sampleRateHz = sampleRateHz;
    out.dt = Picoseconds{dtPs};
    out.granularity = granularity;
    const std::int64_t g = granularity;
    const std::int64_t n = ceilDiv(ceilDiv(std::max<std::int64_t>(schedule.duration().value, 0), dtPs), g) * g;
    if (n > kMaxSamples)
        return fail(kErrSampling, std::format("the record needs {} samples per channel at {:.3g} Hz", n, sampleRateHz));
    out.length = static_cast<std::size_t>(n);

    const FrameTimeline frames(schedule);
    std::map<ChannelId, ChannelSamples> byChannel;
    for (auto const& instr : schedule.instructions()) {
        if (auto* p = std::get_if<Play>(&instr)) {
            const std::int64_t t0 = p->t0.value;
            const std::int64_t len = p->duration().value;
            if (len <= 0) continue;
            const std::int64_t first = std::max<std::int64_t>(ceilDiv(t0, dtPs), 0);
            const std::int64_t last = std::min<std::int64_t>(ceilDiv(t0 + len, dtPs), n);
            auto [it, created] = byChannel.try_emplace(p->ch);
            ChannelSamples& cs = it->second;
            if (created) {
                cs.channel = p->ch;
                cs.frameFrequencyHz = frames.declaredFrequencyHz(p->ch);
                cs.samples.assign(out.length, Complex{});
            }
            for (std::int64_t k = first; k < last; ++k) {
                const double tk = static_cast<double>(k * dtPs) * 1e-12;
                const Complex e = p->wf.sample(static_cast<double>(k * dtPs - t0) * 1e-12);
                cs.samples[static_cast<std::size_t>(k)] += e * std::polar(1.0, frames.basebandPhase(p->ch, tk));
            }
            if (last > first)
                cs.plays.push_back({static_cast<std::size_t>(first), static_cast<std::size_t>(last - first)});
        } else if (auto* a = std::get_if<Acquire>(&instr)) {
            const std::int64_t first = std::max<std::int64_t>(ceilDiv(a->t0.value, dtPs), 0);
            const std::int64_t last = std::min<std::int64_t>(ceilDiv(a->t0.value + a->length.value, dtPs), n);
            AcquireWindow w;
            w.channel = a->ch;
            w.span = {static_cast<std::size_t>(first), static_cast<std::size_t>(std::max<std::int64_t>(last - first, 0))};
            w.weights = a->weights;
            w.kind = a->kind;
            w.memorySlot = a->memorySlot;
            out.acquisitions.push_back(std::move(w));
        }
    }
    for (auto& [ch, cs] : byChannel) {
        (void)ch;
        std::sort(cs.plays.begin(), cs.plays.end(), [](const SampleSpan& x, const SampleSpan& y) { return x.start < y.start; });
        out.channels.push_back(std::move(cs));
    }
    std::stable_sort(out.acquisitions.begin(), out.acquisitions.end(),
                     [](const AcquireWindow& x, const AcquireWindow& y) { return x.span.start < y.span.start; });
    return out;
}

} // namespace qlab::pulse
