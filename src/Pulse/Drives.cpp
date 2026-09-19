#include "Pulse/Drives.hpp"
#include "Pulse/Errors.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <numbers>

namespace qlab::pulse {
namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

Result<double> qubitFrequency(const PulseLibrary& lib, std::uint32_t q) {
    const hw::QubitCal* qc = lib.calibration().qubit(q);
    if (!qc)
        return fail(kErrDrive, std::format("no calibration for qubit {}", q));
    return qc->f01.value.v;
}

Result<void> initEnvelope(DriveEnvelope& d, ChannelId ch, const PulseLibrary& lib,
                          const DriveOptions& opt, std::size_t n,
                          const std::shared_ptr<const FrameTimeline>& frames) {
    d.channel = ch.toString();
    d.id = ch;
    d.site = ch.a;
    d.dtS = secondsOf(lib.dt());
    d.held.assign(n, Complex{});
    d.rwa = opt.rwa;
    d.frames = frames;
    d.channelFrequencyHz = frames->declaredFrequencyHz(ch);
    switch (ch.kind) {
    case ChannelKind::Flux:
        d.kind = DriveKind::Flux;
        return {};
    case ChannelKind::Bichromatic: {
        d.kind = DriveKind::SpinDependentForce;
        d.target = ch.b;
        QXL_TRY_ASSIGN(const MsParams p, lib.msParams(ch.a, ch.b, kPi / 2.0));
        d.scale = p.omegaMaxRadPerS;
        d.etaI = p.etaI;
        d.etaJ = p.etaJ;
        d.modeFrequencyHz = p.modeFrequencyHz;
        break;
    }
    case ChannelKind::Control:
        d.target = ch.b;
        [[fallthrough]];
    default:
        d.kind = DriveKind::Rabi;
        QXL_TRY_ASSIGN(d.scale, lib.rabiScale(ch));
        break;
    }
    QXL_TRY_ASSIGN(d.qubitFrequencyHz, qubitFrequency(lib, ch.a));
    d.referenceFrequencyHz = opt.rwa ? opt.sharedFrameHz.value_or(d.qubitFrequencyHz) : 0.0;
    return {};
}

std::optional<ScheduleEvent::Kind> eventKind(ChannelKind k) {
    switch (k) {
    case ChannelKind::Measure:
        return ScheduleEvent::Kind::ReadoutTone;
    case ChannelKind::Detect:
        return ScheduleEvent::Kind::Detect;
    case ChannelKind::Pump:
        return ScheduleEvent::Kind::Pump;
    case ChannelKind::Acquire:
        return ScheduleEvent::Kind::Acquire;
    default:
        return std::nullopt;
    }
}
} // namespace

double DriveEnvelope::carrierPhase(double tS) const {
    const double offset = frames ? frames->offsetPhase(id, tS) : 0.0;
    return offset + kTwoPi * channelFrequencyHz * tS;
}

Complex DriveEnvelope::at(double tS) const {
    if (held.empty() || !(dtS > 0.0) || !(tS >= 0.0))
        return {};
    const auto k = static_cast<std::size_t>(std::floor(tS / dtS));
    if (k >= held.size())
        return {};
    const Complex h = held[k];
    if (h == Complex{})
        return {};
    const double offset = frames ? frames->offsetPhase(id, tS) : 0.0;
    switch (kind) {
    case DriveKind::Flux:
        return h;
    case DriveKind::Rabi:
        if (rwa)
            return h *
                   std::polar(
                       1.0, -(offset + kTwoPi * (channelFrequencyHz - referenceFrequencyHz) * tS));
        return {2.0 * (h * std::polar(1.0, -(offset + kTwoPi * channelFrequencyHz * tS))).real(),
                0.0};
    case DriveKind::SpinDependentForce: {
        const Complex force = 2.0 * h * std::cos(offset + kTwoPi * channelFrequencyHz * tS);
        if (rwa)
            return force;
        return {2.0 * (force * std::polar(1.0, -kTwoPi * qubitFrequencyHz * tS)).real(), 0.0};
    }
    }
    return {};
}

std::function<Complex(double)> DriveEnvelope::envelope() const {
    return [self = *this](double tS) { return self.at(tS); };
}

