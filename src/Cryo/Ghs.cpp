#include "Cryo/Ghs.hpp"
#include "Cryo/Physics.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::cryo {

GasHandlingSystem::GasHandlingSystem() {
    snap_.pumps = {
        {PumpKind::Scroll, "scroll_backing", false, 0, 20, 1e-2},
        {PumpKind::Turbo, "turbo_ovc", false, 0, 90, 1e-7},
        {PumpKind::Turbo, "turbo_still", false, 0, 90, 1e-4},
        {PumpKind::Compressor, "compressor_3he", false, 0, 15, 0},
    };
    snap_.valves = {{"v_ovc_pump", false}, {"v_still_pump", false}, {"v_condense", false}, {"v_dump", false}, {"v_bypass", false}};
}
Pump* GasHandlingSystem::pump(const std::string& id) { for (auto& p : snap_.pumps) if (p.id == id) return &p; return nullptr; }
Valve* GasHandlingSystem::valve(const std::string& id) { for (auto& v : snap_.valves) if (v.id == id) return &v; return nullptr; }
void GasHandlingSystem::setPump(const std::string& id, bool on) { if (auto* p = pump(id)) p->on = on; }
void GasHandlingSystem::setValve(const std::string& id, bool open) { if (auto* v = valve(id)) v->open = open; }

bool GasHandlingSystem::circulationReady() const {
    auto pc = std::find_if(snap_.pumps.begin(), snap_.pumps.end(), [](const Pump& p) { return p.id == "compressor_3he"; });
    auto ps = std::find_if(snap_.pumps.begin(), snap_.pumps.end(), [](const Pump& p) { return p.id == "turbo_still"; });
    auto vc = std::find_if(snap_.valves.begin(), snap_.valves.end(), [](const Valve& v) { return v.id == "v_condense"; });
    auto vs = std::find_if(snap_.valves.begin(), snap_.valves.end(), [](const Valve& v) { return v.id == "v_still_pump"; });
    return pc->speed_frac > 0.9 && ps->speed_frac > 0.9 && vc->open && vs->open && snap_.mixtureCondensed_frac > 0.5;
}

void GasHandlingSystem::step(double dt, double stillPower_W, double T_still) {
    snap_.time_s += dt;
    for (auto& p : snap_.pumps) {
        double target = p.on ? 1.0 : 0.0;
        p.speed_frac += (target - p.speed_frac) * (1.0 - std::exp(-dt / p.tau_s));
    }
    auto scroll = pump("scroll_backing"), turboOvc = pump("turbo_ovc"), turboStill = pump("turbo_still"), comp = pump("compressor_3he");
    // OVC pressure: exponential pump-down to the ultimate of the active pump when the valve is open;
    // slow leak-up (1e-6 mbar/s) otherwise.
    double ultimate = 1000.0;
    if (valve("v_ovc_pump")->open) {
        if (turboOvc->speed_frac > 0.5 && scroll->speed_frac > 0.5) ultimate = turboOvc->ultimate_mbar;
        else if (scroll->speed_frac > 0.5) ultimate = scroll->ultimate_mbar;
    }
    if (ultimate < snap_.p_ovc_mbar) {
        double rate = (turboOvc->speed_frac > 0.5 ? 1.0 / 600.0 : 1.0 / 900.0); // pump-down time constants
        snap_.p_ovc_mbar = ultimate + (snap_.p_ovc_mbar - ultimate) * std::exp(-dt * rate);
    } else snap_.p_ovc_mbar = std::min(1000.0, snap_.p_ovc_mbar + 1e-6 * dt);
    // Condensation: with the compressor running and the condense valve open, mixture moves from the
    // dump into the fridge over ~4 h once the still is below ~2 K.
    // Condensation happens by Joule-Thomson expansion of the incoming gas against the returning
    // cold stream; it only needs the 4 K stage (not an already-cold still) to be cold enough to
    // start the JT effect (spec 11 §8).
    bool condensing = comp->speed_frac > 0.5 && valve("v_condense")->open && T_still < 4.5;
    if (condensing) snap_.mixtureCondensed_frac = std::min(1.0, snap_.mixtureCondensed_frac + dt / (4.0 * 3600.0));
    if (valve("v_dump")->open && comp->speed_frac > 0.5) snap_.mixtureCondensed_frac = std::max(0.0, snap_.mixtureCondensed_frac - dt / (6.0 * 3600.0));
    snap_.p_dump_mbar = 2500.0 * (1.0 - 0.8 * snap_.mixtureCondensed_frac);
    snap_.p_condense_mbar = condensing ? 300.0 + 700.0 * (1.0 - snap_.mixtureCondensed_frac) : 0.0;
    // Circulation: still pumping delivers n3 set by the still power (L3 ≈ 25 J/mol), only once the
    // mixture is condensed and the still line is pumped.
    bool stillPumped = turboStill->speed_frac > 0.5 && valve("v_still_pump")->open;
    if (stillPumped && snap_.mixtureCondensed_frac > 0.5 && comp->speed_frac > 0.5) {
        double target = std::clamp(stillPower_W / phys::kHe3LatentHeat_Jmol, 0.0, 3e-3);
        snap_.n3_mol_s += (target - snap_.n3_mol_s) * (1.0 - std::exp(-dt / 120.0));
        snap_.p_still_mbar = 0.05 + 0.3 * snap_.n3_mol_s / 1e-3;
    } else {
        snap_.n3_mol_s *= std::exp(-dt / 60.0);
        snap_.p_still_mbar = stillPumped ? 1e-3 : std::min(1000.0, snap_.p_still_mbar + 1e-5 * dt);
    }
}

} // namespace qlab::cryo
