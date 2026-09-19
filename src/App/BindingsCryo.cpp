// Spec 17 §5 — the `cryo.*` and `wiring.*` live-value providers (see Live.hpp). Every value comes
// from the thermal snapshot, the gas-handling snapshot or the wiring the network was solved for;
// nothing here re-derives physics the Cryo module already owns.
#include "App/Live.hpp"
#include <cmath>
#include <format>

namespace qlab::app {
namespace {

using data::FidelityClass;
using detail::splitPath;
using detail::stageFromAnyName;
using lab::BindingValue;

BindingValue num(double v, std::string unit, FidelityClass cls) {
    return BindingValue::number(v, std::move(unit), cls);
}
BindingValue flag(bool v) {
    return BindingValue::number(v ? 1.0 : 0.0, {}, FidelityClass::Model);
}

std::size_t idx(cryo::Stage s) {
    return static_cast<std::size_t>(cryo::stageIndex(s));
}

// Pulse-tube reference figures of a mid-size cryogen-free system (spec 11 §1). Class Model: the
// compressor and its rotary valve are not simulated, only the cooling power they provide.
constexpr double kPulseTubeHz = 1.4;
constexpr double kPulseTubeHighPa = 2.2e6;
constexpr double kPulseTubeLowPa = 0.8e6;
constexpr double kTurboNominalRpm = 90000.0;
constexpr double kLn2TrapColdK = 77.0;

std::optional<BindingValue> stageValue(const LiveState& s, std::string_view path) {
    const detail::PathHead head = splitPath(path);
    cryo::Stage stage = cryo::Stage::RT;
    if (!stageFromAnyName(head.name, stage))
        return std::nullopt;
    const std::size_t k = idx(stage);
    if (head.rest == "T")
        return num(s.stageTemperatures()[k], "K", FidelityClass::Numerical);
    if (head.rest == "P_cool")
        return num(s.thermal.cooling_W[k], "W", FidelityClass::Model);
    if (head.rest == "P_load")
        return num(s.thermal.load_W[k], "W", FidelityClass::Model);
    if (head.rest == "margin")
        return num(s.thermal.margin_W[k], "W", FidelityClass::Model);
    if (head.rest == "P_heater") {
        if (stage == cryo::Stage::STILL)
            return num(s.cooling.stillHeater_W, "W", FidelityClass::Exact);
        if (stage == cryo::Stage::MXC)
            return num(s.cooling.mxcHeater_W, "W", FidelityClass::Exact);
        return std::nullopt;
    }
    if (head.rest == "P_cond")
        return num(s.thermal.loads[k].conduction_W, "W", FidelityClass::Model);
    if (head.rest == "P_rad")
        return num(s.thermal.loads[k].radiation_W, "W", FidelityClass::Model);
    if (head.rest == "P_diss")
        return num(s.thermal.loads[k].dissipation_W, "W", FidelityClass::Model);
    return std::nullopt;
}

const cryo::Pump* findPump(const LiveState& s, std::string_view id) {
    for (const cryo::Pump& p : s.ghs.pumps)
        if (p.id == id)
            return &p;
    return nullptr;
}

} // namespace

lab::BindingRegistry::Provider cryoProvider(std::shared_ptr<const LiveState> state) {
    return [state](std::string_view path) -> std::optional<BindingValue> {
        const LiveState& s = *state;
        const detail::PathHead head = splitPath(path);
        const cryo::StageArray T = s.stageTemperatures();

        if (head.name == "stage")
            return stageValue(s, head.rest);

        // Spec 12 §11: the thermometers, gauges and flow meters are instruments; their readings
        // carry the sensor's own calibration error, which is why they are not the snapshot value.
        if (head.name == "thermo" || head.name == "gauge" || head.name == "flow" ||
            head.name == "pm")
            if (s.registry != nullptr)
                if (auto v = s.registry->query(path, s.physicalLab))
                    return num(*v, {}, FidelityClass::Numerical);

        if (head.name == "flow") { // model fallback when no flow meter is in the rack
            if (head.rest == "n3")
                return num(s.thermal.n3_mol_s, "mol/s", FidelityClass::Model);
            if (head.rest == "p_still")
                return num(s.ghs.p_still_mbar, "mbar", FidelityClass::Model);
            if (head.rest == "p_condense")
                return num(s.ghs.p_condense_mbar, "mbar", FidelityClass::Model);
            return std::nullopt;
        }
        if (head.name == "ghs" && head.rest == "state")
            return BindingValue::text(std::string(cryo::fridgeStateName(s.fridge)),
                                      FidelityClass::Exact);
        if (head.name == "ovc") {
            if (head.rest == "pressure")
                return num(s.ghs.p_ovc_mbar, "mbar", FidelityClass::Model);
            if (head.rest == "lowered")
                return flag(false); // the can is in place whenever the scene shows it
            return std::nullopt;
        }
        if (head.name == "pump") {
            if (head.rest == "turbo.rpm") {
                const cryo::Pump* p = findPump(s, "turbo_ovc");
                return p ? std::optional(
                               num(p->speed_frac * kTurboNominalRpm, "1/min", FidelityClass::Model))
                         : std::nullopt;
            }
            if (head.rest == "scroll.on") {
                const cryo::Pump* p = findPump(s, "scroll_backing");
                return p ? std::optional(flag(p->on)) : std::nullopt;
            }
            return std::nullopt;
        }
        if (head.name == "valve" && head.rest == "open") {
            const std::size_t k = head.index.value_or(0);
            if (k >= s.ghs.valves.size())
                return std::nullopt;
            return flag(s.ghs.valves[k].open);
        }
        if (head.name == "dump" && head.rest == "p")
            return num(s.ghs.p_dump_mbar, "mbar", FidelityClass::Model);
        if (head.name == "trap") {
            if (head.rest == "T")
                return num(s.ghs.ln2TrapCold ? kLn2TrapColdK : T[idx(cryo::Stage::RT)], "K",
                           FidelityClass::Model);
            return std::nullopt; // the trap's liquid level is not modelled (spec 11 §8)
        }
        if (head.name == "pt") {
            if (head.rest == "freq")
                return num(kPulseTubeHz, "Hz", FidelityClass::Model);
            if (head.rest == "p_high")
                return num(kPulseTubeHighPa, "Pa", FidelityClass::Model);
            if (head.rest == "p_low")
                return num(kPulseTubeLowPa, "Pa", FidelityClass::Model);
            if (head.rest == "running")
                return flag(s.cooling.pulseTubeOn);
            if (head.rest == "stage1.T")
                return num(T[idx(cryo::Stage::PT1)], "K", FidelityClass::Numerical);
            if (head.rest == "stage2.T")
                return num(T[idx(cryo::Stage::PT2)], "K", FidelityClass::Numerical);
            return std::nullopt;
        }
        if (head.name == "hx") {
            // The continuous exchanger spans still → cold plate; the step exchangers are strung
            // geometrically between the cold plate and the mixing chamber (spec 11 §1).
            const double still = T[idx(cryo::Stage::STILL)], cp = T[idx(cryo::Stage::CP)],
                         mxc = T[idx(cryo::Stage::MXC)];
            if (head.rest == "cont.T_in")
                return num(still, "K", FidelityClass::Model);
            if (head.rest == "cont.T_out")
                return num(cp, "K", FidelityClass::Model);
            const detail::PathHead step = splitPath(head.rest);
            if (step.name == "step" && step.rest == "T") {
                constexpr int kSteps = 5;
                const int k = static_cast<int>(step.index.value_or(0));
                if (k >= kSteps || cp <= 0.0 || mxc <= 0.0)
                    return std::nullopt;
                const double f = static_cast<double>(k + 1) / (kSteps + 1);
                return num(cp * std::pow(mxc / cp, f), "K", FidelityClass::Model);
            }
            return std::nullopt;
        }
        if (head.name == "line" && head.rest == "condense.T_in")
            return num(T[idx(cryo::Stage::PT2)], "K", FidelityClass::Model);
        return std::nullopt;
    };
}

} // namespace qlab::app
