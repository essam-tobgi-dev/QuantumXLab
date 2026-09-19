#pragma once
// Spec 11 §1 — the six thermal nodes and their cooling-power laws (T08 §1.2, §2).
#include <array>
#include <cmath>
#include <string_view>

namespace qlab::cryo {

enum class Stage : int { RT = 0, PT1 = 1, PT2 = 2, STILL = 3, CP = 4, MXC = 5 };
inline constexpr int kStageCount = 6;
inline constexpr std::array<Stage, kStageCount> kStages{Stage::RT,    Stage::PT1, Stage::PT2,
                                                        Stage::STILL, Stage::CP,  Stage::MXC};

std::string_view stageName(Stage s);     // "RT", "PT1", ...
std::string_view stageLongName(Stage s); // "Mixing chamber", ...
bool stageFromName(std::string_view name, Stage& out);
inline int stageIndex(Stage s) {
    return static_cast<int>(s);
}

// Nominal temperatures (K): 293, 45, 3.5, 0.85, 0.10, 0.015 (spec 11 §1, T08 §3).
double nominalTemperature(Stage s);

// Parameters the user controls through the GHS and heaters (spec 11 §1, §8).
struct CoolingParams {
    bool pulseTubeOn = true;
    bool circulating = true;      // dilution running (after condensation)
    double n3_mol_s = 1.0e-3;     // 3He circulation rate
    double Q0_W = 30e-6;          // parasitic MXC load setting the base temperature
    double stillHeater_W = 10e-3; // raises circulation; dissipated at STILL
    double mxcHeater_W = 0.0;
    double filmBurden_W = 2e-3; // superfluid film load on the still
};

// Cooling power available at a stage held at temperature T (W). Monotonic non-decreasing in T,
// clipped at 0. RT returns +inf (environment). Laws from spec 11 §1:
//   PT1: 40 W (T-30)/15,  PT2: 1.5 W (T-2.8)/1.4,  STILL: n3 L3 - film,
//   CP: 0.35 n3 84 T^2,   MXC: 84 n3 T^2 - Q0.
double coolingPower(Stage s, double T_K, const CoolingParams& p);

// Analytic derivative dQ/dT (W/K), used by the Newton steady-state solve.
double coolingPowerDerivative(Stage s, double T_K, const CoolingParams& p);

// Temperature at which the stage's cooling power equals a given load (inverse law); returns
// nominal RT for Stage::RT. Used as the starting point of the steady-state solve and for the
// "stage cannot reach base" diagnostic.
double temperatureForLoad(Stage s, double load_W, const CoolingParams& p);

// Base temperature of the MXC with no external load: sqrt(Q0 / (84 n3)).
inline double mxcBaseTemperature(const CoolingParams& p) {
    return p.n3_mol_s > 0 ? std::sqrt(p.Q0_W / (84.0 * p.n3_mol_s)) : 4.0;
}

} // namespace qlab::cryo
