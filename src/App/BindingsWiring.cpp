// Spec 17 §5 / 11 §2–§6 — the `wiring.*` live-value provider (see Live.hpp). Each line is walked
// once from its bulkhead toward the chip, carrying the signal power the schedule puts on it; the
// attenuator and filter dissipations are what the thermal network is already solving with, and the
// photon numbers come from `cryo::NoiseBudget`, never from a second copy of the physics.
#include "App/Live.hpp"
#include <cmath>

namespace qlab::app {
namespace {

using data::FidelityClass;
using detail::PathHead;
using detail::splitPath;
using detail::stageFromAnyName;
using lab::BindingValue;

BindingValue num(double v, std::string unit, FidelityClass cls = FidelityClass::Model) {
    return BindingValue::number(v, std::move(unit), cls);
}

double linear(double dB) {
    return std::pow(10.0, dB / 10.0);
}

// Insertion loss of one element in dB (0 for elements that pass the signal through).
double elementLossDb(const LiveState& s, const cryo::Element& e, double fHz, double T_K) {
    switch (e.kind) {
    case cryo::ElementKind::Attenuator:
    case cryo::ElementKind::IrFilter:
    case cryo::ElementKind::Isolator:
    case cryo::ElementKind::Circulator:
    case cryo::ElementKind::LowPassFilter:
    case cryo::ElementKind::RcFilter:
        return e.attenuation_dB;
    case cryo::ElementKind::CoaxSegment:
        if (s.coax != nullptr)
            if (const cryo::CoaxSpec* c = s.coax->find(e.coax))
                return cryo::coaxLoss_dB(*c, e.length_m, fHz, T_K);
        return 0.0;
    case cryo::ElementKind::Amplifier:
    case cryo::ElementKind::Preamp:
        return -e.gain_dB;
    default:
        return 0.0;
    }
}

// The element the path names, together with the signal power arriving at it (W).
struct Found {
    const cryo::Element* element = nullptr;
    double powerIn_W = 0.0;
};

Found findElement(const LiveState& s, const cryo::WiringLine& line, cryo::ElementKind kind,
                  std::uint32_t nth, const std::string* variant, double fHz) {
    const cryo::StageArray T = s.stageTemperatures();
    double power = detail::lineInputPower(s, line);
    std::uint32_t seen = 0;
    for (const cryo::Element& e : line.elements) {
        const double T_K = T[static_cast<std::size_t>(cryo::stageIndex(e.stage))];
        if (e.kind == kind && (variant == nullptr || e.variant == *variant)) {
            if (seen++ == nth)
                return {&e, power};
        }
        power *= linear(-elementLossDb(s, e, fHz, T_K));
    }
    return {};
}

// Photon occupation leaving the n-th attenuator of an input line (T07 (9.2), spec 11 §6).
std::optional<double> attenuatorPhotons(const LiveState& s, std::size_t lineIndex,
                                        std::uint32_t nth, double fHz) {
    const std::optional<cryo::LineNoise> noise = s.lineNoise(lineIndex, fHz);
    if (!noise)
        return std::nullopt;
    std::uint32_t seen = 0;
    for (const cryo::PhotonStep& step : noise->steps) {
        if (step.A_dB <= 0.0)
            continue; // a clamp applies no attenuation
        if (seen++ == nth)
            return step.n_out;
    }
    return std::nullopt;
}

std::optional<BindingValue> lineValue(const LiveState& s, std::size_t lineIndex,
                                      std::string_view path) {
    const cryo::WiringLine* line = s.line(lineIndex);
    if (line == nullptr)
        return std::nullopt;
    const cryo::StageArray T = s.stageTemperatures();
    const double fHz = 5e9; // the readout/drive band the wiring budget of spec 11 §6 is quoted at
    const PathHead head = splitPath(path);
    const auto stageOf = [&](const cryo::Element& e) {
        return T[static_cast<std::size_t>(cryo::stageIndex(e.stage))];
    };

    if (head.name == "rt" && head.rest == "P_in")
        return num(detail::lineInputPower(s, *line), "W", FidelityClass::Model);

    if (head.name == "attn") {
        const Found f = findElement(s, *line, cryo::ElementKind::Attenuator, head.index.value_or(0),
                                    nullptr, fHz);
        if (f.element == nullptr)
            return std::nullopt;
        if (head.rest == "P_diss") // spec 11 §2.3: P_in (1 − 10^(−A/10)) is dissipated at the stage
            return num(f.powerIn_W * (1.0 - linear(-f.element->attenuation_dB)), "W",
                       FidelityClass::Model);
        if (head.rest == "n_th") {
            auto n = attenuatorPhotons(s, lineIndex, head.index.value_or(0), fHz);
            return n ? std::optional(num(*n, {}, FidelityClass::Model)) : std::nullopt;
        }
        if (head.rest == "T")
            return num(stageOf(*f.element), "K", FidelityClass::Numerical);
        return std::nullopt;
    }
    if (head.name == "irf" && head.rest == "P_diss") {
        const Found f = findElement(s, *line, cryo::ElementKind::IrFilter, head.index.value_or(0),
                                    nullptr, fHz);
        if (f.element == nullptr)
            return std::nullopt;
        return num(f.powerIn_W * (1.0 - linear(-f.element->attenuation_dB)), "W",
                   FidelityClass::Model);
    }
    if (head.name == "iso") {
        const Found f = findElement(s, *line, cryo::ElementKind::Isolator, head.index.value_or(0),
                                    nullptr, fHz);
        if (f.element == nullptr)
            return std::nullopt;
        if (head.rest == "T")
            return num(stageOf(*f.element), "K", FidelityClass::Numerical);
        if (head.rest == "isolation")
            return num(f.element->isolation_dB, "dB", FidelityClass::Model);
        return std::nullopt;
    }
    if (head.name == "clamp" && head.rest == "T") {
        cryo::Stage stage = cryo::Stage::MXC;
        if (!head.key.empty() && !stageFromAnyName(head.key, stage))
            return std::nullopt;
        return num(T[static_cast<std::size_t>(cryo::stageIndex(stage))], "K",
                   FidelityClass::Numerical);
    }
    if (head.name == "seg") {
        const Found f = findElement(s, *line, cryo::ElementKind::CoaxSegment,
                                    head.index.value_or(0), nullptr, fHz);
        if (f.element == nullptr)
            return std::nullopt;
        const double cold = stageOf(*f.element);
        const double hot =
            T[static_cast<std::size_t>(cryo::stageIndex(cryo::warmerStage(f.element->stage)))];
        if (head.rest == "T_cold")
            return num(cold, "K", FidelityClass::Numerical);
        if (head.rest == "T_hot")
            return num(hot, "K", FidelityClass::Numerical);
        if (head.rest == "P_cond") {
            if (s.coax == nullptr || s.materials == nullptr)
                return std::nullopt;
            const cryo::CoaxSpec* spec = s.coax->find(f.element->coax);
            if (spec == nullptr)
                return std::nullopt;
            auto load = cryo::conductionLoad(*spec, f.element->length_m, cold, hot, *s.materials);
            if (!load)
                return std::nullopt;
            return num(load->total(), "W", FidelityClass::Model);
        }
        return std::nullopt;
    }
    if (head.name == "hemt" || head.name == "rtamp") {
        const bool warm = head.name == "rtamp";
        const cryo::StageArray& temps = T;
        std::uint32_t nth = 0;
        for (const cryo::Element& e : line->elements) {
            if (e.kind != cryo::ElementKind::Amplifier)
                continue;
            const bool isWarm = e.stage == cryo::Stage::RT;
            if (isWarm != warm)
                continue;
            (void)nth;
            if (head.rest == "P_diss")
                return num(e.dissipation_W, "W", FidelityClass::Model);
            if (head.rest == "T_N")
                return num(e.noiseTemperature_K, "K", FidelityClass::Model);
            if (head.rest == "gain")
                return num(e.gain_dB, "dB", FidelityClass::Model);
            if (head.rest == "T")
                return num(temps[static_cast<std::size_t>(cryo::stageIndex(e.stage))], "K",
                           FidelityClass::Numerical);
            return std::nullopt;
        }
        return std::nullopt;
    }
    if (head.name == "jpa" || head.name == "twpa") {
        const std::string variant(head.name);
        const Found f = findElement(s, *line, cryo::ElementKind::Preamp, 0, &variant, fHz);
        if (f.element == nullptr)
            return std::nullopt;
        if (head.rest == "gain")
            return num(f.element->gain_dB, "dB", FidelityClass::Model);
        if (head.rest == "T_N")
            return num(f.element->noiseTemperature_K, "K", FidelityClass::Model);
        if (head.rest == "pump_on")
            return num(f.element->dissipation_W > 0.0 ? 1.0 : 0.0, {}, FidelityClass::Model);
        if (head.rest == "P_diss")
            return num(f.element->dissipation_W, "W", FidelityClass::Model);
        return std::nullopt;
    }
    return std::nullopt;
}

// Totals over the whole wiring (spec 11 §2): the rows the rack and the fridge shells carry.
std::optional<BindingValue> wiringTotal(const LiveState& s, std::string_view path) {
    if (s.wiring == nullptr)
        return std::nullopt;
    double sum = 0.0;
    bool any = false;
    if (path == "hemt.P_total" || path == "pump.P") {
        const cryo::ElementKind kind =
            path[0] == 'h' ? cryo::ElementKind::Amplifier : cryo::ElementKind::Preamp;
        for (const cryo::WiringLine& l : s.wiring->lines)
            for (const cryo::Element& e : l.elements)
                if (e.kind == kind) {
                    sum += e.dissipation_W;
                    any = true;
                }
        return any ? std::optional(num(sum, "W", FidelityClass::Model)) : std::nullopt;
    }
    if (path == "readout.P") {
        for (const cryo::WiringLine& l : s.wiring->lines)
            if (l.kind == cryo::LineKind::ReadoutIn) {
                sum += detail::lineInputPower(s, l);
                any = true;
            }
        return any ? std::optional(num(sum, "W", FidelityClass::Model)) : std::nullopt;
    }
    if (path == "dc.P_cond") { // conduction the DC looms bring to the mixing chamber
        if (s.heat == nullptr)
            return std::nullopt;
        const cryo::StageArray T = s.stageTemperatures();
        for (const cryo::WiringLine& l : s.wiring->lines) {
            if (l.kind != cryo::LineKind::DC)
                continue;
            auto load = s.heat->lineLoad(l, T, 0.0);
            if (!load)
                continue;
            sum += load->conduction_W[static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::MXC))];
            any = true;
        }
        return any ? std::optional(num(sum, "W", FidelityClass::Model)) : std::nullopt;
    }
    return std::nullopt;
}

} // namespace

namespace detail {

double lineInputPower(const LiveState& state, const cryo::WiringLine& line) {
    const auto it = state.linePowers.find(line.id);
    if (it != state.linePowers.end())
        return it->second;
    const auto byChannel = state.linePowers.find(line.channel);
    return byChannel != state.linePowers.end() ? byChannel->second : 0.0;
}

} // namespace detail

lab::BindingRegistry::Provider wiringProvider(std::shared_ptr<const LiveState> state) {
    return [state](std::string_view path) -> std::optional<BindingValue> {
        const PathHead head = splitPath(path);
        if (head.name == "line") {
            if (!head.index)
                return std::nullopt;
            return lineValue(*state, *head.index, head.rest);
        }
        return wiringTotal(*state, path);
    };
}

} // namespace qlab::app
