// Spec 12 §7, §9, §10; spec 25 §3.7 — oscilloscope (schedule envelope to 1e-12, front-end model,
// edge trigger), DC source (Φ = M I, f01(Φ)), controller (run timeline, trigger rate, memory
// fault).
#include "InstrTestSupport.hpp"

#include <catch2/catch_approx.hpp>

#include <numbers>

using namespace qlab;
using namespace qlab::instr;
using Catch::Approx;
using instrtest::powerOn;

namespace {
struct ScopeBench {
    Awg awg;
    Generator lo;
    IqMixer mixer;
    Oscilloscope scope;
    SignalGraph graph;
    std::shared_ptr<InputHub> hub = std::make_shared<InputHub>();

    explicit ScopeBench(std::shared_ptr<const pulse::Schedule> schedule,
                        std::vector<pulse::ChannelId> channels = {}) {
        RunView run;
        run.schedule = std::move(schedule);
        hub->publish(run);
        hub->publish(Environment{});
        graph.addNode("awg[0].ch[0]", &awg, "ch[0]");
        graph.addNode("awg[0].ch[1]", &awg, "ch[1]");
        graph.addNode("sg_mw[0].rf", &lo, "rf");
        graph.addNode("iq_mixer[0].rf", &mixer, "rf");
        graph.addEdge("awg[0].ch[0]", "iq_mixer[0].if");
        graph.addEdge("sg_mw[0].rf", "iq_mixer[0].lo");
        Bindings b;
        b.inputs = hub;
        b.routing = &graph;
        for (IInstrument* i : std::initializer_list<IInstrument*>{&lo, &mixer, &scope}) {
            i->bind(b);
            powerOn(*i);
        }
        b.channels = std::move(channels);
        awg.bind(b);
        powerOn(awg);
        mixer.attach(&awg, 0, &lo);
        REQUIRE(lo.set("rf_on", true));
    }
};
} // namespace

TEST_CASE("oscilloscope: the envelope view reproduces the schedule samples to 1e-12") {
    auto cx = instrtest::library("sc_fixed_5").scheduleFor("cx", {0, 1});
    REQUIRE(cx);
    auto schedule = std::make_shared<const pulse::Schedule>(*cx);
    ScopeBench bench(schedule, {pulse::ChannelId::drive(0), pulse::ChannelId::control(0, 1)});
    const double duration = pulse::secondsOf(schedule->duration());
    REQUIRE(bench.scope.set("timebase", duration / 10.0));
    REQUIRE(bench.scope.set("sample_rate", 5e9));
    REQUIRE(bench.scope.set("ch[1].source",
                            std::string("awg[0].ch[1]"))); // u[0,1]: the cross-resonance tone
    REQUIRE(bench.scope.set("ch[1].component", std::string("I")));

    // Independent reference: the schedule sampled at the device rate, held over each 222 ps sample.
    auto reference = pulse::sampleSchedule(*schedule, 1e12 / 222.0);
    REQUIRE(reference);
    const double vfs = 0.5;
    for (auto [channel, scheduleChannel, imag] :
         {std::tuple{"ch[0]", pulse::ChannelId::drive(0), false},
          std::tuple{"ch[1]", pulse::ChannelId::control(0, 1), false}}) {
        auto t = acquire(bench.scope, channel);
        REQUIRE(t);
        REQUIRE(t->cls == FidelityClass::Exact); // it is the schedule (spec 12 §7)
        REQUIRE(t->marker("sample_rate_effective")->value == 5e9);
        const pulse::ChannelSamples* cs = reference->find(scheduleChannel);
        REQUIRE(cs != nullptr);
        double peak = 0.0;
        for (std::size_t j = 0; j < t->size(); ++j) {
            const std::int64_t tPs = static_cast<std::int64_t>(j) * 200; // 5 GS/s
            REQUIRE(t->x[j] == Approx(static_cast<double>(tPs) * 1e-12).margin(1e-18));
            const auto k = static_cast<std::size_t>(tPs / 222);
            const Complex s = k < cs->samples.size() ? cs->samples[k] : Complex{};
            REQUIRE(std::abs(t->y[j] - vfs * (imag ? s.imag() : s.real())) <= 1e-12);
            peak = std::max(peak, std::abs(t->y[j]));
        }
        REQUIRE(peak > 0.01); // the pulses are really there
    }
    // The Q stream carries the DRAG derivative component of d[0] (T06 §4): small, non-zero, and
    // exactly the schedule. The cross-resonance tone on u[0,1] is a real gaussian_square with no
    // frame phase, so its Q stream is identically zero — the scope shows that too.
    REQUIRE(bench.scope.set("ch[0].component", std::string("Q")));
    REQUIRE(bench.scope.set("ch[1].component", std::string("Q")));
    const pulse::ChannelSamples* d0 = reference->find(pulse::ChannelId::drive(0));
    auto q0 = acquire(bench.scope, "ch[0]");
    auto q1 = acquire(bench.scope, "ch[1]");
    REQUIRE(q0);
    REQUIRE(q1);
    double qPeak = 0.0;
    for (std::size_t j = 0; j < q0->size(); ++j) {
        const auto k = static_cast<std::size_t>(static_cast<std::int64_t>(j) * 200 / 222);
        const Complex s = k < d0->samples.size() ? d0->samples[k] : Complex{};
        REQUIRE(std::abs(q0->y[j] - vfs * s.imag()) <= 1e-12);
        qPeak = std::max(qPeak, std::abs(q0->y[j]));
    }
    REQUIRE(qPeak > 1e-3);
    for (double y : q1->y)
        REQUIRE(y == 0.0);
    REQUIRE(bench.scope.query("ch[0]").has_value());
}

