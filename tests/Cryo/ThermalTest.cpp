#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Cryo/Cryo.hpp"
#include "Core/Paths.hpp"
#include <cmath>
using namespace qlab;
using namespace qlab::cryo;
using Catch::Approx;

namespace {
struct Fixture {
    MaterialCatalog mats;
    CoaxCatalog coax;
    Wiring wiring;
    HeatLoadModel loads;
    NoiseBudget noise;
    Fixture() : mats(*MaterialCatalog::load()), loads(coax, mats), noise(coax) {
        auto w = loadWiring(core::assetDir() / "Devices/_template/wiring.json");
        REQUIRE(w);
        wiring = *w;
    }
    StageArray nominalT() const { StageArray T{}; for (Stage s : kStages) T[stageIndex(s)] = nominalTemperature(s); return T; }
};
} // namespace

TEST_CASE("steady state with the 27-qubit reference wiring reaches nominal stage temperatures") {
    Fixture f;
    ThermalNetwork net(f.wiring, f.loads, f.mats);
    auto s = net.steadyState();
    REQUIRE(s);
    INFO("T = " << s->T_K[1] << " " << s->T_K[2] << " " << s->T_K[3] << " " << s->T_K[4] << " " << s->T_K[5]);
    INFO("loads = " << s->load_W[1] << " " << s->load_W[2] << " " << s->load_W[3] << " " << s->load_W[4] << " " << s->load_W[5]);
    REQUIRE(s->steady);
    REQUIRE(s->T_K[stageIndex(Stage::PT1)] > 30.0); REQUIRE(s->T_K[stageIndex(Stage::PT1)] < 60.0);
    REQUIRE(s->T_K[stageIndex(Stage::PT2)] > 2.8);  REQUIRE(s->T_K[stageIndex(Stage::PT2)] < 4.5);
    REQUIRE(s->T_K[stageIndex(Stage::STILL)] < 1.2);
    REQUIRE(s->T_K[stageIndex(Stage::CP)] < 0.2);
    double Tmxc = s->T_K[stageIndex(Stage::MXC)];
    REQUIRE(Tmxc > 0.010); REQUIRE(Tmxc < 0.025);
    // Spec 11 worked example: PT2 load for ~40 coax lines + 4 HEMTs is a few hundred mW, below 1.5 W.
    REQUIRE(s->load_W[stageIndex(Stage::PT2)] > 0.05);
    REQUIRE(s->load_W[stageIndex(Stage::PT2)] < 1.5);
    REQUIRE(s->margin_W[stageIndex(Stage::PT2)] > 0);
    // Doubling the wiring raises the mixing-chamber temperature.
    Wiring big = f.wiring;
    for (auto l : f.wiring.lines) { l.id += "_b"; l.channel += "_b"; big.lines.push_back(l); }
    ThermalNetwork net2(big, f.loads, f.mats);
    auto s2 = net2.steadyState();
    REQUIRE(s2);
    REQUIRE(s2->T_K[stageIndex(Stage::MXC)] > Tmxc);
    REQUIRE(s2->load_W[stageIndex(Stage::PT2)] > s->load_W[stageIndex(Stage::PT2)]);
}

TEST_CASE("applied RF power heats the attenuators at the mixing chamber") {
    Fixture f;
    ThermalNetwork net(f.wiring, f.loads, f.mats);
    auto s0 = net.steadyState(); REQUIRE(s0);
    net.linePowers["drive_q0"] = 1e-3; // 0 dBm continuous at the bulkhead
    auto s1 = net.steadyState(); REQUIRE(s1);
    // 0 dBm at the bulkhead: the PT2 attenuator dissipates most of the 1 mW input power, minus the
    // (small, frequency- and temperature-scaled) SS coax loss through PT1 ahead of it.
    double extraPT2 = s1->loads[stageIndex(Stage::PT2)].dissipation_W - s0->loads[stageIndex(Stage::PT2)].dissipation_W;
    REQUIRE(extraPT2 > 0.3e-3);
    REQUIRE(extraPT2 < 1.0e-3);
    double extra = s1->loads[stageIndex(Stage::MXC)].dissipation_W - s0->loads[stageIndex(Stage::MXC)].dissipation_W;
    REQUIRE(extra > 3e-6); REQUIRE(extra < 1e-5);
    REQUIRE(s1->T_K[stageIndex(Stage::MXC)] > s0->T_K[stageIndex(Stage::MXC)]);
}

