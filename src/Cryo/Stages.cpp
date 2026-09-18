#include "Cryo/Stages.hpp"
#include "Cryo/Physics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <array>
#include <utility>

namespace qlab::cryo {

namespace {

// Pulse-tube capacity above the operating point (T08 §2). The spec's linear load lines
// (40 W (T-30)/15 and 1.5 W (T-2.8)/1.4) describe the *operating* region and are used
// unchanged below 45 K / 4.2 K; during a cooldown a PT415-class machine removes far more
// heat at high temperature, and the descent time depends entirely on that part of the map.
// Anchors: 1st stage 40 W at 45 K and ~100 W at 100 K (T08 §2 table, "up to ~100 W at 100 K
// during cooldown"); 2nd stage 1.5 W at 4.2 K rising to a few tens of watts once the stage is
// above ~40 K. Each table starts at the operating point so the law is continuous there.
using CapacityPoint = std::pair<double, double>; // (T K, cooling W)

constexpr std::array<CapacityPoint, 8> kPt1Cooldown{{{45.0, 40.0},
                                                     {60.0, 55.0},
                                                     {80.0, 75.0},
                                                     {100.0, 100.0},
                                                     {150.0, 145.0},
                                                     {200.0, 185.0},
                                                     {250.0, 220.0},
                                                     {300.0, 250.0}}};

constexpr std::array<CapacityPoint, 8> kPt2Cooldown{{{4.2, 1.5},
                                                     {6.0, 3.0},
                                                     {10.0, 8.0},
                                                     {20.0, 18.0},
                                                     {40.0, 32.0},
                                                     {80.0, 45.0},
                                                     {150.0, 55.0},
                                                     {300.0, 65.0}}};

template <std::size_t N>
double interpolate(const std::array<CapacityPoint, N>& tab, double T) {
    if (T <= tab.front().first) return tab.front().second;
    if (T >= tab.back().first) return tab.back().second;
    for (std::size_t i = 1; i < N; ++i)
        if (T <= tab[i].first) {
            double f = (T - tab[i - 1].first) / (tab[i].first - tab[i - 1].first);
            return tab[i - 1].second + f * (tab[i].second - tab[i - 1].second);
        }
    return tab.back().second;
}

template <std::size_t N>
double slope(const std::array<CapacityPoint, N>& tab, double T) {
    if (T >= tab.back().first) return 0.0;
    for (std::size_t i = 1; i < N; ++i)
        if (T <= tab[i].first)
            return (tab[i].second - tab[i - 1].second) / (tab[i].first - tab[i - 1].first);
    return 0.0;
}

// Inverse map: temperature at which the capacity equals `load`. Beyond the table's last point
// the stage cannot be held at all; extrapolate on the final slope so the diagnostic reports a
// finite (large) temperature rather than infinity.
template <std::size_t N>
double invert(const std::array<CapacityPoint, N>& tab, double load) {
    if (load <= tab.front().second) return tab.front().first;
    for (std::size_t i = 1; i < N; ++i)
        if (load <= tab[i].second) {
            double f = (load - tab[i - 1].second) / (tab[i].second - tab[i - 1].second);
            return tab[i - 1].first + f * (tab[i].first - tab[i - 1].first);
        }
    double m = (tab[N - 1].second - tab[N - 2].second) / (tab[N - 1].first - tab[N - 2].first);
    return tab.back().first + (load - tab.back().second) / std::max(m, 1e-6);
}

} // namespace

std::string_view stageName(Stage s) {
    switch (s) {
    case Stage::RT: return "RT";
    case Stage::PT1: return "PT1";
    case Stage::PT2: return "PT2";
    case Stage::STILL: return "STILL";
    case Stage::CP: return "CP";
    case Stage::MXC: return "MXC";
    }
    return "?";
}
std::string_view stageLongName(Stage s) {
    switch (s) {
    case Stage::RT: return "Room temperature (top plate)";
    case Stage::PT1: return "50 K plate (pulse tube 1st stage)";
    case Stage::PT2: return "4 K plate (pulse tube 2nd stage)";
    case Stage::STILL: return "Still";
    case Stage::CP: return "Cold plate";
    case Stage::MXC: return "Mixing chamber";
    }
    return "?";
}
bool stageFromName(std::string_view name, Stage& out) {
    for (Stage s : kStages)
        if (stageName(s) == name) { out = s; return true; }
    if (name == "50K") { out = Stage::PT1; return true; }
    if (name == "4K") { out = Stage::PT2; return true; }
    return false;
}
double nominalTemperature(Stage s) {
    switch (s) {
    case Stage::RT: return 293.0;
    case Stage::PT1: return 45.0;
    case Stage::PT2: return 3.5;
    case Stage::STILL: return 0.85;
    case Stage::CP: return 0.10;
    case Stage::MXC: return 0.015;
    }
    return 293.0;
}

double coolingPower(Stage s, double T, const CoolingParams& p) {
    switch (s) {
    case Stage::RT: return std::numeric_limits<double>::infinity();
    // Spec 11 §1 linear load line in the operating region (below 45 K / 4.2 K); above it the
    // measured cooldown capacity map of §kPt1Cooldown / kPt2Cooldown (T08 §2).
    case Stage::PT1: return p.pulseTubeOn ? (T <= 45.0 ? std::max(0.0, 40.0 * (T - 30.0) / 15.0) : interpolate(kPt1Cooldown, T)) : 0.0;
    case Stage::PT2: return p.pulseTubeOn ? (T <= 4.2 ? std::max(0.0, 1.5 * (T - 2.8) / 1.4) : interpolate(kPt2Cooldown, T)) : 0.0;
    case Stage::STILL: {
        if (!p.circulating) return 0.0;
        // 3He evaporation: n3 L3 minus film burden; the still self-regulates near 0.85 K, we add a
        // weak T dependence so the solve is well-posed (evaporation rate rises with T).
        double base = p.n3_mol_s * phys::kHe3LatentHeat_Jmol - p.filmBurden_W;
        return std::max(0.0, base * std::max(0.0, T / 0.85));
    }
    case Stage::CP: return p.circulating ? 0.35 * p.n3_mol_s * 84.0 * T * T : 0.0;
    case Stage::MXC: return p.circulating ? 84.0 * p.n3_mol_s * T * T - p.Q0_W : 0.0;
    }
    return 0.0;
}

double coolingPowerDerivative(Stage s, double T, const CoolingParams& p) {
    switch (s) {
    case Stage::RT: return 0.0;
    case Stage::PT1: return p.pulseTubeOn ? (T <= 45.0 ? (T > 30.0 ? 40.0 / 15.0 : 0.0) : slope(kPt1Cooldown, T)) : 0.0;
    case Stage::PT2: return p.pulseTubeOn ? (T <= 4.2 ? (T > 2.8 ? 1.5 / 1.4 : 0.0) : slope(kPt2Cooldown, T)) : 0.0;
    case Stage::STILL: return p.circulating ? std::max(0.0, (p.n3_mol_s * phys::kHe3LatentHeat_Jmol - p.filmBurden_W) / 0.85) : 0.0;
    case Stage::CP: return p.circulating ? 2.0 * 0.35 * p.n3_mol_s * 84.0 * T : 0.0;
    case Stage::MXC: return p.circulating ? 2.0 * 84.0 * p.n3_mol_s * T : 0.0;
    }
    return 0.0;
}

double temperatureForLoad(Stage s, double load, const CoolingParams& p) {
    load = std::max(0.0, load);
    switch (s) {
    case Stage::RT: return nominalTemperature(Stage::RT);
    case Stage::PT1: return p.pulseTubeOn ? (load <= 40.0 ? 30.0 + 15.0 * load / 40.0 : invert(kPt1Cooldown, load)) : 293.0;
    case Stage::PT2: return p.pulseTubeOn ? (load <= 1.5 ? 2.8 + 1.4 * load / 1.5 : invert(kPt2Cooldown, load)) : 293.0;
    case Stage::STILL: {
        double base = p.n3_mol_s * phys::kHe3LatentHeat_Jmol - p.filmBurden_W;
        if (!p.circulating || base <= 0) return 4.0;
        return 0.85 * std::max(load, 1e-6) / base;
    }
    case Stage::CP: {
        if (!p.circulating) return 4.0;
        return std::sqrt(std::max(load, 1e-12) / (0.35 * p.n3_mol_s * 84.0));
    }
    case Stage::MXC: {
        if (!p.circulating) return 4.0;
        return std::sqrt((load + p.Q0_W) / (84.0 * p.n3_mol_s));
    }
    }
    return 293.0;
}

} // namespace qlab::cryo
