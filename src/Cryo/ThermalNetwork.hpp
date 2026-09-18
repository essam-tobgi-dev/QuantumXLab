#pragma once
// Spec 11 §1–§2, T08 §7 — lumped thermal network of the six stages: steady state and transient.
#include "Core/Error.hpp"
#include "Cryo/HeatLoads.hpp"
#include "Cryo/Stages.hpp"
#include "Cryo/Wiring.hpp"
#include <array>
#include <string>
#include <vector>

namespace qlab::cryo {

// Published to the UI at 10 Hz (spec 11 §8). All values SI.
struct ThermalSnapshot {
    double time_s = 0;
    StageArray T_K{};
    StageArray load_W{};       // total load arriving at each stage
    StageArray cooling_W{};    // cooling power available at the current temperature
    StageArray margin_W{};     // cooling − load (negative: stage warming)
    std::array<StageLoad, kStageCount> loads{};
    double n3_mol_s = 0;
    double stillPower_W = 0;
    double mxcBase_K = 0;      // no-load base temperature of the mixing chamber
    bool steady = false;
    std::vector<std::string> warnings; // "PT2 cannot reach 4 K: load 2.1 W exceeds 1.5 W" etc.
};

// Lumped heat capacities of the stage masses (copper plates + cans, kg), spec 11 §2.5 defaults
// for a mid-size cryogen-free system.
struct StageMasses {
    // T08 §7: "a fridge with 50 kg of copper and stainless below the 50 K stage stores ≈ 4 MJ",
    // which with 40–100 W of net first-stage cooling gives the ≈ 22 h descent to 4 K. The masses
    // below sum to 50 kg for PT2+STILL+CP+MXC (the 4 K plate and its radiation shield carry most
    // of it, with the dilution unit, cold plate and sample package below); PT1 carries the 50 K
    // plate and its aluminium shield.
    StageArray copper_kg{0.0, 12.0, 24.0, 7.0, 4.0, 7.0};
    StageArray steel_kg{0.0, 6.0, 4.0, 1.0, 1.0, 2.0};
};

class ThermalNetwork {
public:
    ThermalNetwork(const Wiring& wiring, const HeatLoadModel& loads, const MaterialCatalog& mats);

    CoolingParams cooling;
    StageMasses masses;
    LinePowers linePowers; // RF power applied per input line (W at the RT bulkhead)

    // Solve for temperatures where cooling(T_s) = load_s(T) at every stage, top-down with
    // fixed-point iteration over the coupled loads (loads depend on neighbour temperatures).
    Result<ThermalSnapshot> steadyState(int maxIter = 60, double tol_K = 1e-5) const;

    // Transient: C(T) dT/dt = Q_cool(T) − Q_load(T) per stage, explicit Euler with adaptive
    // sub-stepping (stability from the local time constant). Advances the internal state.
    Result<ThermalSnapshot> step(double dt_s);
    void setTemperatures(const StageArray& T) { T_ = T; }
    const StageArray& temperatures() const { return T_; }
    double time() const { return time_s_; }
    void warmToRoomTemperature();

    // Heat capacity of a stage's mass at temperature T (J/K).
    double heatCapacity(Stage s, double T_K) const;

    const Wiring& wiring() const { return wiring_; }

private:
    Result<ThermalSnapshot> evaluate(const StageArray& T, bool steady) const;
    const Wiring& wiring_;
    const HeatLoadModel& loads_;
    const MaterialCatalog& mats_;
    StageArray T_{};
    double time_s_ = 0;
};

} // namespace qlab::cryo
