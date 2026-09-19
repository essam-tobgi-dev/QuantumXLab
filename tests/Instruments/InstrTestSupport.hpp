#pragma once
// Shared fixtures of the Instruments tests: the shipped sc_fixed_5 device as an instr::Environment,
// schedules built from its pulse library, and small numeric helpers.
#include "Instruments/Instruments.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace instrtest {

using namespace qlab;

// Device, calibration and wiring of a shipped transmon device at nominal stage temperatures.
inline instr::Environment deviceEnvironment(const std::string& id = "sc_fixed_5") {
    static std::map<std::string, instr::Environment> cache;
    if (auto it = cache.find(id); it != cache.end())
        return it->second;
    auto loaded = hw::loadShippedDevice(id);
    REQUIRE(loaded);
    instr::Environment env;
    env.device = std::make_shared<const hw::Device>(loaded->device);
    env.calibration = std::make_shared<const hw::Calibration>(loaded->calibration);
    auto wiring = cryo::loadWiring(loaded->device.directory / "wiring.json");
    REQUIRE(wiring);
    env.wiring = std::make_shared<const cryo::Wiring>(std::move(*wiring));
    for (cryo::Stage s : cryo::kStages)
        env.thermal.T_K[static_cast<std::size_t>(cryo::stageIndex(s))] =
            cryo::nominalTemperature(s);
    cache.emplace(id, env);
    return env;
}

// The same environment with the readout output line's preamp set to "none", "twpa" or "jpa".
inline instr::Environment withPreamp(instr::Environment env, const std::string& preamp) {
    auto wiring = std::make_shared<cryo::Wiring>(*env.wiring);
    for (auto& line : wiring->lines) {
        if (line.kind != cryo::LineKind::ReadoutOut)
            continue;
        std::erase_if(line.elements,
                      [](const cryo::Element& e) { return e.kind == cryo::ElementKind::Preamp; });
        line.preamp = preamp;
        if (preamp == "none")
            continue;
        cryo::Element p; // same insertion as cryo::parseWiring: after the first isolator
        p.kind = cryo::ElementKind::Preamp;
        p.stage = cryo::Stage::MXC;
        p.variant = preamp;
        p.id = preamp;
        p.gain_dB = preamp == "twpa" ? 20.0 : 18.0;
        p.noiseTemperature_K = preamp == "twpa" ? 0.35 : 0.25;
        p.dissipation_W = 0.5e-6;
        auto pos =
            std::find_if(line.elements.begin(), line.elements.end(), [](const cryo::Element& e) {
                return e.kind == cryo::ElementKind::Isolator;
            });
        line.elements.insert(pos == line.elements.end() ? line.elements.end() : pos + 1, p);
    }
    env.wiring = wiring;
    return env;
}

inline const pulse::PulseLibrary& library(const std::string& id = "sc_fixed_5") {
    static std::map<std::string, pulse::PulseLibrary> cache;
    auto it = cache.find(id);
    if (it == cache.end()) {
        auto lib = pulse::loadPulses(hw::deviceRoot() / id);
        REQUIRE(lib);
        it = cache.emplace(id, std::move(*lib)).first;
    }
    return it->second;
}

// A schedule holding one constant-envelope play of `durationS` on d[qubit] (a CW test tone).
inline std::shared_ptr<const pulse::Schedule> toneSchedule(double durationS, double amplitude = 1.0,
                                                           std::uint32_t qubit = 0,
                                                           double phase = 0.0) {
    auto s = std::make_shared<pulse::Schedule>(Picoseconds{1000});
    s->insert(pulse::Play{pulse::ChannelId::drive(qubit),
                          pulse::Waveform::constant(durationS, amplitude, phase), Picoseconds{0}},
              Picoseconds{0});
    return s;
}

// Hub + powered-on instrument helpers.
inline std::shared_ptr<instr::InputHub> makeHub(instr::Environment env, instr::RunView run = {}) {
    auto hub = std::make_shared<instr::InputHub>();
    hub->publish(std::move(env));
    hub->publish(std::move(run));
    return hub;
}

inline void powerOn(instr::IInstrument& i) {
    REQUIRE(i.execute(instr::Command::of(instr::Command::Kind::PowerOn)));
}

inline double mean(const std::vector<double>& v) {
    double s = 0.0;
    for (double x : v)
        s += x;
    return v.empty() ? 0.0 : s / static_cast<double>(v.size());
}
inline double stddev(const std::vector<double>& v) {
    const double m = mean(v);
    double s = 0.0;
    for (double x : v)
        s += (x - m) * (x - m);
    return v.size() > 1 ? std::sqrt(s / static_cast<double>(v.size() - 1)) : 0.0;
}

} // namespace instrtest
