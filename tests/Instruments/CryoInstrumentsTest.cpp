// Spec 12 §8, §15; spec 25 §3.7; T08 §8 — thermometry (calibration curves, tracking within noise,
// self-heating), pressure and flow from the gas-handling state, RF power at a routed node.
#include "InstrTestSupport.hpp"

#include <catch2/catch_approx.hpp>

#include <numbers>

using namespace qlab;
using namespace qlab::instr;
using Catch::Approx;
using instrtest::powerOn;

namespace {
Environment fridge(double mxcK = 0.015) {
    Environment env;
    for (cryo::Stage s : cryo::kStages)
        env.thermal.T_K[static_cast<std::size_t>(cryo::stageIndex(s))] =
            cryo::nominalTemperature(s);
    env.thermal.T_K[static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::MXC))] = mxcK;
    return env;
}

// `n` readings of channel T; the hub's lab clock advances 0.1 s per reading.
std::vector<double> readings(Thermometer& th, InputHub& hub, Environment env, std::size_t n) {
    std::vector<double> out;
    for (std::size_t k = 0; k < n; ++k) {
        env.labTimeS += 0.1;
        hub.publish(env);
        auto t = acquire(th, "T");
        REQUIRE(t);
        out.push_back(t->y.back());
    }
    return out;
}
} // namespace

TEST_CASE("thermometer calibration curves invert to the temperature") {
    for (SensorKind kind : {SensorKind::RuO2, SensorKind::Cernox}) {
        const SensorCurve curve{kind};
        double previous = std::numeric_limits<double>::infinity();
        for (int k = 0; k <= 200; ++k) { // log-spaced over the calibrated range
            const double T = curve.minK() * std::pow(curve.maxK() / curve.minK(), k / 200.0);
            const double R = curve.resistanceOhm(T);
            REQUIRE(R < previous); // negative temperature coefficient, monotonic
            previous = R;
            REQUIRE(curve.temperatureK(R) ==
                    Approx(T).epsilon(1e-9)); // spec 25 §3.7 asks for 0.1 %
            // sensitivity = −d ln R / d ln T, against a central difference
            const double h = 1e-5;
            const double numeric = -(std::log(curve.resistanceOhm(T * (1 + h))) -
                                     std::log(curve.resistanceOhm(T * (1 - h)))) /
                                   (2 * h);
            REQUIRE(curve.sensitivity(T) == Approx(numeric).epsilon(1e-5));
        }
    }
    // RuO₂: R ∝ exp[(T0/T)^{1/4}] rises steeply toward base (T08 §8); tens of kΩ at 10 mK.
    const SensorCurve ruo2{SensorKind::RuO2};
    REQUIRE(ruo2.resistanceOhm(0.010) > 20e3);
    REQUIRE(ruo2.resistanceOhm(4.0) < 3e3);
    REQUIRE(std::log(ruo2.resistanceOhm(0.02) / 800.0) ==
            Approx(std::pow(2.15 / 0.02, 0.25)).epsilon(1e-12));
}

