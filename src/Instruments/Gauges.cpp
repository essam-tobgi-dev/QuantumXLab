#include "Instruments/Gauges.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::instr {
namespace {
// Repeatability of a thermistor head at the reference averaging time; the quoted absolute accuracy
// (`uncertainty_db`) is the calibration factor's and is systematic, not part of this scatter.
constexpr double kPowerRepeatabilityDb = 0.02;
constexpr double kPowerReferenceAveragingMs = 100.0;
} // namespace

std::optional<ScalarReading> StripChartInstrument::lastReading() const {
    std::lock_guard lk(historyMu_);
    return history_.empty() ? std::nullopt : std::optional(history_.back());
}

Trace StripChartInstrument::chart(const ChannelDesc& channel, const AcquireContext& ctx,
                                  const ScalarReading& reading) {
    std::vector<ScalarReading> history;
    {
        std::lock_guard lk(historyMu_);
        history_.push_back(reading);
        const auto keep = static_cast<std::size_t>(
            std::max<std::int64_t>(1, ctx.settings.integer("history_points")));
        while (history_.size() > keep)
            history_.pop_front();
        history.assign(history_.begin(), history_.end());
    }
    Trace t = makeTrace(channel, ctx);
    t.sigma = Uncertainty{};
    for (auto const& h : history) {
        t.x.push_back(h.timeS);
        t.y.push_back(h.value);
        t.sigma->y.push_back(h.sigma);
    }
    t.markers.push_back(
        {reading.timeS, reading.value, "reading", reading.value, reading.sigma, channel.yUnit});
    if (reading.outOfRange)
        t.markers.push_back(
            {reading.timeS, reading.value, "out_of_range", reading.value, 0.0, channel.yUnit});
    return t;
}

// ---- pressure gauge -----------------------------------------------------------------------

SettingSchema PressureGauge::makeSchema() {
    SettingSchema s;
    s.id = "instr/gauge.schema.json";
    s.instrument = "pressure_gauge";
    s.settings = {
        SettingSpec::choice("node", {"ovc", "still", "condense", "dump"}, "ovc",
                            "Node of the gas circuit the gauge sits on"),
        SettingSpec::real("range_min", "mbar", 1e-8, 1e-8, 0.0, 1e-8, "Lowest readable pressure")
            .constant(),
        SettingSpec::real("range_max", "mbar", 3000.0, 3000.0, 0.0, 3000.0,
                          "Highest readable pressure")
            .constant(),
        SettingSpec::integer("history_points", "", 1, 100000, 600,
                             "Readings kept for the strip chart"),
        refreshRateSetting(),
    };
    return s;
}

PressureGauge::PressureGauge(std::uint32_t index)
    : StripChartInstrument({"pressure_gauge", index}, makeSchema()) {
    setChannels({{{},
                  "p",
                  "s",
                  "mbar",
                  FidelityClass::Model,
                  false,
                  false,
                  "Pressure reading versus lab time"}});
}

std::string_view PressureGauge::technology(std::string_view node, double mbar) {
    if (node != "ovc")
        return "capacitance"; // Baratron on the still, condensing line and dump
    return mbar < 1e-3 ? "cold_cathode" : "pirani";
}

std::optional<double> PressureGauge::query(std::string_view path) const {
    if (path == "p") {
        const auto r = lastReading();
        return r ? std::optional(r->value) : std::nullopt;
    }
    return InstrumentBase::query(path);
}

Result<Trace> PressureGauge::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    if (!ctx.env)
        return fail(err::NotBound,
                    id().toString() + ": no environment (gas-handling snapshot) is bound");
    const std::string node = ctx.settings.text("node");
    const cryo::GhsSnapshot& g = ctx.env->ghs;
    const double truth = node == "still"      ? g.p_still_mbar
                         : node == "condense" ? g.p_condense_mbar
                         : node == "dump"     ? g.p_dump_mbar
                                              : g.p_ovc_mbar;
    // Repeatability of the technology in use: capacitance 0.25 %, Pirani 5 %, cold cathode 10 % of
    // reading.
    const std::string_view tech = technology(node, truth);
    const double fraction = tech == "capacitance" ? 0.0025 : tech == "pirani" ? 0.05 : 0.10;
    ScalarReading r;
    r.timeS = ctx.stamp.labTimeS;
    const double noisy = truth * (1.0 + fraction * ctx.rng.normal());
    r.value = std::clamp(noisy, ctx.settings.real("range_min"), ctx.settings.real("range_max"));
    r.outOfRange = r.value != noisy;
    r.sigma = fraction * r.value;
    Trace t = chart(channel, ctx, r);
    t.markers.push_back({r.timeS, r.value, std::string(tech), r.value, r.sigma, "mbar"});
    return t;
}

// ---- flow meter ---------------------------------------------------------------------------

SettingSchema FlowMeter::makeSchema() {
    SettingSchema s;
    s.id = "instr/flow.schema.json";
    s.instrument = "flow_meter";
    s.settings = {
        SettingSpec::real("range_max", "mmol/s", 5.0, 5.0, 0.0, 5.0, "Full scale").constant(),
        SettingSpec::integer("history_points", "", 1, 100000, 600,
                             "Readings kept for the strip chart"),
        refreshRateSetting(),
    };
    return s;
}

FlowMeter::FlowMeter(std::uint32_t index)
    : StripChartInstrument({"flow_meter", index}, makeSchema()) {
    setChannels({{{},
                  "n3",
                  "s",
                  "mmol/s",
                  FidelityClass::Model,
                  false,
                  false,
                  "³He circulation rate versus lab time"}});
}

