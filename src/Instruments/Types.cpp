#include "Instruments/Types.hpp"
#include "Data/Fidelity.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::instr {

std::string_view stateName(State s) {
    switch (s) {
    case State::Off: return "Off";
    case State::Idle: return "Idle";
    case State::Armed: return "Armed";
    case State::Acquiring: return "Acquiring";
    case State::Fault: return "Fault";
    }
    return "?";
}

std::string settingToString(const SettingValue& v) {
    struct Visitor {
        std::string operator()(std::monostate) const { return "<unset>"; }
        std::string operator()(bool b) const { return b ? "true" : "false"; }
        std::string operator()(std::int64_t i) const { return std::to_string(i); }
        std::string operator()(double d) const { return std::format("{:.12g}", d); }
        std::string operator()(const std::string& s) const { return s; }
    };
    return std::visit(Visitor{}, v);
}

std::optional<double> settingNumber(const SettingValue& v) {
    if (auto* b = std::get_if<bool>(&v)) return *b ? 1.0 : 0.0;
    if (auto* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
    if (auto* d = std::get_if<double>(&v)) return *d;
    return std::nullopt;
}

const Marker* Trace::marker(std::string_view label) const {
    for (auto const& m : markers)
        if (m.label == label) return &m;
    return nullptr;
}

data::Trace2D Trace::toTrace2D() const {
    data::Trace2D out;
    out.name = instrument + "." + channel;
    out.xUnit = xUnit;
    out.yUnit = yUnit;
    out.x = x;
    out.y = y;
    if (!y_im.empty()) out.yIm = y_im;
    if (sigma) out.sigma = sigma->y;
    for (auto const& m : markers) out.markers.push_back({m.x, m.label});
    out.cls = cls;
    out.timestampS = t.labTimeS;
    return out;
}

std::string traceToCsv(const Trace& t, std::string_view settingsComment) {
    std::string out;
    out += std::format("# instrument: {}\n# channel: {}\n# fidelity_class: {}\n", t.instrument, t.channel,
                       data::fidelityName(t.cls));
    if (t.simulatorOnly) out += "# simulator_only: true\n";
    out += std::format("# lab_time_s: {:.9g}\n# sequence: {}\n", t.t.labTimeS, t.t.sequence);
    if (!settingsComment.empty()) out += std::format("# settings: {}\n", settingsComment);
    for (auto const& m : t.markers)
        out += std::format("# marker: {} x={:.12g} y={:.12g} value={:.12g} sigma={:.6g} {}\n", m.label, m.x, m.y,
                           m.value, m.sigma, m.unit);
    const bool cplx = t.complexValued();
    const bool withSigma = t.sigma && t.sigma->y.size() == t.x.size();
    std::string names = cplx ? "x,re,im" : "x,y";
    std::string unitsRow = cplx ? std::format("{},{},{}", t.xUnit, t.yUnit, t.yUnit) : std::format("{},{}", t.xUnit, t.yUnit);
    if (withSigma) {
        names += ",sigma";
        unitsRow += "," + t.yUnit;
    }
    for (auto const& [name, col] : t.aux) {
        if (col.size() != t.x.size()) continue;
        names += "," + name;
        unitsRow += ",";
    }
    out += names + "\n" + unitsRow + "\n";
    for (std::size_t i = 0; i < t.x.size(); ++i) {
        out += std::format("{:.17g},{:.17g}", t.x[i], i < t.y.size() ? t.y[i] : 0.0);
        if (cplx) out += std::format(",{:.17g}", t.y_im[i]);
        if (withSigma) out += std::format(",{:.9g}", t.sigma->y[i]);
        for (auto const& [name, col] : t.aux) {
            (void)name;
            if (col.size() == t.x.size()) out += std::format(",{:.12g}", col[i]);
        }
        out += "\n";
    }
    return out;
}

double dbmFromWatts(double watts) { return 10.0 * std::log10(std::max(watts, 1e-300) / 1e-3); }
double wattsFromDbm(double dbm) { return 1e-3 * std::pow(10.0, dbm / 10.0); }
double tonePowerWatts(double peakVolts) { return peakVolts * peakVolts / (2.0 * kZ0); }

} // namespace qlab::instr