TEST_CASE("oscilloscope: front-end model, RF nodes and the edge trigger") {
    // A 0.4 full-scale step starting at 96 ns, at IF = 0 so the DAC output is a plain step.
    auto schedule = std::make_shared<pulse::Schedule>(Picoseconds{1000});
    schedule->insert(pulse::Play{pulse::ChannelId::drive(0), pulse::Waveform::constant(160e-9, 0.4),
                                 Picoseconds{96000}},
                     Picoseconds{96000});
    ScopeBench bench(schedule);
    REQUIRE(bench.awg.set("sample_rate", 1e9));
    REQUIRE(bench.awg.set("if_frequency", 0.0));
    REQUIRE(bench.scope.set("ch[0].view", std::string("output")));
    REQUIRE(bench.scope.set("sample_rate", 20e9));
    REQUIRE(bench.scope.set("timebase", 2e-9));
    REQUIRE(bench.scope.set("ch[0].volts_per_div", 0.05));
    REQUIRE(bench.scope.set("trigger", std::string("ch1")));
    REQUIRE(bench.scope.state() == State::Armed); // a trigger source is set
    REQUIRE(bench.scope.set("trigger_level", 0.1));
    auto t = acquire(bench.scope, "ch[0]");
    REQUIRE(t);
    REQUIRE(t->cls == FidelityClass::Model);
    REQUIRE(t->marker("trigger")->value == Approx(96e-9).margin(1e-12)); // the step's edge
    REQUIRE(t->x.front() ==
            Approx(96e-9 - 2e-9).margin(1e-15)); // shown one division into the record
    // 8-bit vertical resolution over 8 divisions.
    const double lsb = 8.0 * 0.05 / 256.0;
    REQUIRE(t->marker("lsb")->value == lsb);
    for (double y : t->y)
        REQUIRE(std::abs(y / lsb - std::round(y / lsb)) < 1e-9);
    REQUIRE(t->y.back() == Approx(0.2).margin(lsb)); // 0.4 × V_fs
    REQUIRE(t->y.front() == Approx(0.0).margin(lsb));
    // Gaussian response with −3 dB at 1 GHz: 10–90 % rise time 0.34/B = 340 ps.
    double t10 = 0.0, t90 = 0.0;
    for (std::size_t j = 1; j < t->size(); ++j) {
        if (t->y[j - 1] < 0.02 && t->y[j] >= 0.02)
            t10 = t->x[j];
        if (t->y[j - 1] < 0.18 && t->y[j] >= 0.18)
            t90 = t->x[j];
    }
    REQUIRE(t90 - t10 == Approx(0.3397 / 1e9).margin(60e-12)); // 50 ps sample spacing
    REQUIRE(bench.scope.set("bandwidth", 4e9));
    auto fast = acquire(bench.scope, "ch[0]");
    double f10 = 0.0, f90 = 0.0;
    for (std::size_t j = 1; j < fast->size(); ++j) {
        if (fast->y[j - 1] < 0.02 && fast->y[j] >= 0.02)
            f10 = fast->x[j];
        if (fast->y[j - 1] < 0.18 && fast->y[j] >= 0.18)
            f90 = fast->x[j];
    }
    REQUIRE(f90 - f10 < t90 - t10);

    // The 5 GHz mixer output is far outside a 1 GHz scope: e^{−(ln 2/2)(f/B)²} = 1.7e−4 of it
    // survives.
    REQUIRE(bench.scope.set("bandwidth", 1e9));
    REQUIRE(bench.scope.set("trigger", std::string("run")));
    REQUIRE(bench.scope.set("timebase", 30e-9));
    REQUIRE(bench.scope.set("ch[0].source", std::string("iq_mixer[0].rf")));
    REQUIRE(bench.scope.set("ch[0].volts_per_div", 0.001));
    auto rf = acquire(bench.scope, "ch[0]");
    REQUIRE(rf);
    double rfPeak = 0.0;
    for (double y : rf->y)
        rfPeak = std::max(rfPeak, std::abs(y));
    REQUIRE(rfPeak < 0.2 * 0.5 * 3.0 * 1.7e-4 + 8.0 * 0.001 / 256.0);
    // No source on channel 3; a level nothing crosses leaves the scope in auto mode.
    REQUIRE(acquire(bench.scope, "ch[2]").error().code == err::NoSignal);
    REQUIRE(bench.scope.set("ch[0].source", std::string("awg[0].ch[0]")));
    REQUIRE(bench.scope.set("trigger", std::string("ch1")));
    REQUIRE(bench.scope.set("trigger_level", 4.0));
    REQUIRE(acquire(bench.scope, "ch[0]")->marker("no_trigger") != nullptr);
    // A playhead outside the captured record is "no reading", not a reading of 0 V — the binding
    // path has to be distinguishable from a channel genuinely sitting at zero.
    RunView far;
    far.schedule = schedule;
    far.playheadS = 1.0;
    bench.hub->publish(far);
    REQUIRE_FALSE(bench.scope.query("ch[0]").has_value());
    far.playheadS = 120e-9; // inside the step: a real reading
    bench.hub->publish(far);
    REQUIRE(bench.scope.query("ch[0]").has_value());
}

