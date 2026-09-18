// Spec 10 §6 — loading `pulses.json` and resolving it against the device calibration.
#include "Hardware/Loader.hpp"
#include "Pulse/Errors.hpp"
#include "Pulse/Library.hpp"
#include <algorithm>
#include <format>
#include <numbers>

namespace qlab::pulse {
namespace {
constexpr double kPi = std::numbers::pi;

Result<std::vector<std::uint32_t>> parseQubits(const core::Json& j) {
    if (!j.is_array()) return fail(kErrLibrary, "defcal 'qubits' must be an array");
    std::vector<std::uint32_t> out;
    for (auto const& q : j) {
        if (!q.is_number_unsigned()) return fail(kErrLibrary, "defcal qubit index must be an integer");
        out.push_back(q.get<std::uint32_t>());
    }
    return out;
}

bool isSymmetricTwoQubitGate(std::string_view g) { return g == "ms" || g == "cz" || g == "siswap"; }
} // namespace

std::string DefcalKey::toString() const {
    std::string s(gate);
    s += '(';
    for (std::size_t i = 0; i < qubits.size(); ++i) s += (i ? "," : "") + std::to_string(qubits[i]);
    s += ')';
    return s;
}

// ---- construction ---------------------------------------------------------------------

Result<PulseLibrary> PulseLibrary::fromJson(core::Json pulses, hw::Device device, hw::Calibration calibration) {
    PulseLibrary lib;
    lib.device_ = std::move(device);
    lib.calibration_ = std::move(calibration);
    lib.deviceId_ = pulses.value("device", "");
    if (lib.deviceId_.empty()) return fail(kErrLibrary, "pulses.json: missing field 'device'");
    if (lib.deviceId_ != lib.device_.id)
        return fail(kErrLibrary, std::format("pulses.json is for '{}' but the device is '{}'", lib.deviceId_,
                                             lib.device_.id));

    // Timing comes from device.json (spec 10 §1).
    lib.dt_ = Picoseconds{lib.device_.timing.dtPs};
    lib.granularity_ = lib.device_.timing.granularitySamples;
    lib.minPulseSamples_ = lib.device_.timing.minPulseSamples;
    if (lib.dt_.value <= 0) return fail(kErrLibrary, "device timing.dt_ps must be positive");
    if (pulses.contains("templates") && pulses["templates"].is_object()) lib.templates_ = pulses["templates"];

    // frames: id → { frequency_ghz (number or cal.* expression), channel }
    const auto res = lib.resolver();
    if (pulses.contains("frames")) {
        for (auto const& [name, f] : pulses["frames"].items()) {
            FrameDecl d;
            d.name = name;
            const std::string chName = f.value("channel", std::string{});
            if (chName.empty()) return fail(kErrLibrary, std::format("pulses.json: missing field frames.{}.channel", name));
            QXL_TRY_ASSIGN(d.channel, parseChannel(chName));
            if (f.contains("frequency_ghz")) {
                auto ghz = evalJsonValue(f["frequency_ghz"], {}, res);
                if (!ghz) return fail(ghz.error().withNote(std::format("in frames.{}.frequency_ghz", name)));
                d.frequencyHz = *ghz * 1e9;
            }
            d.phase = f.value("phase_rad", 0.0);
            lib.frames_.emplace(name, std::move(d));
        }
    }

    if (!pulses.contains("defcals") || !pulses["defcals"].is_array())
        return fail(kErrLibrary, "pulses.json: missing field 'defcals' (array)");
    for (auto const& dj : pulses["defcals"]) {
        Defcal d;
        d.key.gate = dj.value("gate", std::string{});
        if (d.key.gate.empty()) return fail(kErrLibrary, "pulses.json: defcal without field 'gate'");
        if (!dj.contains("qubits"))
            return fail(kErrLibrary, std::format("pulses.json: defcal '{}' has no field 'qubits'", d.key.gate));
        QXL_TRY_ASSIGN(d.key.qubits, parseQubits(dj["qubits"]));
        if (dj.contains("params"))
            for (auto const& p : dj["params"]) d.params.push_back(p.get<std::string>());
        if (dj.contains("instructions")) d.instructions = dj["instructions"];
        d.ref = dj.value("ref", std::string{});
        d.paramsFrom = dj.value("params_from", std::string{});
        d.extra = dj;
        if (d.instructions.is_null() && d.ref.empty())
            return fail(kErrLibrary, std::format("defcal {} has neither 'instructions' nor 'ref'", d.key.toString()));
        lib.defcals_.emplace(d.key, std::move(d));
    }
    lib.pulsesJson_ = std::move(pulses);
    QXL_TRY(lib.prepareMsShapes());
    QXL_TRY(lib.checkComplete());
    return lib;
}

Result<PulseLibrary> PulseLibrary::load(const std::filesystem::path& dir) {
    QXL_TRY_ASSIGN(auto pulses, core::JsonEnvelope::load(dir / "pulses.json", "pulses"));
    QXL_TRY_ASSIGN(auto loaded, hw::loadDevice(dir));
    QXL_TRY_ASSIGN(auto lib, fromJson(std::move(pulses.data), std::move(loaded.device), std::move(loaded.calibration)));
    QXL_TRY(lib.checkMsTable());
    return lib;
}

Result<PulseLibrary> PulseLibrary::withCalibration(hw::Calibration calibration) const {
    return fromJson(pulsesJson_, device_, std::move(calibration));
}

Result<PulseLibrary> loadPulses(const std::filesystem::path& deviceDir) { return PulseLibrary::load(deviceDir); }

// ---- lookup ---------------------------------------------------------------------------

std::vector<DefcalKey> PulseLibrary::keys() const {
    std::vector<DefcalKey> out;
    out.reserve(defcals_.size());
    for (auto const& [k, d] : defcals_) { (void)d; out.push_back(k); }
    return out;
}

const Defcal* PulseLibrary::find(std::string_view gate, std::span<const std::uint32_t> qubits) const {
    DefcalKey key{std::string(gate), {qubits.begin(), qubits.end()}};
    if (auto it = defcals_.find(key); it != defcals_.end()) return &it->second;
    if (qubits.size() == 2 && isSymmetricTwoQubitGate(gate)) {
        std::swap(key.qubits[0], key.qubits[1]);
        if (auto it = defcals_.find(key); it != defcals_.end()) return &it->second;
    }
    return nullptr;
}

bool PulseLibrary::has(std::string_view gate, std::span<const std::uint32_t> qubits) const {
    return find(gate, qubits) != nullptr;
}

const FrameDecl* PulseLibrary::frameFor(ChannelId ch) const {
    for (auto const& [name, f] : frames_) {
        (void)name;
        if (f.channel == ch) return &f;
    }
    return nullptr;
}

PathResolver PulseLibrary::resolver() const { return makeCalibrationResolver(device_, calibration_); }

Result<double> PulseLibrary::path(std::string_view p) const { return resolver()(p); }

Picoseconds PulseLibrary::quantise(double seconds, bool isPlay) const {
    return pulse::quantise(seconds, dt_, granularity_, isPlay ? minPulseSamples_ : 0);
}

Schedule PulseLibrary::emptySchedule() const { return Schedule(dt_); }

// Every channel the schedule touches gets the frame `pulses.json` declared for it (spec 10 §3),
// so that sampling and the Lindblad adapter of §8 know each channel's carrier without the library.
void PulseLibrary::attachFrames(Schedule& s) const {
    const auto used = s.channels();
    for (auto const& [name, f] : frames_) {
        (void)name;
        const bool present = std::any_of(s.frames().begin(), s.frames().end(),
                                         [&](const FrameDecl& d) { return d.name == f.name; });
        if (used.contains(f.channel) && !present) s.addFrame(f);
    }
}

// ---- amplitude → Rabi rate (spec 10 §7) -----------------------------------------------

Result<double> PulseLibrary::driveCoupling(std::uint32_t qubit) const {
    // hw::Calibration carries no measured line coupling, so invert the area theorem
    // θ = κ_d · A · area(e) on a defcal whose rotation angle is fixed by its name.
    struct Known { const char* gate; double theta; };
    static constexpr Known kKnown[] = {{"sx", kPi / 2.0}, {"x", kPi}, {"rx", kPi / 2.0}, {"ry", kPi / 2.0}};
    for (auto const& k : kKnown) {
        auto wf = singleQubitWaveform(k.gate, qubit);
        if (!wf) continue;
        const double area = std::abs(wf->area());
        if (area <= 0.0) continue;
        return k.theta / area;
    }
    return fail(kErrNoDefcal, std::format("no defcal on qubit {} fixes the drive coupling (need sx, x, rx or ry)", qubit));
}

Result<double> PulseLibrary::rabiScale(ChannelId ch) const {
    switch (ch.kind) {
    case ChannelKind::Drive:
    case ChannelKind::Raman:
    case ChannelKind::Control: return driveCoupling(ch.a);
    case ChannelKind::Bichromatic: {
        QXL_TRY_ASSIGN(const MsParams p, msParams(ch.a, ch.b, kPi / 2.0));
        return p.omegaMaxRadPerS;
    }
    default:
        return fail(kErrDrive, std::format("channel {} has no calibrated Rabi scale", ch.toString()));
    }
}

// ---- dispatch -------------------------------------------------------------------------

Result<Schedule> PulseLibrary::scheduleFor(std::string_view gate, std::span<const std::uint32_t> qubits,
                                           const ParamMap& params) const {
    const Defcal* d = find(gate, qubits);
    if (!d) {
        DefcalKey key{std::string(gate), {qubits.begin(), qubits.end()}};
        return fail(Error(kErrNoDefcal, std::format("device '{}' has no defcal for {}", deviceId_, key.toString()))
                        .withId("E_NO_DEFCAL"));
    }
    for (auto const& p : d->params)
        if (!params.contains(p))
            return fail(kErrNoDefcal, std::format("defcal {} needs parameter '{}'", d->key.toString(), p));

    Result<Schedule> s = d->ref.empty() ? buildFromInstructions(*d, params) : buildFromTemplate(*d, params);
    if (!s) return s;
    s->sort();
    attachFrames(*s);
    return s;
}

Result<Schedule> PulseLibrary::scheduleFor(std::string_view gate, std::initializer_list<std::uint32_t> qubits,
                                           const ParamMap& params) const {
    return scheduleFor(gate, std::span<const std::uint32_t>(qubits.begin(), qubits.size()), params);
}

} // namespace qlab::pulse
