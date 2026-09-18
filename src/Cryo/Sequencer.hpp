#pragma once
// Spec 11 §8, T08 §7 — cooldown / warm-up sequencer driving the GHS and thermal network.
#include "Cryo/Ghs.hpp"
#include "Cryo/ThermalNetwork.hpp"
#include <string_view>

namespace qlab::cryo {

enum class FridgeState { Warm, Pumping, Precooling, Condensing, Base, Warming };
std::string_view fridgeStateName(FridgeState s);

class CooldownSequencer {
public:
    CooldownSequencer(ThermalNetwork& net, GasHandlingSystem& ghs) : net_(net), ghs_(ghs) {}
    void startCooldown();
    void startWarmup();
    // Advance the whole system by dt (seconds of simulated time); returns the thermal snapshot.
    Result<ThermalSnapshot> step(double dt_s);
    FridgeState state() const { return state_; }
    double elapsed_s() const { return elapsed_; }
    const ThermalSnapshot& last() const { return last_; }
private:
    ThermalNetwork& net_;
    GasHandlingSystem& ghs_;
    FridgeState state_ = FridgeState::Warm;
    double elapsed_ = 0;
    ThermalSnapshot last_;
};

} // namespace qlab::cryo