TEST_CASE("DC source: flux bias and f01(flux) from the charge-basis spectrum") {
    const Environment env = instrtest::deviceEnvironment("sc_tunable_grid_54");
    DcSource dc;
    Bindings b;
    b.inputs = instrtest::makeHub(env);
    dc.bind(b);
    powerOn(dc);
    const hw::QubitCal& q0 = env.calibration->qubits[0];
    // Φ = M I: one flux quantum per Φ0/M = 1.034 mA at M = 2 pH (T05 §4: 0.7–2 mA).
    const double perQuantum = units::consts::Phi0.v / 2e-12;
    REQUIRE(perQuantum == Approx(1.0339e-3).epsilon(1e-4));
    REQUIRE(dc.fluxQuanta(perQuantum) == Approx(1.0).epsilon(1e-12));
    REQUIRE(dc.fluxQuanta(0.25 * perQuantum) == Approx(0.25).epsilon(1e-12));
    // Zero current is the calibrated point; the spectrum is Φ0-periodic and even in Φ.
    const double f0 = *dc.qubitFrequencyHz(0, 0.0);
    REQUIRE(f0 == Approx(q0.f01.value.v).epsilon(1e-8));
    REQUIRE(*dc.qubitFrequencyHz(0, perQuantum) == Approx(f0).epsilon(1e-9));
    REQUIRE(*dc.qubitFrequencyHz(0, -0.3 * perQuantum) ==
            Approx(*dc.qubitFrequencyHz(0, 0.3 * perQuantum)).epsilon(1e-10));
    // At Φ0/4 a symmetric SQUID has E_J = E_JΣ cos(π/4) (T05 (4.1)): independent recomputation.
    auto solved = hw::transmon::fromTargets(q0.f01.value, q0.anharmonicity.value);
    REQUIRE(solved);
    const units::Frequency ejQuarter(solved->EJ.v * std::cos(std::numbers::pi / 4.0));
    auto spectrum = hw::transmon::chargeBasisSpectrum(ejQuarter, solved->EC);
    REQUIRE(spectrum);
    const double fQuarter = *dc.qubitFrequencyHz(0, 0.25 * perQuantum);
    REQUIRE(fQuarter == Approx(spectrum->f01.v).epsilon(1e-10));
    REQUIRE(fQuarter < f0);
    // The asymptotic curve √(8 E_J E_C) − E_C follows it within a percent in the transmon regime.
    REQUIRE(*dc.theoryFrequencyHz(0, 0.25 * perQuantum) ==
            Approx(std::sqrt(8.0 * ejQuarter.v * solved->EC.v) - solved->EC.v).epsilon(1e-12));
    REQUIRE(*dc.theoryFrequencyHz(0, 0.25 * perQuantum) == Approx(fQuarter).epsilon(0.01));

    // Settings: ±10 mA clamped and reported, 1 nA resolution, compliance voltage I·R.
    REQUIRE(dc.set("ch[0].current", 12e-3));
    REQUIRE(std::get<double>(dc.get("ch[0].current")) == 10e-3);
    REQUIRE(dc.reports().size() == 1);
    REQUIRE(dc.set("ch[0].current", 0.2584567891e-3));
    REQUIRE(std::get<double>(dc.get("ch[0].current")) == Approx(0.258457e-3).margin(1e-15));
    REQUIRE(*dc.query("ch[0].I") == Approx(0.258457e-3).margin(1e-15));
    REQUIRE(*dc.query("ch[0].V") == Approx(0.258457e-3 * 60.0).epsilon(1e-12));
    REQUIRE(*dc.query("ch[0].flux") == Approx(0.258457e-3 / perQuantum).epsilon(1e-9));
    REQUIRE(acquire(dc, "ch[0].I")->y[0] == Approx(0.258457e-3).margin(1e-15));

    // The flux-spectroscopy arc: maxima at integer flux, no qubit line near half-integer flux.
    REQUIRE(dc.set("sweep", true));
    REQUIRE(dc.set("sweep_points", std::int64_t{49}));
    auto arc = acquire(dc, "sweep");
    REQUIRE(arc);
    REQUIRE(arc->cls ==
            FidelityClass::Model); // Numerical eigenvalues over a Model M: the weaker class
    REQUIRE(arc->size() < 49);
    REQUIRE(arc->size() > 30);
    REQUIRE(arc->aux.at("theory").size() == arc->size());
    const auto top =
        static_cast<std::size_t>(std::max_element(arc->y.begin(), arc->y.end()) - arc->y.begin());
    REQUIRE(std::abs(arc->x[top] - std::round(arc->x[top])) < 0.03);
    REQUIRE(arc->y[top] == Approx(f0).epsilon(2e-3));
    for (double phi : arc->x)
        REQUIRE(std::abs(std::abs(phi - std::round(phi)) - 0.5) > 0.01);
    REQUIRE(arc->marker("operating_point")->x == Approx(0.258457e-3 / perQuantum).epsilon(1e-9));
    // An asymmetric SQUID keeps a lower sweet spot at Φ0/2: the arc becomes continuous.
    REQUIRE(dc.set("asymmetry", 0.5));
    REQUIRE(acquire(dc, "sweep")->size() == 49);
    REQUIRE(*dc.qubitFrequencyHz(0, 0.0) == Approx(q0.f01.value.v).epsilon(1e-8));

    // A channel whose qubit the calibration does not describe is reported as itself. The sweep
    // skips currents that fall outside the transmon regime, so without resolving (E_JΣ, E_C) up
    // front this current-independent failure would be skipped at every point and then misreported
    // as "no point in the transmon regime".
    DcSource unmapped;
    Bindings ub;
    ub.inputs = instrtest::makeHub(instrtest::deviceEnvironment("sc_fixed_5")); // five qubits
    unmapped.bind(ub);
    powerOn(unmapped);
    REQUIRE(unmapped.set("sweep", true));
    REQUIRE(unmapped.set("sweep_channel", std::int64_t{6}));
    auto missing = acquire(unmapped, "sweep");
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error().code == err::BadInput);
    INFO("message: " << missing.error().message);
    REQUIRE(missing.error().message.find("calibration has no qubit 6") != std::string::npos);
}

