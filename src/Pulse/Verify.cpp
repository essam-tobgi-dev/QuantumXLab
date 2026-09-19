// Spec 10 §1, §4, §5, §10 — schedule verification against the device timing constraints.
#include "Hardware/Device.hpp"
#include "Pulse/Schedule.hpp"
#include <algorithm>
#include <format>
#include <map>

namespace qlab::pulse {
namespace {

// Spec 10 §4: a channel exists when the device has the line behind it (spec 11 §9 wiring,
// generated from the same device model): drive and readout lines serve data qubits, flux lines
// every qubit and coupler of a tunable device, CR channels an edge of the coupling graph (they
// share the control's drive line), MS channels any pair of distinct ions.
bool channelExists(const hw::Device& dev, ChannelId ch) {
    const auto n = static_cast<std::uint32_t>(dev.qubitCount());
    const auto data = [&](std::uint32_t q) {
        return q < n && dev.qubits[q].kind == hw::QubitKind::Data;
    };
    switch (ch.kind) {
    case ChannelKind::Drive:
    case ChannelKind::Measure:
    case ChannelKind::Acquire:
    case ChannelKind::Raman:
        return data(ch.a);
    case ChannelKind::Flux:
        return ch.a < n;
    case ChannelKind::Control:
        return data(ch.a) && data(ch.b) && dev.adjacent(ch.a, ch.b);
    case ChannelKind::Bichromatic:
        return data(ch.a) && data(ch.b) && ch.a != ch.b;
    case ChannelKind::GlobalRaman:
    case ChannelKind::Detect:
    case ChannelKind::Pump:
        return dev.technology == hw::Technology::IonChain;
    }
    return false;
}

// Channels that are legal on this technology at all.
bool channelKindSupported(const hw::Device& dev, ChannelKind k) {
    const bool ion = dev.technology == hw::Technology::IonChain;
    switch (k) {
    case ChannelKind::Drive:
    case ChannelKind::Control:
    case ChannelKind::Measure:
        return !ion;
    case ChannelKind::Flux:
        return dev.technology == hw::Technology::TransmonTunable ||
               dev.technology == hw::Technology::TransmonTunableCoupler;
    case ChannelKind::Acquire:
        return true;
    case ChannelKind::GlobalRaman:
    case ChannelKind::Raman:
    case ChannelKind::Bichromatic:
    case ChannelKind::Detect:
    case ChannelKind::Pump:
        return ion;
    }
    return false;
}

} // namespace

Result<void> Schedule::verifyTiming(int granularitySamples, int minPulseSamples,
                                    std::vector<Warning>* warnings) const {
    const std::int64_t grain = dt_.value * std::max(1, granularitySamples);
    const std::int64_t minLen = dt_.value * std::max(1, minPulseSamples);

    // Alignment and minimum length.
    for (auto const& i : instrs_) {
        const auto start = instructionStart(i).value;
        const auto len = instructionDuration(i).value;
        const auto ch = instructionChannel(i);
        if (start % grain != 0)
            return fail(
                Error(kErrAlign,
                      std::format("pulse on {} starts at {} ps, not a multiple of {} ps "
                                  "(dt {} ps x granularity {})",
                                  ch.toString(), start, grain, dt_.value, granularitySamples))
                    .withId("E_PULSE_ALIGN"));
        if (len == 0)
            continue;
        if (len % grain != 0)
            return fail(
                Error(kErrAlign, std::format("duration {} ps on {} is not a multiple of {} ps", len,
                                             ch.toString(), grain))
                    .withId("E_PULSE_ALIGN"));
        if (std::holds_alternative<Play>(i) && len < minLen)
            return fail(
                Error(kErrMin, std::format("pulse on {} is {} ps, shorter than the {}-sample "
                                           "minimum ({} ps)",
                                           ch.toString(), len, minPulseSamples, minLen))
                    .withId("E_PULSE_MIN"));
    }

    // Overlap: two Plays on one channel may not intersect.
    std::map<ChannelId, std::vector<std::pair<std::int64_t, std::int64_t>>> spans;
    for (auto const& i : instrs_) {
        if (!std::holds_alternative<Play>(i))
            continue;
        const auto s = instructionStart(i).value;
        spans[instructionChannel(i)].emplace_back(s, s + instructionDuration(i).value);
    }
    for (auto& [ch, v] : spans) {
        std::sort(v.begin(), v.end());
        for (std::size_t k = 1; k < v.size(); ++k)
            if (v[k].first < v[k - 1].second)
                return fail(
                    Error(kErrOverlap,
                          std::format("pulses on {} overlap: [{}, {}) and [{}, {})", ch.toString(),
                                      v[k - 1].first, v[k - 1].second, v[k].first, v[k].second))
                        .withId("E_PULSE_OVERLAP"));
    }

    // Dead phase: a phase shift with no later pulse on that channel (legal, reported).
    if (warnings) {
        for (auto const& i : instrs_) {
            auto* fo = std::get_if<FrameOp>(&i);
            if (!fo || (fo->op != FrameOp::Op::ShiftPhase && fo->op != FrameOp::Op::SetPhase))
                continue;
            const bool hasLater =
                std::any_of(instrs_.begin(), instrs_.end(), [&](const Instruction& j) {
                    return std::holds_alternative<Play>(j) && instructionChannel(j) == fo->ch &&
                           instructionStart(j) >= fo->t0;
                });
            if (!hasLater)
                warnings->push_back(
                    {"W_DEAD_PHASE",
                     std::format("phase shift on {} at {} ps has no subsequent pulse",
                                 fo->ch.toString(), fo->t0.value)});
        }
    }
    return {};
}

Result<void> Schedule::verify(const hw::Device& dev, std::vector<Warning>* warnings) const {
    for (auto ch : channels()) {
        if (!channelKindSupported(dev, ch.kind))
            return fail(
                Error(kErrNoChannel, std::format("{} channels do not exist on a {} device",
                                                 ch.toString(), hw::technologyName(dev.technology)))
                    .withId("E_NO_CHANNEL"));
        if (!channelExists(dev, ch))
            return fail(Error(kErrNoChannel, std::format("channel {} is not present on device '{}'",
                                                         ch.toString(), dev.id))
                            .withId("E_NO_CHANNEL"));
    }
    if (dt_.value != dev.timing.dtPs)
        return fail(Error(kErrAlign, std::format("schedule dt {} ps does not match device dt {} ps",
                                                 dt_.value, dev.timing.dtPs))
                        .withId("E_PULSE_ALIGN"));
    QXL_TRY(verifyTiming(dev.timing.granularitySamples, dev.timing.minPulseSamples, warnings));

    // Flux bandwidth: a flux pulse must not carry significant energy above the line cutoff
    // (spec 10 §6.4 / spec 11 §4.3). Checked from the sampled spectrum.
    for (auto const& i : instrs_) {
        auto* p = std::get_if<Play>(&i);
        if (!p || p->ch.kind != ChannelKind::Flux)
            continue;
        const double cutoff = 1e9; // fast-flux low-pass, spec 11 §4.3
        auto sp = p->wf.spectrum(dt_.value);
        double total = 0.0, above = 0.0;
        for (std::size_t k = 0; k < sp.magnitude.size(); ++k) {
            const double e = sp.magnitude[k] * sp.magnitude[k];
            total += e;
            if (std::abs(sp.freqHz[k]) > cutoff)
                above += e;
        }
        if (total > 0.0 && above / total > 0.01)
            return fail(Error(kErrFluxBw,
                              std::format("flux pulse on {} has {:.1f}% of its energy above the "
                                          "{:.0f} MHz line cutoff",
                                          p->ch.toString(), 100.0 * above / total, cutoff / 1e6))
                            .withId("E_FLUX_BW"));
    }
    return {};
}

} // namespace qlab::pulse
