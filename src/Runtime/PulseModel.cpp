// Spec 15 §3.5 (c), spec 11 §5 — what a pulse-level run needs besides the schedule: the time-domain
// system model bound to the schedule's envelopes, and the average power the schedule puts on each
// fridge line.
#include "Cryo/Wiring.hpp"
#include "Hardware/SystemModel.hpp"
#include "Pulse/Drives.hpp"
#include "Pulse/Sampling.hpp"
#include "Runtime/PulseModel.hpp"
#include <charconv>
#include <cmath>
#include <format>

namespace qlab::runtime {
namespace {
// Spec 10 §5: a Play amplitude is a fraction of the AWG full scale (spec 12 §3 default 0.5 V peak);
// the line sees it across Z₀ = 50 Ω.
constexpr double kAwgFullScaleV = 0.5;
constexpr double kZ0 = 50.0;

// "m[0..4]" covers m[0] … m[4]; "d[0]" covers only itself; "detect" matches by name.
bool lineCovers(std::string_view lineChannel, std::string_view channel) {
    if (lineChannel == channel) return true;
    const std::size_t open = lineChannel.find('[');
    const std::size_t dots = lineChannel.find("..");
    if (open == std::string_view::npos || dots == std::string_view::npos || lineChannel.back() != ']') return false;
    if (channel.compare(0, open, lineChannel, 0, open) != 0 || channel.size() <= open || channel[open] != '[')
        return false;
    const auto number = [](std::string_view s) -> std::optional<std::uint32_t> {
        std::uint32_t v = 0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        return r.ec == std::errc{} && r.ptr == s.data() + s.size() ? std::optional{v} : std::nullopt;
    };
    const auto lo = number(lineChannel.substr(open + 1, dots - open - 1));
    const auto hi = number(lineChannel.substr(dots + 2, lineChannel.size() - dots - 3));
    const auto q = number(channel.substr(open + 1, channel.size() - open - 2));
    return lo && hi && q && *q >= *lo && *q <= *hi;
}
} // namespace

std::uint32_t pulseSiteLevels(const hw::Device& device, const RunOptions& options) {
    const std::uint32_t floorLevels = hw::isTransmon(device.technology) ? 3u : 2u;
    return std::max(floorLevels, options.levels);
}

Result<PulseModel> buildPulseModel(const ExecutionInput& in, const ProgramPlan& plan) {
    if (!in.program || !in.program->pulses)
        return fail(err::Unsupported, "a pulse-level run needs a compiled program with a pulse schedule");
    if (!in.device || !in.calibration) return fail(err::NoDevice, "a pulse-level run needs a device and a calibration");
    static const RunOptions kDefault;
    const RunOptions& options = in.options ? *in.options : kDefault;

    hw::SystemModelOptions opt;
    opt.qubits = plan.qubits;
    opt.levels = pulseSiteLevels(*in.device, options);
    opt.includeDecoherence = options.pulseDecoherence && in.noise != nullptr;
    opt.includeThermal = opt.includeDecoherence;
    opt.includeLeakage = false; // the leakage level is in the model itself, not a collapse channel
    opt.fockCutoff = static_cast<int>(options.fockCutoff);
    // The Lindblad backend bounds the *total* dimension (D <= 243), not the per-site one, so a
    // motional mode may carry a realistic Fock cutoff. The model built below is checked against
    // that cap by `setModel`, and an over-large request is refused there, never clamped behind the
    // user's back (spec 15 §2).
    if (options.fockCutoff < 2)
        return fail(err::LindbladCap,
                    std::format("Fock cutoff {} is below the two levels a site needs", options.fockCutoff));
    QXL_TRY_ASSIGN(hw::SystemModelSpec spec, hw::buildSystemModel(*in.device, *in.calibration, opt));

    PulseModel out;
    out.siteDims = spec.siteDims;
    out.qubitSites = static_cast<std::uint32_t>(spec.qubitIndices.size());
    out.model.siteDims = spec.siteDims;
    out.model.h0 = spec.h0Dense();
    out.model.frameFrequenciesHz = spec.frameFrequenciesHz;
    for (const hw::CollapseSpec& c : spec.collapse) out.model.collapse.push_back({c.name, c.op.toDense()});

    // The schedule's envelopes, bound to the operators of the channels they name (spec 10 §8).
    QXL_TRY_ASSIGN(pulse::SystemDrives drives,
                   pulse::toSystemDrives(in.program->pulses->schedule, *in.device, /*rwa=*/true));
    out.durationS = drives.durationS;
    for (const hw::DriveSpec& d : spec.drives) {
        const pulse::DriveEnvelope* e = drives.find(d.channel);
        if (!e) continue; // no Play on this channel: the term is identically zero
        out.model.drives.push_back(qsim::DriveTerm{d.channel, d.inPhase.toDense(), d.quadrature.toDense(), e->envelope()});
        out.channels.push_back(d.channel);
    }
    if (out.model.drives.empty() && !drives.drives.empty()) {
        std::string names;
        for (const pulse::DriveEnvelope& e : drives.drives) names += (names.empty() ? "" : ", ") + e.channel;
        return fail(err::Unsupported,
                    std::format("the device model has no Hamiltonian term for the driven channels ({})", names));
    }
    out.model.durationS = drives.durationS;
    for (const pulse::ScheduleEvent& ev : drives.events)
        if (ev.kind == pulse::ScheduleEvent::Kind::Acquire) out.acquireEnd = std::max(out.acquireEnd, ev.t0S + ev.durationS);
    return out;
}

Result<cryo::LinePowers> schedulePower(const pulse::Schedule& schedule, const hw::Device& device, double repetitionS) {
    cryo::LinePowers powers;
    if (schedule.empty() || repetitionS <= 0.0) return powers;
    const double dtS = static_cast<double>(schedule.dt().get()) * 1e-12;
    if (dtS <= 0.0) return fail(err::Unsupported, "the schedule has no sample period");
    QXL_TRY_ASSIGN(const pulse::SampledSchedule sampled, pulse::sampleSchedule(schedule, 1.0 / dtS,
                                                                              device.timing.granularitySamples));
    std::map<std::string, double, std::less<>> byChannel;
    for (const pulse::ChannelSamples& ch : sampled.channels) {
        double energy = 0.0; // ∫ (A·V_fs·|e|)²/(2 Z₀) dt over the plays of this channel
        for (const num::Complex& s : ch.samples)
            energy += std::norm(s) * kAwgFullScaleV * kAwgFullScaleV / (2.0 * kZ0) * dtS;
        if (energy > 0.0) byChannel[ch.channel.toString()] += energy / repetitionS;
    }
    // Map the channels onto the fridge lines when the device ships a wiring file (spec 11 §9).
    const std::filesystem::path wiringPath = device.directory / "wiring.json";
    if (!device.directory.empty() && std::filesystem::exists(wiringPath)) {
        QXL_TRY_ASSIGN(const cryo::Wiring wiring, cryo::loadWiring(wiringPath));
        for (const auto& [channel, watts] : byChannel) {
            const cryo::WiringLine* hit = nullptr;
            for (const cryo::WiringLine& line : wiring.lines)
                if (lineCovers(line.channel, channel)) { hit = &line; break; }
            powers[hit ? hit->id : channel] += watts;
        }
        return powers;
    }
    for (const auto& [channel, watts] : byChannel) powers[channel] += watts;
    return powers;
}

} // namespace qlab::runtime
