#include "Cryo/ThermalNetwork.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::cryo {

ThermalNetwork::ThermalNetwork(const Wiring& w, const HeatLoadModel& l, const MaterialCatalog& m)
    : wiring_(w), loads_(l), mats_(m) {
    for (Stage s : kStages)
        T_[stageIndex(s)] = nominalTemperature(s);
}

void ThermalNetwork::warmToRoomTemperature() {
    for (auto& t : T_)
        t = 293.0;
    time_s_ = 0;
}

double ThermalNetwork::heatCapacity(Stage s, double T) const {
    int i = stageIndex(s);
    double C = 0;
    if (const Material* cu = mats_.find("Cu_OFHC_RRR100"))
        C += masses.copper_kg[i] * cu->specificHeat(T);
    else
        C += masses.copper_kg[i] * 385.0 * std::min(1.0, std::pow(T / 300.0, 3.0)); // fallback
    if (const Material* ss = mats_.find("stainless_304"))
        C += masses.steel_kg[i] * ss->specificHeat(T);
    else
        C += masses.steel_kg[i] * 480.0 * std::min(1.0, std::pow(T / 300.0, 3.0));
    return std::max(C, 1e-3);
}

Result<ThermalSnapshot> ThermalNetwork::evaluate(const StageArray& T, bool steady) const {
    ThermalSnapshot s;
    s.time_s = time_s_;
    s.T_K = T;
    auto L = loads_.stageLoads(wiring_, T, linePowers, cooling);
    if (!L)
        return std::unexpected(L.error());
    s.loads = *L;
    for (int i = 0; i < kStageCount; ++i) {
        Stage st = static_cast<Stage>(i);
        s.load_W[i] = (*L)[i].total();
        double q = coolingPower(st, T[i], cooling);
        s.cooling_W[i] = std::isinf(q) ? s.load_W[i] : q;
        s.margin_W[i] = s.cooling_W[i] - s.load_W[i];
    }
    s.n3_mol_s = cooling.n3_mol_s;
    s.stillPower_W = cooling.stillHeater_W;
    s.mxcBase_K = mxcBaseTemperature(cooling);
    s.steady = steady;
    // Capacity warnings: a stage whose load exceeds the maximum cooling of its law.
    if (cooling.pulseTubeOn) {
        if (s.load_W[1] > 40.0)
            s.warnings.push_back(
                std::format("PT1 load {:.1f} W exceeds 40 W capacity", s.load_W[1]));
        if (s.load_W[2] > 1.5)
            s.warnings.push_back(
                std::format("PT2 load {:.2f} W exceeds 1.5 W at 4.2 K", s.load_W[2]));
    }
    if (T[5] > 0.05 && cooling.circulating)
        s.warnings.push_back(
            std::format("MXC at {:.1f} mK: load {:.1f} µW", T[5] * 1e3, s.load_W[5] * 1e6));
    return s;
}

Result<ThermalSnapshot> ThermalNetwork::steadyState(int maxIter, double tol) const {
    StageArray T = T_;
    T[0] = nominalTemperature(Stage::RT);
    if (!cooling.pulseTubeOn) {
        for (auto& t : T)
            t = 293.0;
        return evaluate(T, true);
    }
    for (int it = 0; it < maxIter; ++it) {
        auto L = loads_.stageLoads(wiring_, T, linePowers, cooling);
        if (!L)
            return std::unexpected(L.error());
        double maxDelta = 0;
        for (int i = 1; i < kStageCount; ++i) {
            Stage st = static_cast<Stage>(i);
            double Tnew = temperatureForLoad(st, (*L)[i].total(), cooling);
            if (!cooling.circulating && i >= 3)
                Tnew = std::max(Tnew, T[2]); // no dilution: hangs at PT2
            // Under-relax to keep the coupled iteration stable.
            double Tn = 0.5 * T[i] + 0.5 * Tnew;
            maxDelta = std::max(maxDelta, std::abs(Tn - T[i]) / std::max(Tn, 1e-3));
            T[i] = Tn;
        }
        if (maxDelta < tol)
            return evaluate(T, true);
    }
    auto s = evaluate(T, false);
    if (s)
        s->warnings.push_back("steady-state iteration did not converge");
    return s;
}

Result<ThermalSnapshot> ThermalNetwork::step(double dt) {
    // Loads are evaluated explicitly at the start of the step (they vary slowly); each stage's
    // own cooling law is treated implicitly (backward Euler with Newton), because the cold stages
    // have tiny heat capacities and would otherwise force sub-millisecond explicit steps.
    double remaining = dt;
    while (remaining > 0) {
        double h = std::min(remaining, 300.0); // loads re-evaluated at least every 5 min
        auto L = loads_.stageLoads(wiring_, T_, linePowers, cooling);
        if (!L)
            return std::unexpected(L.error());
        for (int i = 1; i < kStageCount; ++i) {
            Stage st = static_cast<Stage>(i);
            double load = (*L)[i].total();
            double Told = T_[i];
            double C = heatCapacity(st, Told);
            // Solve C (T − Told)/h = load − Qcool(T) for T.
            double T = Told;
            for (int it = 0; it < 30; ++it) {
                double q = coolingPower(st, T, cooling);
                if (std::isinf(q)) {
                    T = Told;
                    break;
                }
                double g = C * (T - Told) / h - (load - q);
                double dg = C / h + coolingPowerDerivative(st, T, cooling);
                double Tn = T - g / dg;
                if (Tn < 0.5 * T)
                    Tn = 0.5 * T;
                if (Tn > 2.0 * T)
                    Tn = 2.0 * T;
                if (std::abs(Tn - T) < 1e-9 * std::max(T, 1e-3)) {
                    T = Tn;
                    break;
                }
                T = Tn;
            }
            // A stage cannot cool below the temperature its law reaches at this load.
            double floorT = temperatureForLoad(st, load, cooling);
            if (T < Told && T < floorT)
                T = std::min(Told, floorT);
            T = std::clamp(T, 0.005, 300.0);
            if (!cooling.circulating && i >= 3) {
                // No dilution cooling: the still, cold plate and mixing chamber are pre-cooled
                // through the heat switches / exchanger gas toward the PT2 plate with a ~2 h time
                // constant.
                T = Told + (T_[2] - Told) * (1.0 - std::exp(-h / 7200.0));
                T = std::max(T, T_[2]);
            }
            T_[i] = T;
        }
        time_s_ += h;
        remaining -= h;
    }
    return evaluate(T_, false);
}

} // namespace qlab::cryo