TEST_CASE("controller: run timeline, executing instruction, trigger rate, sequencer memory") {
    auto cx = instrtest::library("sc_fixed_5").scheduleFor("cx", {0, 1});
    REQUIRE(cx);
    auto schedule = std::make_shared<const pulse::Schedule>(*cx);
    auto hub = std::make_shared<InputHub>();
    RunView run;
    run.schedule = schedule;
    run.shot = 17;
    hub->publish(run);
    Controller ctl;
    Bindings b;
    b.inputs = hub;
    ctl.bind(b);
    powerOn(ctl);

    const auto entries = Controller::timeline(*schedule);
    REQUIRE(entries.size() == schedule->size());
    for (std::size_t i = 1; i < entries.size(); ++i)
        REQUIRE(entries[i].t0S >= entries[i - 1].t0S);
    auto t = acquire(ctl, "timeline");
    REQUIRE(t);
    REQUIRE(t->cls == FidelityClass::Exact);
    REQUIRE(t->size() == entries.size());
    REQUIRE(t->aux.at("line").size() == entries.size());
    // Put the playhead in the middle of the first play: that instruction is the one executing.
    const auto firstPlay = std::find_if(entries.begin(), entries.end(),
                                        [](const TimelineEntry& e) { return e.kind == 0; });
    REQUIRE(firstPlay != entries.end());
    run.playheadS = firstPlay->t0S + 0.5 * firstPlay->durationS;
    hub->publish(run);
    auto mid = acquire(ctl, "timeline");
    REQUIRE(mid->marker("playhead")->x == run.playheadS);
    const auto executing = Controller::executingAt(entries, run.playheadS);
    REQUIRE(executing.has_value());
    REQUIRE(entries[*executing].kind == 0);
    REQUIRE(entries[*executing].t0S <= run.playheadS);
    REQUIRE(mid->marker("current: " + entries[*executing].label) != nullptr);
    REQUIRE(*ctl.query("instruction") == static_cast<double>(*executing));
    REQUIRE(*ctl.query("shot") == 17.0);
    REQUIRE_FALSE(Controller::executingAt(entries, 1.0).has_value()); // long after the schedule

    // Trigger rate 1/(T_schedule + shot_loop); locked to the 10 MHz reference.
    const double duration = pulse::secondsOf(schedule->duration());
    REQUIRE(acquire(ctl, "rate")->y[0] == Approx(1.0 / (duration + 250e-6)).epsilon(1e-12));
    REQUIRE(ctl.set("shot_loop", 1e-3));
    REQUIRE(*ctl.query("rate") == Approx(1.0 / (duration + 1e-3)).epsilon(1e-12));
    REQUIRE(acquire(ctl, "locked")->y[0] == 1.0);
    REQUIRE(ctl.set("clock_ref", std::string("internal")));
    REQUIRE(*ctl.query("locked") == 0.0);
    REQUIRE(std::get<double>(ctl.get("feedforward_latency_ns")) == 200.0);

    // More instructions than the sequencer holds: Fault with the count.
    REQUIRE(ctl.set("sequencer_memory", std::int64_t{64}));
    auto big = std::make_shared<pulse::Schedule>(Picoseconds{1000});
    for (int k = 0; k < 100; ++k)
        big->append(pulse::FrameOp{pulse::ChannelId::drive(0), Picoseconds{0},
                                   pulse::FrameOp::Op::ShiftPhase, 0.1});
    run.schedule = big;
    hub->publish(run);
    REQUIRE(acquire(ctl, "timeline").error().code == err::MemoryOverflow);
    REQUIRE(ctl.state() == State::Fault);
    REQUIRE(ctl.faultMessage().find("100 instructions exceeds the 64") != std::string::npos);
}
