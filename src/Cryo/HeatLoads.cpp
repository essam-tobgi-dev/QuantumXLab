#include "Cryo/HeatLoads.hpp"
#include "Cryo/Physics.hpp"
#include <algorithm>
#include <format>

namespace qlab::cryo {

Stage warmerStage(Stage s) {
    int i = stageIndex(s);
    return i == 0 ? Stage::RT : static_cast<Stage>(i - 1);
}

double HeatLoadModel::radiation(Stage s, const StageArray& T) const {
    if (s == Stage::RT)
        return 0.0;
    Stage w = warmerStage(s);
    int i = stageIndex(s), j = stageIndex(w);
    // Effective emissivity between the warmer shield (emitter) and this stage's can (absorber),
    // with N floating MLI layers reducing the exchange by (N+1).
    return phys::radiationLoad(shields.area_m2[i], shields.emissivity[j], shields.emissivity[i],
                               shields.mliLayers[i], T[j], T[i]);
}

Result<LineLoad> HeatLoadModel::lineLoad(const WiringLine& line, const StageArray& T,
                                         double Pin) const {
    LineLoad L;
    L.lineId = line.id;
    bool output = line.kind == LineKind::ReadoutOut;
    // Power tracking along the chain (input lines: RT → chip; output lines carry ~pW signal, so
    // only amplifier dissipation counts).
    double P = output ? 0.0 : Pin;
    for (const Element& e : line.elements) {
        int i = stageIndex(e.stage);
        switch (e.kind) {
        case ElementKind::CoaxSegment: {
            // A coax segment ending at stage e.stage connects the warmer stage to e.stage. For
            // output lines the element's stage is the colder end as well (chain is listed chip→RT
            // but each segment still spans warmer→e.stage). Guard the RT end.
            Stage cold = e.stage, hot = warmerStage(e.stage);
            if (output) { // the output chain lists segments toward RT: the segment "to PT2" spans
                          // PT2..STILL
                // elements were built with stage = the warmer end of the run; conduction flows into
                // the colder neighbour.
                hot = e.stage;
                cold = static_cast<Stage>(std::min(kStageCount - 1, stageIndex(e.stage) + 1));
                if (e.stage == Stage::RT) {
                    hot = Stage::RT;
                    cold = Stage::PT1;
                }
            }
            if (hot == cold)
                break;
            auto spec = coax_.get(e.coax);
            if (!spec)
                return std::unexpected(spec.error());
            auto q =
                conductionLoad(**spec, e.length_m, T[stageIndex(cold)], T[stageIndex(hot)], mats_);
            if (!q)
                return std::unexpected(q.error());
            L.conduction_W[stageIndex(cold)] += q->total() * (*spec)->conductors;
            if (!output)
                P *= std::pow(10.0,
                              -coaxLoss_dB(**spec, e.length_m, 5e9, T[stageIndex(cold)]) / 10.0);
            break;
        }
        case ElementKind::Attenuator:
            if (!output) {
                L.dissipation_W[i] += phys::attenuatorDissipation(P, e.attenuation_dB);
                P *= phys::dbToLinear(-e.attenuation_dB);
            }
            break;
        case ElementKind::IrFilter:
        case ElementKind::Isolator:
        case ElementKind::Circulator:
            if (!output) {
                L.dissipation_W[i] += phys::attenuatorDissipation(P, e.attenuation_dB);
                P *= phys::dbToLinear(-e.attenuation_dB);
            }
            break;
        case ElementKind::Amplifier:
        case ElementKind::Preamp:
            L.dissipation_W[i] += e.dissipation_W;
            break;
        default:
            break;
        }
    }
    return L;
}

Result<std::array<StageLoad, kStageCount>>
HeatLoadModel::stageLoads(const Wiring& w, const StageArray& T, const LinePowers& powers,
                          const CoolingParams& cool) const {
    std::array<StageLoad, kStageCount> out{};
    for (const auto& line : w.lines) {
        double P = 0.0;
        if (auto it = powers.find(line.id); it != powers.end())
            P = it->second;
        auto L = lineLoad(line, T, P);
        if (!L)
            return std::unexpected(L.error());
        for (int i = 0; i < kStageCount; ++i) {
            out[i].conduction_W += L->conduction_W[i];
            out[i].dissipation_W += L->dissipation_W[i];
        }
    }
    for (int i = 1; i < kStageCount; ++i) {
        Stage st = static_cast<Stage>(i);
        out[i].radiation_W = radiation(st, T);
        // The fixed parasitic term is support conduction and residual-gas load from the 300 K
        // environment, so it is proportional to the temperature difference to room temperature
        // and vanishes when the stage is warm. Without this a warm fridge with the pulse tube off
        // is driven *above* room temperature by its own parasitic load.
        double Trt = nominalTemperature(Stage::RT);
        double span = Trt - nominalTemperature(st);
        double f = span > 0 ? std::clamp((Trt - T[i]) / span, 0.0, 1.0) : 1.0;
        out[i].parasitic_W = parasitic_W[i] * f;
    }
    out[stageIndex(Stage::STILL)].dissipation_W += cool.stillHeater_W;
    out[stageIndex(Stage::MXC)].dissipation_W += cool.mxcHeater_W;
    return out;
}

} // namespace qlab::cryo
