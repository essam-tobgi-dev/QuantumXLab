#include "Instruments/DcSource.hpp"
#include "Instruments/Awg.hpp" // parsePortIndex
#include "Units/Units.hpp"
#include <cmath>
#include <format>

namespace qlab::instr {
namespace {
// Below this E_J/E_C the 0-1 transition is no longer a transmon line: the charge dispersion of
// T05 (3.5), ε₁ ≈ 0.023 E_C at E_J/E_C = 20 (≈ 4 MHz for E_C/h = 190 MHz) and rising steeply as
// E_J falls, smears the transition far beyond a spectroscopy linewidth, so flux points that close
// to Φ0/2 of a symmetric SQUID show no qubit line at all (T05 §3.5, §4).
constexpr double kTransmonRegimeMin = 20.0;
} // namespace

SettingSchema DcSource::makeSchema() {
    SettingSchema s;
    s.id = "instr/dc_source.schema.json";
    s.instrument = "dc_source";
    for (std::uint32_t k = 0; k < kChannels; ++k)
        s.settings.push_back(SettingSpec::real(std::format("ch[{}].current", k), "A", -10e-3, 10e-3,
                                               1e-9, 0.0, "Output current (±10 mA, 1 nA steps)"));
    s.settings.push_back(SettingSpec::integer("channels", "", kChannels, kChannels, kChannels,
                                              "Outputs of this unit")
                             .constant());
    s.settings.push_back(SettingSpec::boolean("output_on", true, "Outputs enabled"));
    s.settings.push_back(
        SettingSpec::real("mutual_inductance", "H", 0.5e-12, 5e-12, 0.0, 2e-12,
                          "Flux-line mutual inductance M (device flux_M_pH, Model)"));
    s.settings.push_back(
        SettingSpec::real("flux_offset", "", -0.5, 0.5, 0.0, 0.0, "Flux at zero current, in Φ0"));
    s.settings.push_back(
        SettingSpec::real("asymmetry", "", 0.0, 0.9, 0.0, 0.0, "SQUID junction asymmetry d"));
    s.settings.push_back(SettingSpec::real("line_resistance", "Ω", 0.1, 1e4, 0.0, 60.0,
                                           "Loom and filter resistance seen by an output"));
    s.settings.push_back(SettingSpec::boolean(
        "sweep", false, "Drive the flux-spectroscopy program (spec 10 §9) over the sweep range"));
    s.settings.push_back(
        SettingSpec::integer("sweep_channel", "", 0, kChannels - 1, 0, "Output that is swept"));
    s.settings.push_back(
        SettingSpec::real("sweep_start", "A", -10e-3, 10e-3, 1e-9, -1.2e-3, "Sweep start current"));
    s.settings.push_back(
        SettingSpec::real("sweep_stop", "A", -10e-3, 10e-3, 1e-9, 1.2e-3, "Sweep stop current"));
    s.settings.push_back(SettingSpec::integer("sweep_points", "", 3, 2001, 101, "Sweep points"));
    s.settings.push_back(refreshRateSetting());
    return s;
}

DcSource::DcSource(std::uint32_t index) : InstrumentBase({"dc_source", index}, makeSchema()) {
    std::vector<ChannelDesc> ch;
    for (std::uint32_t k = 0; k < kChannels; ++k) {
        ch.push_back({{},
                      std::format("ch[{}].I", k),
                      "s",
                      "A",
                      FidelityClass::Model,
                      false,
                      false,
                      "Output current"});
        ch.push_back({{},
                      std::format("ch[{}].V", k),
                      "s",
                      "V",
                      FidelityClass::Model,
                      false,
                      false,
                      "Compliance voltage I·R_line"});
    }
    ch.push_back({{},
                  "sweep",
                  "",
                  "Hz",
                  FidelityClass::Model,
                  false,
                  false,
                  "f01 versus Φ/Φ0 over the sweep range; aux `theory` (asymptotic) and `current`"});
    setChannels(std::move(ch));
}

std::uint32_t DcSource::qubitOfChannel(std::uint32_t k) const {
    const auto& bound = bindings().channels;
    return k < bound.size() ? bound[k].a : k;
}

double DcSource::currentA(std::uint32_t k) const {
    const SettingValues v = snapshot();
    return v.flag("output_on") && state() != State::Off ? v.real(std::format("ch[{}].current", k))
                                                        : 0.0;
}

double DcSource::fluxQuanta(double current) const {
    const SettingValues v = snapshot();
    return v.real("mutual_inductance") * current / units::consts::Phi0.v +
           v.real("flux_offset"); // Φ = M I + Φ_offset
}

Result<DcSource::Junction> DcSource::junction(std::uint32_t k) const {
    auto env = environment();
    if (!env || !env->calibration)
        return fail(err::NotBound, id().toString() + ": no calibration is bound");
    const std::uint32_t q = qubitOfChannel(k);
    {
        std::lock_guard lk(cacheMu_);
        if (auto it = cache_.find(q);
            it != cache_.end() && it->second.first == env->calibration.get())
            return it->second.second;
    }
    const hw::QubitCal* qc = env->calibration->qubit(q);
    if (!qc)
        return fail(err::BadInput,
                    std::format("{}: calibration has no qubit {}", id().toString(), q));
    auto solved = hw::transmon::fromTargets(qc->f01.value, qc->anharmonicity.value);
    if (!solved)
        return fail(err::BadInput,
                    std::format("{}: qubit {}: {}", id().toString(), q, solved.error().message));
    // The calibrated point is at zero current, i.e. at Φ_offset: E_JΣ = E_J(cal) / c(Φ_offset, d).
    const SettingValues v = snapshot();
    const double c = hw::transmon::josephsonEnergy(units::Frequency(1.0), v.real("flux_offset"),
                                                   v.real("asymmetry"))
                         .v;
    const Junction j{solved->EJ.v / std::max(c, 1e-6), solved->EC.v};
    std::lock_guard lk(cacheMu_);
    if (cacheOwner_ != env->calibration) {
        cache_.clear();
        cacheOwner_ = env->calibration;
    }
    cache_[q] = {env->calibration.get(), j};
    return j;
}

Result<double> DcSource::qubitFrequencyHz(std::uint32_t k, double current) const {
    QXL_TRY_ASSIGN(Junction j, junction(k));
    const double d = snapshot().real("asymmetry");
    const units::Frequency ej =
        hw::transmon::josephsonEnergy(units::Frequency(j.ejSumHz), fluxQuanta(current), d);
    if (ej.v <
        kTransmonRegimeMin * j.ecHz) // outside the transmon regime there is no resolvable 0-1 line
        return fail(err::BadInput,
                    std::format("{}: E_J/E_C = {:.2f} at this flux — outside the transmon regime",
                                id().toString(), ej.v / j.ecHz));
    auto spectrum = hw::transmon::chargeBasisSpectrum(ej, units::Frequency(j.ecHz));
    if (!spectrum)
        return std::unexpected(spectrum.error());
    return spectrum->f01.v;
}

Result<double> DcSource::theoryFrequencyHz(std::uint32_t k, double current) const {
    QXL_TRY_ASSIGN(Junction j, junction(k));
    const units::Frequency ej = hw::transmon::josephsonEnergy(
        units::Frequency(j.ejSumHz), fluxQuanta(current), snapshot().real("asymmetry"));
    return hw::transmon::approxF01(ej, units::Frequency(j.ecHz)).v; // T05 (3.4): √(8 E_J E_C) − E_C
}

void DcSource::settingChanged(std::string_view key) {
    if (key != "flux_offset" && key != "asymmetry")
        return; // E_JΣ depends on both
    std::lock_guard lk(cacheMu_);
    cache_.clear();
}

std::optional<double> DcSource::query(std::string_view path) const {
    if (const auto k = parsePortIndex(path); k && *k < kChannels) {
        if (path.ends_with(".I"))
            return currentA(*k);
        if (path.ends_with(".V"))
            return currentA(*k) * snapshot().real("line_resistance");
        if (path.ends_with(".flux"))
            return fluxQuanta(currentA(*k));
        if (path.ends_with(".f01")) {
            auto f = qubitFrequencyHz(*k, currentA(*k));
            return f ? std::optional(*f) : std::nullopt;
        }
    }
    return InstrumentBase::query(path);
}

Result<Trace> DcSource::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    const SettingValues& v = ctx.settings;
    if (const auto k = parsePortIndex(channel.name)) {
        const double current =
            v.flag("output_on") ? v.real(std::format("ch[{}].current", *k)) : 0.0;
        return scalarTrace(channel, ctx,
                           channel.name.ends_with(".I") ? current
                                                        : current * v.real("line_resistance"));
    }
    // sweep: f01(Φ/Φ0) over the current range of the swept output.
    const auto k = static_cast<std::uint32_t>(v.integer("sweep_channel"));
    const auto points = static_cast<std::size_t>(v.integer("sweep_points"));
    Trace t = makeTrace(channel, ctx);
    auto& theory = t.aux["theory"];
    auto& current = t.aux["current"];
    // Resolve (E_JΣ, E_C) once, so a calibration problem is reported as itself instead of being
    // mistaken below for "no point in the transmon regime" at every current.
    QXL_TRY(junction(k));
    for (std::size_t i = 0; i < points; ++i) {
        const double amps = v.real("sweep_start") + (v.real("sweep_stop") - v.real("sweep_start")) *
                                                        static_cast<double>(i) /
                                                        static_cast<double>(points - 1);
        auto f = qubitFrequencyHz(k, amps);
        if (!f && f.error().code != err::BadInput)
            return std::unexpected(f.error());
        if (!f)
            continue; // E_J too small near Φ0/2 of a symmetric SQUID: no qubit line there
        QXL_TRY_ASSIGN(double approx, theoryFrequencyHz(k, amps));
        t.x.push_back(fluxQuanta(amps));
        t.y.push_back(*f);
        theory.push_back(approx);
        current.push_back(amps);
    }
    if (t.x.empty())
        return fail(err::BadInput,
                    id().toString() + ": the sweep range holds no point in the transmon regime");
    const double now = v.flag("output_on") ? v.real(std::format("ch[{}].current", k)) : 0.0;
    if (auto f = qubitFrequencyHz(k, now))
        t.markers.push_back({fluxQuanta(now), *f, "operating_point", *f, 0.0, "Hz"});
    t.markers.push_back({0.0, 0.0, "current_per_flux_quantum",
                         units::consts::Phi0.v / v.real("mutual_inductance"), 0.0, "A"});
    return t;
}

} // namespace qlab::instr