TEST_CASE("thermometer readings track the cryo snapshot within their noise") {
    auto hub = std::make_shared<InputHub>();
    Thermometer ruo2(SensorKind::RuO2), cernox(SensorKind::Cernox);
    Bindings b;
    b.inputs = hub;
    ruo2.bind(b);
    cernox.bind(b);
    REQUIRE(acquire(ruo2, "T").error().code == err::PoweredOff);
    powerOn(ruo2);
    powerOn(cernox);
    REQUIRE(acquire(ruo2, "T").error().code == err::NotBound); // no thermal snapshot yet
    REQUIRE(ruo2.stage() == cryo::Stage::MXC);
    REQUIRE(cernox.stage() == cryo::Stage::PT2);
    REQUIRE(ruo2.set("history_points", std::int64_t{2000}));

    // MXC at 20 mK, default 0.1 nW excitation: self-heating < 0.01 mK; noise 0.5 % of reading.
    const std::size_t n = 600;
    auto cold = readings(ruo2, *hub, fridge(0.020), n);
    const double expected = Thermometer::sensorTemperature(0.020, 1e-10, 2.77);
    REQUIRE(expected == Approx(0.020).epsilon(1e-3));
    REQUIRE(instrtest::stddev(cold) == Approx(0.005 * expected).epsilon(0.12));
    REQUIRE(instrtest::mean(cold) ==
            Approx(expected).margin(4.0 * 0.005 * expected / std::sqrt(static_cast<double>(n))));
    // The stage warms to 100 mK: the reading follows.
    auto warm = readings(ruo2, *hub, fridge(0.100), n);
    REQUIRE(instrtest::mean(warm) ==
            Approx(0.100).margin(4.0 * 0.005 * 0.1 / std::sqrt(static_cast<double>(n)) + 1e-6));
    auto last = ruo2.lastReading();
    REQUIRE(last->stageK == 0.100);
    REQUIRE(last->sigmaK == Approx(0.005 * last->temperatureK));
    REQUIRE(*ruo2.query("T") == last->temperatureK);
    REQUIRE(*ruo2.query("R") ==
            Approx(ruo2.curve().resistanceOhm(last->temperatureK)).epsilon(1e-9));
    // The strip chart holds the history with its σ; the R channel is the bridge's resistance.
    auto chart = acquire(ruo2, "T");
    REQUIRE(chart->size() == 2 * n + 1);
    REQUIRE(chart->sigma->y.size() == chart->size());
    REQUIRE(chart->cls == FidelityClass::Model);
    REQUIRE(chart->yUnit == "K");
    auto r = acquire(ruo2, "R");
    REQUIRE(r->yUnit == "Ω");
    REQUIRE(r->y.back() == Approx(ruo2.curve().resistanceOhm(0.1)).epsilon(0.03));

    // Cernox on PT2 (3.5 K): 0.1 % of reading.
    auto pt2 = readings(cernox, *hub, fridge(), n);
    REQUIRE(instrtest::mean(pt2) ==
            Approx(3.5).margin(4.0 * 0.001 * 3.5 / std::sqrt(static_cast<double>(n))));
    REQUIRE(instrtest::stddev(pt2) == Approx(0.0035).epsilon(0.12));
    // Out of range (a RuO₂ on a 100 K plate during cooldown) clamps and says so.
    Environment cooldown = fridge();
    cooldown.thermal.T_K[static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::MXC))] = 100.0;
    hub->publish(cooldown);
    auto over = acquire(ruo2, "T");
    REQUIRE(over->y.back() == Approx(40.0).epsilon(0.02));
    REQUIRE(over->marker("out_of_range") != nullptr);
}

TEST_CASE("RuO2 self-heating: 10 nW reads 1-4 mK high at 10 mK, 0.1 nW less than 0.2 mK") {
    // Spec 12 §15. T_s⁴ = T⁴ + 4P/k: ΔT = P R_K with R_K = 1/(k T³) ∝ T⁻³ for small P.
    REQUIRE(Thermometer::sensorTemperature(0.010, 1e-8, 2.77) - 0.010 ==
            Approx(2.5e-3).epsilon(0.02));
    const double small = Thermometer::sensorTemperature(0.050, 1e-12, 2.77) - 0.050;
    REQUIRE(small == Approx(1e-12 / (2.77 * std::pow(0.050, 3))).epsilon(1e-4));
    REQUIRE((Thermometer::sensorTemperature(0.100, 1e-12, 2.77) - 0.100) / small ==
            Approx(1.0 / 8.0).epsilon(1e-3)); // R_K ∝ T⁻³

    auto hub = std::make_shared<InputHub>();
    Thermometer th(SensorKind::RuO2);
    Bindings b;
    b.inputs = hub;
    th.bind(b);
    powerOn(th);
    REQUIRE(th.set("excitation", 1e-8));
    const double hot = instrtest::mean(readings(th, *hub, fridge(0.010), 400)) - 0.010;
    REQUIRE(hot > 1e-3);
    REQUIRE(hot < 4e-3);
    REQUIRE(th.lastReading()->sensorK == Approx(0.0125).epsilon(0.01));
    REQUIRE(th.lastReading()->stageK ==
            0.010); // the truth did not move: only the reading did (T08 §8)
    REQUIRE(th.set("excitation", 1e-10));
    const double gentle = instrtest::mean(readings(th, *hub, fridge(0.010), 400)) - 0.010;
    REQUIRE(std::abs(gentle) < 0.2e-3);
    REQUIRE(th.set("excitation", 1.0)); // far above the 100 nW limit: clamped and reported
    REQUIRE(std::get<double>(th.get("excitation")) == 1e-7);
    REQUIRE(th.reports().size() == 1);
}