TEST_CASE("photon-number budget reproduces the T07 §9 tables") {
    Fixture f;
    StageArray T = f.nominalT();
    NoiseBudgetOptions opt; opt.f_Hz = 5e9; opt.n_in = 1250.0;
    auto d = ChainCatalog::instantiate("drive_std", "d", "d[0]"); REQUIRE(d);
    LineNoise ln = f.noise.inputLine(*d, T, opt);
    REQUIRE(ln.n_chip == Approx(0.264).epsilon(0.02));
    REQUIRE(ln.T_eff_K == Approx(0.153).epsilon(0.02));
    auto r = ChainCatalog::instantiate("readout_in_std", "r", "m[0]"); REQUIRE(r);
    LineNoise rn = f.noise.inputLine(*r, T, opt);
    REQUIRE(rn.n_chip == Approx(1.5e-3).epsilon(0.05));
    REQUIRE(rn.T_eff_K == Approx(0.037).epsilon(0.05));
    auto d60 = ChainCatalog::instantiate("drive_60dB", "d60", "d[1]"); REQUIRE(d60);
    LineNoise n60 = f.noise.inputLine(*d60, T, opt);
    REQUIRE(n60.n_chip == Approx(3.6e-3).epsilon(0.05)); // 20 dB at CP variant (T07 §9)
    // Cable loss can only lower the delivered photon number.
    opt.includeCableLoss = true;
    REQUIRE(f.noise.inputLine(*d, T, opt).n_chip <= ln.n_chip * 1.0001);
    REQUIRE(ln.totalAttenuation_dB == Approx(40.0));
}

TEST_CASE("Friis system noise temperature of the output chain") {
    Fixture f;
    StageArray T = f.nominalT();
    const WiringLine* out = f.wiring.find("readout_out_f0"); REQUIRE(out);
    OutputChainNoise o = f.noise.outputLine(*out, T, 5e9);
    // TWPA (T_N 0.35 K, 20 dB) first: T_sys ≈ 0.35 + 2.5/100 + small losses ≈ 0.4 K.
    REQUIRE(o.T_sys_K > 0.3); REQUIRE(o.T_sys_K < 0.8);
    REQUIRE(o.gainTotal_dB > 80.0);
    // Without the preamp: HEMT dominates, T_sys ≈ 2.5 K + isolator/cable losses.
    auto plain = ChainCatalog::instantiate("readout_out_std", "o", "a[0]"); REQUIRE(plain);
    OutputChainNoise p = f.noise.outputLine(*plain, T, 5e9);
    REQUIRE(p.T_sys_K > 2.4); REQUIRE(p.T_sys_K < 4.0);
    REQUIRE(p.quantumEfficiency < o.quantumEfficiency);
    // Back-action through two 20 dB isolators: n_th(3.5 K) × 1e-4 + n_th(15 mK) ≈ 1.4e-3.
    REQUIRE(p.n_backaction == Approx(1.4e-3).epsilon(0.15));
}

TEST_CASE("transient cooldown follows the published timeline order of magnitude") {
    Fixture f;
    ThermalNetwork net(f.wiring, f.loads, f.mats);
    GasHandlingSystem ghs;
    CooldownSequencer seq(net, ghs);
    seq.startCooldown();
    double t = 0, tPT2cold = -1, tBase = -1;
    const double dt = 60.0;
    while (t < 72.0 * 3600.0) {
        auto s = seq.step(dt); REQUIRE(s);
        t += dt;
        const auto& T = net.temperatures();
        if (tPT2cold < 0 && T[stageIndex(Stage::PT2)] < 4.5) tPT2cold = t;
        if (tBase < 0 && T[stageIndex(Stage::MXC)] < 0.030) { tBase = t; break; }
    }
    INFO("PT2 < 4.5 K after " << tPT2cold / 3600 << " h; base after " << tBase / 3600 << " h");
    REQUIRE(tPT2cold > 0);
    REQUIRE(tPT2cold / 3600.0 > 8.0);
    REQUIRE(tPT2cold / 3600.0 < 48.0);
    REQUIRE(tBase > tPT2cold);
    REQUIRE(seq.state() == FridgeState::Base);
    REQUIRE(net.temperatures()[stageIndex(Stage::MXC)] < 0.030);
}
