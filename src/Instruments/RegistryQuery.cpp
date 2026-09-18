// Spec 12 §11, spec 17 §5 — fridge-line taps and the scalar binding query of the registry.
#include "Instruments/Registry.hpp"
#include "Units/Units.hpp"
#include <charconv>
#include <format>

namespace qlab::instr {

// ---- LineTap ------------------------------------------------------------------------------

LineTap::LineTap(const SignalGraph& graph, std::string upstream, std::shared_ptr<const InputHub> inputs, std::string lineId, cryo::Stage stage)
    : graph_(graph), upstream_(std::move(upstream)), inputs_(std::move(inputs)), lineId_(std::move(lineId)), stage_(stage) {}

double LineTap::sampleRateHz(std::string_view) const { return graph_.sampleRateAt(upstream_).value_or(0.0); }

Result<Signal> LineTap::signal(std::string_view, const SignalRequest& request) const {
    QXL_TRY_ASSIGN(Signal s, graph_.signalAt(upstream_, request));
    auto env = inputs_ ? inputs_->environment() : nullptr;
    if (!env || !env->wiring) return fail(err::NotBound, std::format("line tap {}: no wiring is bound", lineId_));
    const cryo::WiringLine* line = env->wiring->find(lineId_);
    if (!line) return fail(err::BadRouting, std::format("line tap: the wiring has no line '{}'", lineId_));
    static const CryoCatalogs fallback;
    const CryoCatalogs& catalogs = env->catalogs ? *env->catalogs : fallback;
    const cryo::StageArray temperatures = env->stageTemperatures();
    const double f = s.referenceHz > 0.0 ? s.referenceHz : 5e9;
    // Everything from the bulkhead down to and including this stage (elements run RT → chip).
    double lossDb = 0.0;
    for (auto const& e : line->elements) {
        if (cryo::stageIndex(e.stage) > cryo::stageIndex(stage_)) break;
        if (e.kind == cryo::ElementKind::Attenuator) lossDb += e.attenuation_dB;
        if (e.kind == cryo::ElementKind::CoaxSegment)
            if (const cryo::CoaxSpec* c = catalogs.coax.find(e.coax))
                lossDb += cryo::coaxLoss_dB(*c, e.length_m, f, temperatures[static_cast<std::size_t>(cryo::stageIndex(e.stage))]);
    }
    scaleSignal(s, -lossDb);
    // Photon number leaving this stage (T07 (9.2)) → one-sided noise density n̄ h f.
    cryo::NoiseBudgetOptions opt;
    opt.f_Hz = f;
    const cryo::LineNoise budget = catalogs.budget.inputLine(*line, temperatures, opt);
    double photons = budget.n_in;
    for (auto const& step : budget.steps)
        if (cryo::stageIndex(step.stage) <= cryo::stageIndex(stage_)) photons = step.n_out;
    s.noisePsdWPerHz = photons * units::consts::h.v * f;
    s.node = std::format("line.{}.{}", lineId_, cryo::stageName(stage_));
    s.cls = FidelityClass::Model;
    return s;
}

// ---- query --------------------------------------------------------------------------------

namespace {
struct Head { std::string_view name; std::optional<std::uint32_t> index; std::string_view rest; };

// "gen[2].f" → {gen, 2, "f"};  "awg.running" → {awg, –, "running"};  "locked" → {locked, –, ""}.
Head splitHead(std::string_view path) {
    Head h;
    const auto dot = path.find('.');
    const auto bracket = path.find('[');
    if (bracket != std::string_view::npos && (dot == std::string_view::npos || bracket < dot)) {
        const auto close = path.find(']', bracket);
        h.name = path.substr(0, bracket);
        if (close != std::string_view::npos) {
            std::uint32_t v = 0;
            if (std::from_chars(path.data() + bracket + 1, path.data() + close, v).ec == std::errc{}) h.index = v;
            h.rest = close + 1 < path.size() && path[close + 1] == '.' ? path.substr(close + 2) : path.substr(std::min(close + 1, path.size()));
        }
        return h;
    }
    h.name = path.substr(0, dot);
    h.rest = dot == std::string_view::npos ? std::string_view{} : path.substr(dot + 1);
    return h;
}
} // namespace

std::optional<double> InstrumentRegistry::query(std::string_view path, bool physicalLab) const {
    for (std::string_view root : {"instr.", "cryo."})
        if (path.starts_with(root)) { path.remove_prefix(root.size()); break; }
    if (path.starts_with("run.")) {
        auto run = hub_->run();
        if (!run) return std::nullopt;
        const std::string_view key = path.substr(4);
        if (key == "shot") return static_cast<double>(run->shot);
        if (key == "shots") return static_cast<double>(run->shots);
        if (key == "progress") return run->shots ? static_cast<double>(run->shot) / static_cast<double>(run->shots) : 0.0;
        if (key == "gate_cursor") return static_cast<double>(run->gateCursor);
        if (key == "wall_time") return run->wallTimeS;
        if (key == "playhead") return run->playheadS;
        if (key == "running") return run->running ? 1.0 : 0.0;
        return std::nullopt;
    }
    const Head h = splitHead(path);
    auto ask = [&](std::string_view kind, std::uint32_t index, std::string_view rest) -> std::optional<double> {
        const IInstrument* i = find(kind, index);
        if (!i || (physicalLab && i->simulatorOnly())) return std::nullopt;
        return i->query(rest);
    };
    const std::uint32_t idx = h.index.value_or(0);
    if (h.name == "gen") return ask("sg_mw", idx, h.rest);
    if (h.name == "mixer") return ask("iq_mixer", idx, h.rest);
    if (h.name == "dig") return ask("digitizer", idx, h.rest);
    if (h.name == "dc") return ask("dc_source", idx, h.rest);
    if (h.name == "sa") return ask("spectrum_analyzer", idx, h.rest);
    if (h.name == "scope") return ask("oscilloscope", idx, h.rest);
    if (h.name == "seq") return ask("awg", idx, "seq." + std::string(h.rest));
    if (h.name == "ref" || h.name == "trig") return ask("controller", idx, h.rest);
    if (h.name == "gauge") return ask("pressure_gauge", idx, h.rest);
    if (h.name == "flow") return ask("flow_meter", idx, h.rest);
    if (h.name == "pm") return ask("power_meter", idx, h.rest);
    if (h.name == "thermo") { // every thermometer in creation order, RuO₂ and Cernox alike
        std::uint32_t k = 0;
        for (auto const& i : instruments_)
            if (i->id().kind.starts_with("thermometer_") && k++ == idx) return i->query(h.rest);
        return std::nullopt;
    }
    if (h.name == "probe") { // "probe.state.qubit[0].bloch[2]" → probe_state
        const Head p = splitHead(h.rest);
        return ask("probe_" + std::string(p.name), p.index.value_or(0), p.rest);
    }
    return ask(h.name, idx, h.rest); // "awg.running", "vna.span", "sg_mw[1].frequency", …
}

} // namespace qlab::instr