TEST_CASE("pressure gauge and flow meter read the gas-handling state") {
    auto hub = std::make_shared<InputHub>();
    Environment env = fridge();
    env.ghs.p_ovc_mbar = 2.0e-6;
    env.ghs.p_still_mbar = 0.12;
    env.ghs.p_condense_mbar = 350.0;
    env.ghs.n3_mol_s = 0.8e-3;
    hub->publish(env);
    PressureGauge gauge;
    FlowMeter flow;
    Bindings b;
    b.inputs = hub;
    gauge.bind(b);
    flow.bind(b);
    powerOn(gauge);
    powerOn(flow);

    std::vector<double> ovc;
    for (int k = 0; k < 300; ++k)
        ovc.push_back(acquire(gauge, "p")->y.back());
    REQUIRE(PressureGauge::technology("ovc", 2e-6) == "cold_cathode");
    REQUIRE(PressureGauge::technology("ovc", 0.5) == "pirani");
    REQUIRE(PressureGauge::technology("still", 0.12) == "capacitance");
    REQUIRE(instrtest::mean(ovc) == Approx(2.0e-6).epsilon(0.03));
    REQUIRE(instrtest::stddev(ovc) ==
            Approx(0.10 * 2.0e-6).epsilon(0.2)); // cold cathode: 10 % of reading
    REQUIRE(gauge.set("node", std::string("still")));
    auto still = acquire(gauge, "p");
    REQUIRE(still->y.back() == Approx(0.12).epsilon(0.01)); // capacitance manometer: 0.25 %
    REQUIRE(still->yUnit == "mbar");
    REQUIRE(still->marker("capacitance") != nullptr);
    REQUIRE(*gauge.query("p") == still->y.back());
    env.ghs.p_still_mbar = 1e-12; // below the gauge's range
    hub->publish(env);
    auto under = acquire(gauge, "p");
    REQUIRE(under->y.back() == 1e-8);
    REQUIRE(under->marker("out_of_range") != nullptr);

    std::vector<double> n3;
    for (int k = 0; k < 300; ++k)
        n3.push_back(acquire(flow, "n3")->y.back());
    REQUIRE(instrtest::mean(n3) == Approx(0.8).margin(0.003)); // mmol/s
    REQUIRE(instrtest::stddev(n3) == Approx(0.01 * 0.8 + 0.002).epsilon(0.2));
    REQUIRE(acquire(flow, "n3")->yUnit == "mmol/s");
    env.ghs.n3_mol_s = 9e-3; // beyond the 5 mmol/s full scale
    hub->publish(env);
    auto full = acquire(flow, "n3");
    REQUIRE(full->y.back() == 5.0);
    REQUIRE(full->marker("out_of_range") != nullptr);
}

