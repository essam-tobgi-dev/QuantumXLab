#include "Instruments/Thermometer.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::instr {
namespace {
// RuO₂ thick film (Model): R0 exp[(T0/T)^{1/4}] — 36.8 kΩ at 10 mK, 1.9 kΩ at 4 K, 1.3 kΩ at 40 K.
constexpr double kRuO2R0 = 800.0, kRuO2T0 = 2.15;
// Cernox (Model): ln R = A + B ln T + C ln²T through (300 K, 60 Ω), (4.2 K, 4 kΩ), (1.4 K, 20 kΩ);
// monotonic below e^{−B/2C} ≈ 8600 K.
constexpr double kCxA = 10.439, kCxB = -1.6234, kCxC = 0.08961;
constexpr double kNoiseRuO2 = 0.005, kNoiseCernox = 0.001; // of reading (spec 12 §8)
} // namespace

double SensorCurve::minK() const { return kind == SensorKind::RuO2 ? 0.010 : 1.0; }
double SensorCurve::maxK() const { return kind == SensorKind::RuO2 ? 40.0 : 325.0; }

double SensorCurve::resistanceOhm(double T) const {
    if (kind == SensorKind::RuO2) return kRuO2R0 * std::exp(std::pow(kRuO2T0 / T, 0.25));
    const double x = std::log(T);
    return std::exp(kCxA + kCxB * x + kCxC * x * x);
}

double SensorCurve::temperatureK(double R) const {
    if (kind == SensorKind::RuO2) return kRuO2T0 / std::pow(std::log(R / kRuO2R0), 4.0);
    // C x² + B x + (A − ln R) = 0 on the decreasing branch x < −B/2C.
    const double disc = kCxB * kCxB - 4.0 * kCxC * (kCxA - std::log(R));
    return std::exp((-kCxB - std::sqrt(std::max(disc, 0.0))) / (2.0 * kCxC));
}

double SensorCurve::sensitivity(double T) const {
    if (kind == SensorKind::RuO2) return 0.25 * std::pow(kRuO2T0 / T, 0.25);
    return -(kCxB + 2.0 * kCxC * std::log(T));
}

SettingSchema Thermometer::makeSchema(SensorKind kind) {
    SettingSchema s;
    s.id = "instr/thermometer.schema.json";
    s.instrument = kind == SensorKind::RuO2 ? "thermometer_ruo2" : "thermometer_cernox";
    const bool ruo2 = kind == SensorKind::RuO2;
    s.settings = {
        ruo2 ? SettingSpec::choice("stage", {"MXC", "CP", "STILL", "PT2"}, "MXC", "Stage the sensor is mounted on")
             : SettingSpec::choice("stage", {"PT1", "PT2", "STILL"}, "PT2", "Stage the sensor is mounted on"),
        SettingSpec::real("excitation", "W", 1e-12, 1e-7, 0.0, ruo2 ? 1e-10 : 1e-9, "Bridge excitation power dissipated in the sensor"),
        // k of T_s⁴ = T⁴ + 4P/k. RuO₂ default: 10 nW reads 2.5 mK high at 10 mK (spec 12 §8, §15).
        SettingSpec::real("kapitza", "", 0.01, 1e4, 0.0, ruo2 ? 2.77 : 50.0, "Boundary conductance coefficient k in W/K⁴ (Model)"),
        SettingSpec::real("noise_fraction", "", ruo2 ? kNoiseRuO2 : kNoiseCernox, ruo2 ? kNoiseRuO2 : kNoiseCernox, 0.0,
                          ruo2 ? kNoiseRuO2 : kNoiseCernox, "Reading noise as a fraction of the reading").constant(),
        SettingSpec::real("range_min", "K", ruo2 ? 0.010 : 1.0, ruo2 ? 0.010 : 1.0, 0.0, ruo2 ? 0.010 : 1.0, "Lowest calibrated temperature").constant(),
        SettingSpec::real("range_max", "K", ruo2 ? 40.0 : 325.0, ruo2 ? 40.0 : 325.0, 0.0, ruo2 ? 40.0 : 325.0, "Highest calibrated temperature").constant(),
        SettingSpec::integer("history_points", "", 1, 100000, 600, "Readings kept for the strip chart"),
        refreshRateSetting(),
    };
    return s;
}

