#include "Cryo/Wiring.hpp"
#include "Core/Json.hpp"
#include <algorithm>
#include <format>
#include <set>

namespace qlab::cryo {

std::string_view lineKindName(LineKind k) {
    switch (k) {
    case LineKind::XY: return "xy";
    case LineKind::Flux: return "flux";
    case LineKind::ReadoutIn: return "readout_in";
    case LineKind::ReadoutOut: return "readout_out";
    case LineKind::Pump: return "pump";
    case LineKind::DC: return "dc";
    }
    return "?";
}
bool lineKindFromName(std::string_view s, LineKind& out) {
    for (LineKind k : {LineKind::XY, LineKind::Flux, LineKind::ReadoutIn, LineKind::ReadoutOut, LineKind::Pump, LineKind::DC})
        if (lineKindName(k) == s) { out = k; return true; }
    return false;
}
std::string_view elementKindName(ElementKind k) {
    switch (k) {
    case ElementKind::CoaxSegment: return "coax";
    case ElementKind::Attenuator: return "attenuator";
    case ElementKind::LowPassFilter: return "lpf";
    case ElementKind::IrFilter: return "ir_filter";
    case ElementKind::Isolator: return "isolator";
    case ElementKind::Circulator: return "circulator";
    case ElementKind::Amplifier: return "amplifier";
    case ElementKind::Preamp: return "preamp";
    case ElementKind::ThermalClamp: return "thermal_clamp";
    case ElementKind::RcFilter: return "rc_filter";
    case ElementKind::Bulkhead: return "bulkhead";
    case ElementKind::Chip: return "chip";
    }
    return "?";
}

double WiringLine::totalAttenuation_dB() const {
    double a = 0;
    for (auto& e : elements) if (e.kind == ElementKind::Attenuator) a += e.attenuation_dB;
    return a;
}
double WiringLine::attenuationAt(Stage s) const {
    double a = 0;
    for (auto& e : elements) if (e.kind == ElementKind::Attenuator && e.stage == s) a += e.attenuation_dB;
    return a;
}
std::vector<const Element*> WiringLine::elementsAt(Stage s) const {
    std::vector<const Element*> v;
    for (auto& e : elements) if (e.stage == s) v.push_back(&e);
    return v;
}

std::array<double, kStageCount> ChainCatalog::defaultLengths() { return {0.0, 0.25, 0.20, 0.15, 0.10, 0.10}; }
std::vector<std::string> ChainCatalog::ids() {
    return {"drive_std", "drive_60dB", "readout_in_std", "readout_out_std", "flux_std", "pump_std", "dc_loom_std"};
}
bool ChainCatalog::has(std::string_view id) { auto v = ids(); return std::find(v.begin(), v.end(), id) != v.end(); }

namespace {
Element coax(Stage s, const char* type, double L) { Element e; e.kind = ElementKind::CoaxSegment; e.stage = s; e.coax = type; e.length_m = L; e.id = std::format("coax_{}", stageName(s)); return e; }
Element att(Stage s, double dB) { Element e; e.kind = ElementKind::Attenuator; e.stage = s; e.attenuation_dB = dB; e.id = std::format("att_{}", stageName(s)); return e; }
Element clamp(Stage s) { Element e; e.kind = ElementKind::ThermalClamp; e.stage = s; e.conductance_W_K = 0.3 * std::max(nominalTemperature(s), 0.02) / 4.0; e.id = std::format("clamp_{}", stageName(s)); return e; }
Element lpf(Stage s, double fc) { Element e; e.kind = ElementKind::LowPassFilter; e.stage = s; e.cutoff_Hz = fc; e.id = std::format("lpf_{}", stageName(s)); return e; }
Element ir(Stage s) { Element e; e.kind = ElementKind::IrFilter; e.stage = s; e.attenuation_dB = 2.0; e.cutoff_Hz = 15e9; e.id = "ir_filter"; return e; }
Element iso(Stage s, int n) { Element e; e.kind = ElementKind::Isolator; e.stage = s; e.isolation_dB = 20; e.attenuation_dB = 0.2; e.id = std::format("iso_{}_{}", stageName(s), n); return e; }
Element amp(Stage s, const char* id, double g, double tn, double p) { Element e; e.kind = ElementKind::Amplifier; e.stage = s; e.gain_dB = g; e.noiseTemperature_K = tn; e.dissipation_W = p; e.id = id; return e; }
Element rc(Stage s, double fc) { Element e; e.kind = ElementKind::RcFilter; e.stage = s; e.cutoff_Hz = fc; e.id = std::format("rc_{}", stageName(s)); return e; }
Element mark(ElementKind k, Stage s, const char* id) { Element e; e.kind = k; e.stage = s; e.id = id; return e; }
} // namespace

Result<WiringLine> ChainCatalog::instantiate(std::string_view chain, std::string lineId, std::string channel,
                                             const std::array<double, kStageCount>& L) {
    WiringLine w;
    w.id = std::move(lineId); w.channel = std::move(channel); w.chain = std::string(chain);
    auto& E = w.elements;
    auto Lm = [&](Stage s) { return L[stageIndex(s)]; };
    if (chain == "drive_std" || chain == "drive_60dB" || chain == "readout_in_std" || chain == "flux_std" || chain == "pump_std") {
        bool ro = chain == "readout_in_std", flux = chain == "flux_std", pump = chain == "pump_std";
        w.kind = ro ? LineKind::ReadoutIn : (flux ? LineKind::Flux : (pump ? LineKind::Pump : LineKind::XY));
        E.push_back(mark(ElementKind::Bulkhead, Stage::RT, "bulkhead"));
        E.push_back(coax(Stage::PT1, "SS_086", Lm(Stage::PT1)));
        E.push_back(clamp(Stage::PT1));
        E.push_back(coax(Stage::PT2, "SS_086", Lm(Stage::PT2)));
        E.push_back(att(Stage::PT2, 20));
        E.push_back(coax(Stage::STILL, "CuNi_086", Lm(Stage::STILL)));
        if (ro) E.push_back(att(Stage::STILL, 10)); else E.push_back(clamp(Stage::STILL));
        E.push_back(coax(Stage::CP, "CuNi_086", Lm(Stage::CP)));
        if (ro || chain == "drive_60dB" || pump) E.push_back(att(Stage::CP, 20)); else E.push_back(clamp(Stage::CP));
        E.push_back(coax(Stage::MXC, "CuNi_086", Lm(Stage::MXC)));
        if (flux) { E.push_back(lpf(Stage::MXC, 1e9)); E.push_back(ir(Stage::MXC)); }
        else { E.push_back(att(Stage::MXC, 20)); E.push_back(lpf(Stage::MXC, ro ? 10e9 : 8e9)); E.push_back(ir(Stage::MXC)); }
        if (flux) { // flux: 20 dB only at PT2 (spec 11 §4.3): remove the extra attenuators
            std::erase_if(E, [](const Element& e) { return e.kind == ElementKind::Attenuator && e.stage != Stage::PT2; });
            E.insert(E.begin() + 7, clamp(Stage::CP)); // keep thermalization at CP
        }
        E.push_back(mark(ElementKind::Chip, Stage::MXC, "chip"));
        return w;
    }
    if (chain == "readout_out_std") {
        w.kind = LineKind::ReadoutOut;
        w.preamp = "none";
        E.push_back(mark(ElementKind::Chip, Stage::MXC, "chip"));
        E.push_back(iso(Stage::MXC, 1));
        E.push_back(iso(Stage::MXC, 2));
        E.push_back(coax(Stage::CP, "NbTi_086", Lm(Stage::MXC)));
        E.push_back(clamp(Stage::CP));
        E.push_back(coax(Stage::STILL, "NbTi_086", Lm(Stage::CP)));
        E.push_back(clamp(Stage::STILL));
        E.push_back(coax(Stage::PT2, "NbTi_086", Lm(Stage::STILL)));
        E.push_back(amp(Stage::PT2, "hemt", w.hemtGain_dB, w.hemtNoise_K, w.hemtDissipation_W));
        // Post-HEMT run uses CuNi (not copper): the low-noise stage is already behind the HEMT, so
        // the extra insertion loss is tolerable and the reduced conductivity keeps the 4 K/50 K heat
        // budget closed for a multi-line feedline (real systems make the same trade, spec 11 §6).
        E.push_back(coax(Stage::PT1, "CuNi_219", Lm(Stage::PT2)));
        E.push_back(clamp(Stage::PT1));
        E.push_back(coax(Stage::RT, "CuNi_219", Lm(Stage::PT1)));
        E.push_back(amp(Stage::RT, "rt_amp", 30, 80, 0));
        E.push_back(mark(ElementKind::Bulkhead, Stage::RT, "bulkhead"));
        return w;
    }
    if (chain == "dc_loom_std") {
        w.kind = LineKind::DC;
        E.push_back(mark(ElementKind::Bulkhead, Stage::RT, "bulkhead"));
        for (Stage s : {Stage::PT1, Stage::PT2, Stage::STILL, Stage::CP, Stage::MXC}) {
            E.push_back(coax(s, "DC_loom_12", Lm(s)));
            E.push_back(clamp(s));
            if (s == Stage::PT2) E.push_back(rc(s, 10e3));
            if (s == Stage::MXC) E.push_back(rc(s, 1e3));
        }
        E.push_back(mark(ElementKind::Chip, Stage::MXC, "chip"));
        return w;
    }
    return fail(ErrorCode::Cryo_ + 20, std::format("unknown wiring chain '{}'", chain));
}

const WiringLine* Wiring::find(std::string_view lineId) const {
    for (auto& l : lines) if (l.id == lineId) return &l;
    return nullptr;
}
std::map<LineKind, int> Wiring::countByKind() const {
    std::map<LineKind, int> m;
    for (auto& l : lines) ++m[l.kind];
    return m;
}

Result<Wiring> parseWiring(const std::string& text) {
    // The device files written by tools/gencal.py use the kind "wiring", like their siblings
    // "device", "calibration" and "pulses"; "qlab.wiring" is the older spelling and stays accepted.
    auto env = core::JsonEnvelope::parse(text, "");
    if (!env) return std::unexpected(env.error());
    if (env->kind != "wiring" && env->kind != "qlab.wiring")
        return fail(ErrorCode::InvalidArgument,
                    "expected kind 'wiring', found '" + env->kind + "'");
    const core::Json& d = env->data;
    Wiring w;
    w.device = d.value("device", "");
    w.layout = d.value("layout", "");
    if (d.contains("lengths_m"))
        for (auto& [k, v] : d["lengths_m"].items()) { Stage s; if (stageFromName(k, s)) w.stageLengths_m[stageIndex(s)] = v.get<double>(); }
    if (!d.contains("lines") || !d["lines"].is_array()) return fail(ErrorCode::Cryo_ + 21, "wiring.json: 'lines' missing");
    for (const auto& j : d["lines"]) {
        auto line = ChainCatalog::instantiate(j.value("chain", ""), j.value("id", ""), j.value("channel", ""), w.stageLengths_m);
        if (!line) return std::unexpected(line.error());
        if (j.contains("attenuation_db"))
            for (auto& [k, v] : j["attenuation_db"].items()) {
                Stage s; if (!stageFromName(k, s)) return fail(ErrorCode::Cryo_ + 22, "wiring.json: bad stage '" + k + "'");
                line->attenuationOverride_dB[s] = v.get<double>();
                bool found = false;
                for (auto& e : line->elements) if (e.kind == ElementKind::Attenuator && e.stage == s) { e.attenuation_dB = v.get<double>(); found = true; }
                if (!found && v.get<double>() > 0) { Element a; a.kind = ElementKind::Attenuator; a.stage = s; a.attenuation_dB = v.get<double>(); a.id = std::format("att_{}", stageName(s));
                    auto pos = std::find_if(line->elements.begin(), line->elements.end(), [&](const Element& e) { return e.kind == ElementKind::ThermalClamp && e.stage == s; });
                    if (pos != line->elements.end()) *pos = a; else line->elements.push_back(a); }
            }
        if (j.contains("preamp")) {
            line->preamp = j["preamp"].get<std::string>();
            if (*line->preamp != "none") {
                Element p; p.kind = ElementKind::Preamp; p.stage = Stage::MXC; p.variant = *line->preamp; p.id = *line->preamp;
                p.gain_dB = p.variant == "twpa" ? 20.0 : 18.0; p.noiseTemperature_K = p.variant == "twpa" ? 0.35 : 0.25; p.dissipation_W = 0.5e-6;
                auto pos = std::find_if(line->elements.begin(), line->elements.end(), [](const Element& e) { return e.kind == ElementKind::Isolator; });
                line->elements.insert(pos == line->elements.end() ? line->elements.end() : pos + 1, p);
            }
        }
        if (j.contains("hemt")) {
            const auto& h = j["hemt"];
            line->hemtGain_dB = h.value("gain_db", 38.0); line->hemtNoise_K = h.value("noise_k", 2.5); line->hemtDissipation_W = h.value("dissipation_mw", 12.0) * 1e-3;
            for (auto& e : line->elements) if (e.id == "hemt") { e.gain_dB = line->hemtGain_dB; e.noiseTemperature_K = line->hemtNoise_K; e.dissipation_W = line->hemtDissipation_W; }
        }
        w.lines.push_back(std::move(*line));
    }
    return w;
}
Result<Wiring> loadWiring(const std::filesystem::path& path) {
    auto t = core::readTextFile(path);
    if (!t) return std::unexpected(t.error());
    auto w = parseWiring(*t);
    if (!w) w.error().notes.push_back("file: " + path.string());
    return w;
}
std::string serializeWiring(const Wiring& w) {
    core::Json d;
    d["device"] = w.device; d["layout"] = w.layout;
    for (Stage s : kStages) if (s != Stage::RT) d["lengths_m"][std::string(stageName(s))] = w.stageLengths_m[stageIndex(s)];
    d["lines"] = core::Json::array();
    for (auto& l : w.lines) {
        core::Json j{{"id", l.id}, {"channel", l.channel}, {"chain", l.chain}};
        core::Json a = core::Json::object();
        for (auto& e : l.elements) if (e.kind == ElementKind::Attenuator) a[std::string(stageName(e.stage))] = e.attenuation_dB;
        if (!a.empty()) j["attenuation_db"] = a;
        if (l.preamp) j["preamp"] = *l.preamp;
        if (l.kind == LineKind::ReadoutOut) j["hemt"] = {{"gain_db", l.hemtGain_dB}, {"noise_k", l.hemtNoise_K}, {"dissipation_mw", l.hemtDissipation_W * 1e3}};
        d["lines"].push_back(j);
    }
    return core::JsonEnvelope::serialize("wiring", d);
}

Status validateWiring(const Wiring& w) {
    static const std::set<double> allowed{0, 3, 6, 10, 20, 30};
    std::set<std::string> channels;
    for (auto& l : w.lines) {
        if (!ChainCatalog::has(l.chain)) return fail(ErrorCode::Cryo_ + 20, std::format("line '{}': unknown chain '{}'", l.id, l.chain));
        if (!channels.insert(l.channel).second) return fail(ErrorCode::Cryo_ + 23, std::format("channel '{}' is served by more than one line", l.channel));
        bool input = l.kind != LineKind::ReadoutOut;
        std::set<Stage> anchored;
        for (auto& e : l.elements) {
            if (e.kind == ElementKind::Attenuator) {
                if (!allowed.count(e.attenuation_dB)) return fail(ErrorCode::Cryo_ + 24, std::format("line '{}': attenuation {} dB at {} not in {{0,3,6,10,20,30}}", l.id, e.attenuation_dB, stageName(e.stage)));
                if (!input) return fail(ErrorCode::Cryo_ + 25, std::format("line '{}': attenuator on an output line", l.id));
            }
            if ((e.kind == ElementKind::Isolator || e.kind == ElementKind::Circulator || e.kind == ElementKind::Amplifier || e.kind == ElementKind::Preamp) && input)
                return fail(ErrorCode::Cryo_ + 26, std::format("line '{}': '{}' is not allowed on an input line", l.id, elementKindName(e.kind)));
            if (e.kind != ElementKind::CoaxSegment && e.kind != ElementKind::Bulkhead && e.kind != ElementKind::Chip) anchored.insert(e.stage);
        }
        for (Stage s : {Stage::PT1, Stage::PT2, Stage::STILL, Stage::CP, Stage::MXC})
            if (!anchored.count(s)) return fail(ErrorCode::Cryo_ + 27, std::format("line '{}': no thermalization at {}", l.id, stageName(s)));
    }
    return {};
}

} // namespace qlab::cryo