TEST_CASE("power meter reads 10 log10(P/1 mW) of the routed node") {
    Awg awg;
    Generator lo;
    IqMixer mixer;
    PowerMeter meter;
    SignalGraph graph;
    auto hub = std::make_shared<InputHub>();
    RunView run;
    run.schedule = instrtest::toneSchedule(20e-6, 1.0);
    hub->publish(run);
    hub->publish(Environment{});
    graph.addNode("awg[0].ch[0]", &awg, "ch[0]");
    graph.addNode("sg_mw[0].rf", &lo, "rf");
    graph.addNode("iq_mixer[0].rf", &mixer, "rf");
    graph.addEdge("awg[0].ch[0]", "iq_mixer[0].if");
    graph.addEdge("sg_mw[0].rf", "iq_mixer[0].lo");
    graph.addEdge("iq_mixer[0].rf", "bench.cable", -3.0); // a 3 dB pad on the way to the sensor
    Bindings b;
    b.inputs = hub;
    b.routing = &graph;
    for (IInstrument* i : std::initializer_list<IInstrument*>{&awg, &lo, &mixer, &meter}) {
        i->bind(b);
        powerOn(*i);
    }
    mixer.attach(&awg, 0, &lo);
    REQUIRE(awg.set("sample_rate", 1e9));
    REQUIRE(lo.set("rf_on", true));

    // Carrier V_fs²/2Z0 through 6 dB, plus the −40 dBc leakage and −35 dBc image riding along.
    const double carrierDbm = 10.0 * std::log10(0.25 / 100.0 / 1e-3) - 6.0;
    const double totalDbm = carrierDbm + 10.0 * std::log10(1.0 + 1e-4 + std::pow(10.0, -3.5));
    auto direct = acquire(meter, "p"); // the channel the shipped descriptor names
    REQUIRE(direct);
    REQUIRE(direct->yUnit == "dBm");
    REQUIRE(direct->y.back() == Approx(totalDbm).margin(0.1));
    // Repeatability 0.02 dB at the reference 100 ms, improving as 1/√t; the head's quoted absolute
    // accuracy is systematic and is reported beside the reading, not folded into the scatter.
    REQUIRE(direct->sigma->y.back() == Approx(0.02).epsilon(1e-12));
    REQUIRE(direct->marker("uncertainty")->value == Approx(0.1));
    REQUIRE(PowerMeter::repeatabilityDb(400.0) == Approx(0.01).epsilon(1e-12));
    REQUIRE(meter.set("averaging_ms", 400.0));
    REQUIRE(acquire(meter, "p")->sigma->y.back() == Approx(0.01).epsilon(1e-12));
    REQUIRE(meter.set("averaging_ms", 100.0));
    REQUIRE(meter.set("input", std::string("bench.cable")));
    REQUIRE(acquire(meter, "p")->y.back() == Approx(totalDbm - 3.0).margin(0.1));
    REQUIRE(*meter.query("p") == Approx(totalDbm - 3.0).margin(0.1));
    REQUIRE(*meter.query("P_dBm") == *meter.query("p"));
    // No LO: nothing is converted, so the head is left measuring its own noise floor. That floor
    // fluctuates by 5 % in power — (10/ln10)·0.05 = 0.22 dB, 1σ — and the display cannot read below
    // `range_min`, so the reading is a normal truncated at −70 dBm: about half the draws land
    // exactly on it and none below. One draw is therefore a 0.22 dB-wide random variable, which is
    // what this asserts; a ±0.5 dB oracle on a single reading would be only 2.3σ.
    REQUIRE(lo.set("rf_on", false));
    std::vector<double> floorDbm;
    std::size_t atFloor = 0, flagged = 0;
    for (int k = 0; k < 200; ++k) {
        auto one = acquire(meter, "p");
        REQUIRE(one);
        const double p = one->y.back();
        REQUIRE(p >= -70.0); // the head cannot read below its own floor
        if (p == -70.0)
            ++atFloor;
        if (one->marker("out_of_range") != nullptr)
            ++flagged;
        floorDbm.push_back(p);
    }
    REQUIRE(flagged == atFloor); // every clamped reading says so (spec 12 §1: clamp and report)
    REQUIRE(atFloor > 70);       // ≈ half of 200; binomial σ ≈ 7
    REQUIRE(atFloor < 130);
    // Mean of a normal truncated from below at its own centre: −70 + σ/√(2π).
    const double sigmaDb = 10.0 / std::numbers::ln10 * 0.05;
    REQUIRE(instrtest::mean(floorDbm) ==
            Approx(-70.0 + sigmaDb / std::sqrt(2.0 * std::numbers::pi)).margin(0.04));
    REQUIRE(meter.set("input", std::string("nowhere")));
    REQUIRE(acquire(meter, "p").error().code == err::BadRouting);
}
