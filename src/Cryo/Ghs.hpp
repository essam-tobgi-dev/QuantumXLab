#pragma once
// Spec 11 §8 — gas-handling system: pumps, valves, pressures, circulation rate.
#include <string>
#include <vector>

namespace qlab::cryo {

enum class PumpKind { Turbo, Scroll, Compressor };

struct Pump {
    PumpKind kind;
    std::string id; // "turbo_ovc", "scroll_backing", "compressor_3he"
    bool on = false;
    double speed_frac = 0;       // 0..1, spins up/down with a time constant
    double tau_s = 30;           // spin-up time constant
    double ultimate_mbar = 1e-7; // for vacuum pumps
};

struct Valve {
    std::string id;
    bool open = false;
};

struct GhsSnapshot {
    double time_s = 0;
    double p_ovc_mbar = 1000;   // outer vacuum can
    double p_still_mbar = 1000; // still pumping line
    double p_condense_mbar = 0; // condensing line (3He return)
    double p_dump_mbar = 2500;  // mixture dump tanks
    double n3_mol_s = 0;        // circulation rate delivered to the network
    double mixtureCondensed_frac = 0;
    std::vector<Pump> pumps;
    std::vector<Valve> valves;
    bool compressorFault = false;
    bool ln2TrapCold = false;
};

class GasHandlingSystem {
  public:
    GasHandlingSystem();
    void setPump(const std::string& id, bool on);
    void setValve(const std::string& id, bool open);
    void setLn2Trap(bool cold) { snap_.ln2TrapCold = cold; }
    // Advance pump speeds, pressures, and the circulation rate (depends on still power).
    void step(double dt_s, double stillPower_W, double T_still_K);
    const GhsSnapshot& snapshot() const { return snap_; }
    bool ovcAtVacuum() const { return snap_.p_ovc_mbar < 1e-4; }
    bool circulationReady() const;

  private:
    Pump* pump(const std::string& id);
    Valve* valve(const std::string& id);
    GhsSnapshot snap_;
};

} // namespace qlab::cryo
