#include "Cryo/Sequencer.hpp"

namespace qlab::cryo {

std::string_view fridgeStateName(FridgeState s) {
    switch (s) {
    case FridgeState::Warm: return "warm";
    case FridgeState::Pumping: return "pumping";
    case FridgeState::Precooling: return "precooling";
    case FridgeState::Condensing: return "condensing";
    case FridgeState::Base: return "base";
    case FridgeState::Warming: return "warming";
    }
    return "?";
}

void CooldownSequencer::startCooldown() {
    net_.warmToRoomTemperature();
    net_.cooling.pulseTubeOn = false;
    net_.cooling.circulating = false;
    ghs_.setPump("scroll_backing", true);
    ghs_.setValve("v_ovc_pump", true);
    ghs_.setPump("turbo_ovc", true);
    state_ = FridgeState::Pumping;
    elapsed_ = 0;
}

void CooldownSequencer::startWarmup() {
    net_.cooling.circulating = false;
    net_.cooling.pulseTubeOn = false;
    ghs_.setValve("v_condense", false);
    ghs_.setValve("v_dump", true);
    ghs_.setValve("v_still_pump", false);
    ghs_.setPump("turbo_still", false);
    state_ = FridgeState::Warming;
}

Result<ThermalSnapshot> CooldownSequencer::step(double dt) {
    elapsed_ += dt;
    const auto& T = net_.temperatures();
    switch (state_) {
    case FridgeState::Warm: break;
    case FridgeState::Pumping:
        if (ghs_.ovcAtVacuum()) { net_.cooling.pulseTubeOn = true; state_ = FridgeState::Precooling; }
        break;
    case FridgeState::Precooling:
        if (T[stageIndex(Stage::PT2)] < 4.5 && T[stageIndex(Stage::STILL)] < 5.0) {
            ghs_.setPump("compressor_3he", true);
            ghs_.setValve("v_condense", true);
            ghs_.setPump("turbo_still", true);
            ghs_.setValve("v_still_pump", true);
            state_ = FridgeState::Condensing;
        }
        break;
    case FridgeState::Condensing:
        if (ghs_.circulationReady()) {
            net_.cooling.circulating = true;
            // Raise the still heater to its normal operating power now that the mixture has
            // condensed, driving the circulation rate up to the design set point (spec 11 §1).
            net_.cooling.stillHeater_W = 25e-3;
            state_ = FridgeState::Base;
        }
        break;
    case FridgeState::Base: break;
    case FridgeState::Warming:
        if (T[stageIndex(Stage::PT2)] > 280.0) { state_ = FridgeState::Warm; ghs_.setPump("turbo_ovc", false); ghs_.setPump("scroll_backing", false); }
        break;
    }
    ghs_.step(dt, net_.cooling.stillHeater_W, T[stageIndex(Stage::STILL)]);
    if (state_ == FridgeState::Base || state_ == FridgeState::Condensing) net_.cooling.n3_mol_s = std::max(ghs_.snapshot().n3_mol_s, state_ == FridgeState::Base ? 2e-4 : 0.0);
    auto s = net_.step(dt);
    if (s) last_ = *s;
    return s;
}

} // namespace qlab::cryo