std::optional<double> FlowMeter::query(std::string_view path) const {
    if (path == "n3") {
        const auto r = lastReading();
        return r ? std::optional(r->value) : std::nullopt;
    }
    return InstrumentBase::query(path);
}

Result<Trace> FlowMeter::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    if (!ctx.env)
        return fail(err::NotBound,
                    id().toString() + ": no environment (gas-handling snapshot) is bound");
    // ṅ₃ of the GHS; the thermal snapshot carries the same value once the network has taken it
    // over.
    const double molPerS =
        ctx.env->ghs.n3_mol_s > 0.0 ? ctx.env->ghs.n3_mol_s : ctx.env->thermal.n3_mol_s;
    ScalarReading r;
    r.timeS = ctx.stamp.labTimeS;
    const double truth = molPerS * 1e3;
    r.sigma = 0.01 * truth + 0.002; // 1 % of reading + 2 µmol/s
    const double noisy = truth + r.sigma * ctx.rng.normal();
    r.value = std::clamp(noisy, 0.0, ctx.settings.real("range_max"));
    r.outOfRange = noisy > ctx.settings.real("range_max");
    return chart(channel, ctx, r);
}

// ---- power meter --------------------------------------------------------------------------

SettingSchema PowerMeter::makeSchema() {
    SettingSchema s;
    s.id = "instr/power_meter.schema.json";
    s.instrument = "power_meter";
    s.settings = {
        SettingSpec::text("input", "iq_mixer[0].rf", "Routing node the sensor is attached to"),
        SettingSpec::real("range_min", "dBm", -70.0, -70.0, 0.0, -70.0, "Sensor noise floor")
            .constant(),
        SettingSpec::real("range_max", "dBm", 20.0, 20.0, 0.0, 20.0, "Maximum input").constant(),
        // Calibrated band of the head: outside it the calibration factor does not apply and the
        // reading is flagged rather than trusted (spec 12 §8, descriptor `frequency range`).
        SettingSpec::real("freq_min", "Hz", 10e6, 10e6, 0.0, 10e6, "Lowest calibrated frequency")
            .constant(),
        SettingSpec::real("freq_max", "Hz", 18e9, 18e9, 0.0, 18e9, "Highest calibrated frequency")
            .constant(),
        // Absolute accuracy of the head (calibration factor): systematic, so it is reported beside
        // the reading rather than added to the point-to-point scatter.
        SettingSpec::real("uncertainty_db", "dB", 0.1, 0.5, 0.01, 0.1,
                          "Absolute accuracy of the calibration factor"),
        SettingSpec::real("averaging_ms", "ms", 1.0, 1000.0, 1.0, kPowerReferenceAveragingMs,
                          "Averaging time; the repeatability improves as 1/√t"),
        SettingSpec::integer("history_points", "", 1, 100000, 600,
                             "Readings kept for the strip chart"),
        refreshRateSetting(),
    };
    return s;
}

PowerMeter::PowerMeter(std::uint32_t index)
    : StripChartInstrument({"power_meter", index}, makeSchema()) {
    setChannels({{{},
                  "p",
                  "s",
                  "dBm",
                  FidelityClass::Model,
                  false,
                  false,
                  "Mean RF power at the node versus lab time"}});
}

double PowerMeter::repeatabilityDb(double averagingMs) {
    return kPowerRepeatabilityDb *
           std::sqrt(kPowerReferenceAveragingMs / std::max(averagingMs, 1e-9));
}

std::optional<double> PowerMeter::query(std::string_view path) const {
    if (path == "p" || path == "P_dBm") {
        const auto r = lastReading();
        return r ? std::optional(r->value) : std::nullopt;
    }
    return InstrumentBase::query(path);
}

Result<Trace> PowerMeter::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    const SignalGraph* graph = routing();
    if (!graph)
        return fail(err::NotBound, id().toString() + ": no routing matrix is bound");
    SignalRequest request;
    request.noiseSeed = ctx.rng.next();
    QXL_TRY_ASSIGN(Signal s, graph->signalAt(ctx.settings.text("input"), request));
    // The thermal sensor averages over the record: P̄ = ⟨|z|²⟩/2Z0, plus its own floor at range_min.
    const double floorW = wattsFromDbm(ctx.settings.real("range_min"));
    const double watts = meanPowerWatts(s) + floorW * (1.0 + 0.05 * ctx.rng.normal());
    const double repeatability = repeatabilityDb(ctx.settings.real("averaging_ms"));
    ScalarReading r;
    r.timeS = ctx.stamp.labTimeS;
    const double dbm = dbmFromWatts(std::max(watts, 1e-30)) + repeatability * ctx.rng.normal();
    r.value = std::clamp(dbm, ctx.settings.real("range_min"), ctx.settings.real("range_max"));
    r.outOfRange = r.value != dbm; // the head bottoms out on its floor as well as saturating on top
    r.sigma =
        repeatability; // point-to-point scatter; the calibration factor's accuracy is a marker
    Trace t = chart(channel, ctx, r);
    t.markers.push_back(
        {r.timeS, r.value, "uncertainty", ctx.settings.real("uncertainty_db"), 0.0, "dB"});
    // Out of the head's calibrated band the reading is uncalibrated, not wrong by a known factor.
    if (s.referenceHz > 0.0 && (s.referenceHz < ctx.settings.real("freq_min") ||
                                s.referenceHz > ctx.settings.real("freq_max")))
        t.markers.push_back({r.timeS, r.value, "out_of_band", s.referenceHz, 0.0, "Hz"});
    if (!s.connected)
        t.markers.push_back({r.timeS, r.value, "no_signal", r.value, 0.0, "dBm"});
    return t;
}

} // namespace qlab::instr
