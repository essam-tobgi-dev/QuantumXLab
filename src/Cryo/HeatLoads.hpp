#pragma once
// Spec 11 §2 — per-stage heat loads: conduction along wiring, radiation between shields,
// attenuator/amplifier dissipation, and fixed parasitic loads.
#include "Core/Error.hpp"
#include "Cryo/Coax.hpp"
#include "Cryo/Stages.hpp"
#include "Cryo/Wiring.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace qlab::cryo {

using StageArray = std::array<double, kStageCount>;

// Applied RF power per line at the room-temperature input (W), keyed by line id. Lines absent
// from the map carry no signal power (attenuators dissipate nothing).
using LinePowers = std::map<std::string, double, std::less<>>;

struct StageLoad {
    double conduction_W = 0;   // wiring conduction arriving from the warmer stage
    double radiation_W = 0;    // radiative load from the warmer shield
    double dissipation_W = 0;  // attenuators, amplifiers, preamp pumps, heaters
    double parasitic_W = 0;    // fixed residual load (supports, gas conduction, vibration)
    double total() const { return conduction_W + radiation_W + dissipation_W + parasitic_W; }
};

struct LineLoad {
    std::string lineId;
    StageArray conduction_W{};   // conducted into each stage by this line's coax segments
    StageArray dissipation_W{};  // dissipated at each stage by attenuators/amplifiers
    double totalAt(Stage s) const { return conduction_W[stageIndex(s)] + dissipation_W[stageIndex(s)]; }
};

// Geometry of the radiation shields (spec 11 §2.2). Areas are the outer surface of each stage's
// can; emissivities per material; the OVC (RT) to PT1 gap uses MLI (10 layers).
struct ShieldGeometry {
    StageArray area_m2{0.0, 1.6, 1.1, 0.7, 0.45, 0.35};
    StageArray emissivity{0.9, 0.06, 0.03, 0.03, 0.03, 0.03}; // RT wall painted, then gold/Al
    std::array<int, kStageCount> mliLayers{0, 10, 0, 0, 0, 0};
};

// Fixed loads that do not scale with wiring (spec 11 §2.4): supports, residual gas, pulse-tube
// vibration heating at the MXC, heat switches. Defaults from the worked example.
inline StageArray defaultParasitic_W() { return {0.0, 2.0, 0.08, 0.3e-3, 8e-6, 8e-6}; }

class HeatLoadModel {
public:
    HeatLoadModel(const CoaxCatalog& coax, const MaterialCatalog& mats)
        : coax_(coax), mats_(mats) {}

    ShieldGeometry shields;
    StageArray parasitic_W = defaultParasitic_W();

    // Conduction and dissipation of one line at the given stage temperatures.
    Result<LineLoad> lineLoad(const WiringLine& line, const StageArray& T_K,
                              double inputPower_W = 0.0) const;

    // Sum over all lines + radiation + parasitic → per-stage loads.
    Result<std::array<StageLoad, kStageCount>> stageLoads(const Wiring& wiring, const StageArray& T_K,
                                                          const LinePowers& powers = {},
                                                          const CoolingParams& cool = {}) const;

    // Radiative load into stage s from the shield above it at the given temperatures.
    double radiation(Stage s, const StageArray& T_K) const;

private:
    const CoaxCatalog& coax_;
    const MaterialCatalog& mats_;
};

// Warmer neighbour of a stage (RT for PT1, ...). Stage::RT maps to itself.
Stage warmerStage(Stage s);

} // namespace qlab::cryo
