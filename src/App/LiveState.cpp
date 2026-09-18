// Spec 17 §5 — shared helpers of the live-value providers (see Live.hpp).
#include "App/Live.hpp"
#include "Lab/Layout.hpp"   // stageFromLayoutName: the "rt"/"s50"/"mxc" spelling of spec 17 §3.1
#include <algorithm>
#include <cctype>
#include <charconv>

namespace qlab::app {

namespace detail {

PathHead splitPath(std::string_view path) {
    PathHead h;
    const auto dot = path.find('.');
    const auto bracket = path.find('[');
    if (bracket != std::string_view::npos && (dot == std::string_view::npos || bracket < dot)) {
        const auto close = path.find(']', bracket);
        h.name = path.substr(0, bracket);
        if (close == std::string_view::npos) return h;
        h.key = path.substr(bracket + 1, close - bracket - 1);
        std::uint32_t v = 0;
        if (std::from_chars(path.data() + bracket + 1, path.data() + close, v).ec == std::errc{}) h.index = v;
        h.rest = close + 1 < path.size() && path[close + 1] == '.' ? path.substr(close + 2)
                                                                   : path.substr(std::min(close + 1, path.size()));
        return h;
    }
    h.name = path.substr(0, dot);
    h.rest = dot == std::string_view::npos ? std::string_view{} : path.substr(dot + 1);
    return h;
}

bool stageFromAnyName(std::string_view name, cryo::Stage& out) {
    std::string upper(name);
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (cryo::stageFromName(upper, out)) return true;
    std::string lower(name);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // Layout spelling of spec 17 §3.1: rt, s50, s4, still, cp, mxc.
    return lab::stageFromLayoutName(lower, out);
}

} // namespace detail

cryo::StageArray LiveState::stageTemperatures() const {
    cryo::StageArray t = thermal.T_K;
    for (int i = 0; i < cryo::kStageCount; ++i)
        if (!(t[static_cast<std::size_t>(i)] > 0.0))
            t[static_cast<std::size_t>(i)] = cryo::nominalTemperature(static_cast<cryo::Stage>(i));
    return t;
}

const cryo::WiringLine* LiveState::line(std::size_t index) const {
    if (wiring == nullptr || index >= wiring->lines.size()) return nullptr;
    return &wiring->lines[index];
}

std::optional<cryo::LineNoise> LiveState::lineNoise(std::size_t index, double fHz) const {
    const cryo::WiringLine* l = line(index);
    if (l == nullptr || budget == nullptr) return std::nullopt;
    cryo::NoiseBudgetOptions opt;
    opt.f_Hz = fHz > 0.0 ? fHz : 5e9;
    return budget->inputLine(*l, stageTemperatures(), opt);
}

const viz::SingleReduction* LiveState::single(std::uint32_t qubit) const {
    if (!reductions) return nullptr;
    return reductions->single(QubitIndex{qubit});
}

std::optional<double> LiveState::resonatorHz(std::uint32_t qubit) const {
    if (calibration != nullptr)
        if (const hw::QubitCal* q = calibration->qubit(qubit); q != nullptr && q->readoutFrequency)
            return q->readoutFrequency->value.v;
    if (device != nullptr && qubit < device->readout.resonatorFrequencies.size())
        return device->readout.resonatorFrequencies[qubit].v;
    return std::nullopt;
}

void registerBindingProviders(lab::BindingRegistry& registry, std::shared_ptr<const LiveState> state,
                              const lab::StaticProvider& statics) {
    registry.registerProvider(lab::BindingRoot::Cryo, cryoProvider(state));
    registry.registerProvider(lab::BindingRoot::Wiring, wiringProvider(state));
    registry.registerProvider(lab::BindingRoot::Device, deviceProvider(state));
    registry.registerProvider(lab::BindingRoot::Instr, instrProvider(state));
    registry.registerProvider(lab::BindingRoot::Run, runProvider(std::move(state)));
    registry.registerProvider(lab::BindingRoot::Static, statics.asProvider());
}

// ---------------------------------------------------------------- instr.* and run.*

lab::BindingRegistry::Provider instrProvider(std::shared_ptr<const LiveState> state) {
    return [state](std::string_view path) -> std::optional<lab::BindingValue> {
        if (state->registry == nullptr) return std::nullopt;
        // Spec 12 §12: the probes are Simulator-only and the registry hides them in Physical-lab mode.
        const std::optional<double> v = state->registry->query(path, state->physicalLab);
        if (!v) return std::nullopt;
        lab::BindingValue out = lab::BindingValue::number(*v, {}, data::FidelityClass::Model);
        out.simulatorOnly = path.starts_with("probe.");
        return out;
    };
}

lab::BindingRegistry::Provider runProvider(std::shared_ptr<const LiveState> state) {
    return [state](std::string_view path) -> std::optional<lab::BindingValue> {
        using data::FidelityClass;
        const auto number = [](double v, std::string unit, FidelityClass cls) {
            return lab::BindingValue::number(v, std::move(unit), cls);
        };
        if (path == "running") return number(state->running ? 1.0 : 0.0, {}, FidelityClass::Exact);
        if (path == "shot") return number(static_cast<double>(state->shotsDone), {}, FidelityClass::Exact);
        if (path == "shots") return number(static_cast<double>(state->shotsTotal), {}, FidelityClass::Exact);
        if (path == "progress")
            return number(state->shotsTotal ? static_cast<double>(state->shotsDone) /
                                                  static_cast<double>(state->shotsTotal)
                                            : 0.0,
                          {}, FidelityClass::Exact);
        if (path == "gate_cursor") return number(static_cast<double>(state->playheadGate), {}, FidelityClass::Exact);
        if (path == "playhead") return number(state->playheadS, "s", FidelityClass::Exact);
        if (path == "wall_time") return number(state->runWallTimeS, "s", FidelityClass::Model);
        if (path == "seed") return number(static_cast<double>(state->seed), {}, FidelityClass::Exact);
        // Spec 17 §8: the Illustrative signal-flow packets. `schedule.line[k].{t_s, amp, sigma_s}`
        // is the time since the line's most recent play started, its amplitude and its width.
        if (path.starts_with("schedule.line[")) return detail::packetBinding(*state, path.substr(9));
        return std::nullopt;
    };
}

} // namespace qlab::app