const DriveEnvelope* SystemDrives::find(std::string_view channel) const {
    auto it = std::find_if(drives.begin(), drives.end(),
                           [&](const DriveEnvelope& d) { return d.channel == channel; });
    return it == drives.end() ? nullptr : &*it;
}

Result<SystemDrives> toSystemDrives(const Schedule& schedule, const PulseLibrary& lib,
                                    const DriveOptions& opt) {
    if (schedule.dt() != lib.dt())
        return fail(kErrDrive,
                    std::format("schedule dt {} ps differs from the device dt {} ps of '{}'",
                                schedule.dt().value, lib.dt().value, lib.deviceId()));
    SystemDrives out;
    out.dtS = secondsOf(schedule.dt());
    out.durationS = secondsOf(schedule.duration());
    out.rwa = opt.rwa;
    const std::int64_t dtPs = schedule.dt().value;
    const std::int64_t duration = std::max<std::int64_t>(schedule.duration().value, 0);
    const auto n = static_cast<std::size_t>((duration + dtPs - 1) / dtPs);
    const auto frames = std::make_shared<const FrameTimeline>(schedule);

    std::map<ChannelId, DriveEnvelope> byChannel;
    for (auto const& instr : schedule.instructions()) {
        if (auto* a = std::get_if<Acquire>(&instr)) {
            out.events.push_back({ScheduleEvent::Kind::Acquire, a->ch, a->ch.a, secondsOf(a->t0),
                                  secondsOf(a->length), a->kind, a->weights, a->memorySlot});
            continue;
        }
        auto* p = std::get_if<Play>(&instr);
        if (!p)
            continue;
        if (auto ev = eventKind(p->ch.kind)) {
            out.events.push_back({*ev, p->ch, p->ch.a, secondsOf(p->t0), secondsOf(p->duration()),
                                  AcquireKind::Integrate, std::string{}, 0});
            continue;
        }
        if (p->ch.kind == ChannelKind::GlobalRaman)
            return fail(
                kErrDrive,
                "the global Raman beam g has no calibrated Rabi rate; address ions with r[i]");

        auto [it, created] = byChannel.try_emplace(p->ch);
        DriveEnvelope& d = it->second;
        if (created)
            QXL_TRY(initEnvelope(d, p->ch, lib, opt, n, frames));
        const std::int64_t t0 = p->t0.value;
        const std::int64_t first = std::max<std::int64_t>((t0 + dtPs - 1) / dtPs, 0);
        const std::int64_t last = std::min<std::int64_t>(
            (t0 + p->duration().value + dtPs - 1) / dtPs, static_cast<std::int64_t>(n));
        if (d.kind == DriveKind::SpinDependentForce && created)
            d.detuningRadPerS =
                kTwoPi * (d.modeFrequencyHz - frames->frequencyHz(p->ch, secondsOf(p->t0)));
        for (std::int64_t k = first; k < last; ++k) {
            const double tk = static_cast<double>(k * dtPs) * 1e-12;
            const Complex e = p->wf.sample(static_cast<double>(k * dtPs - t0) * 1e-12);
            const double phase = d.kind == DriveKind::Flux ? 0.0 : frames->phaseOps(p->ch, tk);
            d.held[static_cast<std::size_t>(k)] += d.scale * e * std::polar(1.0, phase);
        }
    }
    for (auto& [ch, d] : byChannel) {
        (void)ch;
        out.drives.push_back(std::move(d));
    }
    std::stable_sort(out.events.begin(), out.events.end(),
                     [](const ScheduleEvent& x, const ScheduleEvent& y) { return x.t0S < y.t0S; });
    return out;
}

Result<SystemDrives> toSystemDrives(const Schedule& schedule, const hw::Device& device, bool rwa) {
    if (device.directory.empty())
        return fail(
            kErrDrive,
            std::format("device '{}' was not loaded from a directory; pass its PulseLibrary",
                        device.id));
    QXL_TRY_ASSIGN(const PulseLibrary lib, loadPulses(device.directory));
    DriveOptions opt;
    opt.rwa = rwa;
    return toSystemDrives(schedule, lib, opt);
}

} // namespace qlab::pulse