Thermometer::Thermometer(SensorKind kind, std::uint32_t index)
    : InstrumentBase({kind == SensorKind::RuO2 ? "thermometer_ruo2" : "thermometer_cernox", index}, makeSchema(kind)) {
    curve_.kind = kind;
    setChannels({
        {{}, "T", "s", "K", FidelityClass::Model, false, false, "Temperature reading versus lab time"},
        {{}, "R", "s", "Ω", FidelityClass::Model, false, false, "Sensor resistance versus lab time"},
    });
}

cryo::Stage Thermometer::stage() const {
    cryo::Stage s = cryo::Stage::MXC;
    cryo::stageFromName(snapshot().text("stage"), s);
    return s;
}

double Thermometer::sensorTemperature(double stageK, double excitationW, double kapitza) {
    return std::pow(std::pow(stageK, 4.0) + 4.0 * excitationW / kapitza, 0.25);
}

std::optional<ThermometerReading> Thermometer::lastReading() const {
    std::lock_guard lk(historyMu_);
    return history_.empty() ? std::nullopt : std::optional(history_.back());
}

std::optional<double> Thermometer::query(std::string_view path) const {
    const auto last = lastReading();
    if (path == "T") return last ? std::optional(last->temperatureK) : std::nullopt;
    if (path == "R") return last ? std::optional(last->resistanceOhm) : std::nullopt;
    return InstrumentBase::query(path);
}

Result<Trace> Thermometer::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    if (!ctx.env) return fail(err::NotBound, id().toString() + ": no environment (thermal snapshot) is bound");
    const SettingValues& v = ctx.settings;
    cryo::Stage stage = cryo::Stage::MXC;
    cryo::stageFromName(v.text("stage"), stage);
    ThermometerReading r;
    r.timeS = ctx.stamp.labTimeS;
    r.stageK = ctx.env->stageTemperatures()[static_cast<std::size_t>(cryo::stageIndex(stage))];
    r.sensorK = sensorTemperature(r.stageK, v.real("excitation"), v.real("kapitza"));
    const double inRange = std::clamp(r.sensorK, curve_.minK(), curve_.maxK());
    r.outOfRange = inRange != r.sensorK;
    // Bridge noise on R sized so that the converted reading scatters by `noise_fraction` of itself.
    const double noise = v.real("noise_fraction");
    r.resistanceOhm = curve_.resistanceOhm(inRange) * (1.0 + noise * curve_.sensitivity(inRange) * ctx.rng.normal());
    r.temperatureK = std::clamp(curve_.temperatureK(r.resistanceOhm), curve_.minK(), curve_.maxK());
    r.sigmaK = noise * r.temperatureK;
    std::vector<ThermometerReading> history;
    {
        std::lock_guard lk(historyMu_);
        history_.push_back(r);
        const auto keep = static_cast<std::size_t>(v.integer("history_points"));
        while (history_.size() > keep) history_.pop_front();
        history.assign(history_.begin(), history_.end());
    }
    Trace t = makeTrace(channel, ctx);
    t.sigma = Uncertainty{};
    for (auto const& h : history) {
        t.x.push_back(h.timeS);
        if (channel.name == "T") { t.y.push_back(h.temperatureK); t.sigma->y.push_back(h.sigmaK); }
        else { t.y.push_back(h.resistanceOhm); t.sigma->y.push_back(noise * curve_.sensitivity(h.temperatureK) * h.resistanceOhm); }
    }
    t.markers.push_back({r.timeS, t.y.back(), "reading", t.y.back(), t.sigma->y.back(), channel.yUnit});
    if (r.outOfRange) t.markers.push_back({r.timeS, t.y.back(), "out_of_range", r.temperatureK, 0.0, "K"});
    return t;
}

} // namespace qlab::instr
