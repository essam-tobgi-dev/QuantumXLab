// Spec 12 §1 (registry ids), §11 (bindings, routing, live mode), §14 (component descriptors) and
// the §15 acceptance line "every instrument has a component descriptor that passes lint".
#include "InstrTestSupport.hpp"

#include <catch2/catch_approx.hpp>

using namespace qlab;
using namespace qlab::instr;
using Catch::Approx;

TEST_CASE("the registry is keyed by the instrument ids of spec 12 and builds the standard rack") {
    InstrumentRegistry registry;
    // Spec 12 §1: the physical instrument kinds, in panel order.
    const std::vector<std::string> physical = {"sg_mw",
                                               "awg",
                                               "iq_mixer",
                                               "digitizer",
                                               "vna",
                                               "spectrum_analyzer",
                                               "oscilloscope",
                                               "thermometer_ruo2",
                                               "thermometer_cernox",
                                               "pressure_gauge",
                                               "flow_meter",
                                               "power_meter",
                                               "dc_source",
                                               "controller"};
    for (auto const& kind : physical)
        REQUIRE(registry.hasKind(kind));
    REQUIRE(registry.kinds(true).size() == physical.size());
    REQUIRE_FALSE(registry.hasKind("spectrometer"));
    REQUIRE(registry.create("spectrometer").error().code == err::UnknownInstrument);

    REQUIRE(registry.createStandardSet());
    for (auto const& kind : physical)
        REQUIRE(registry.find(kind) != nullptr);
    // Instances of one kind are numbered from 0 and print as "sg_mw[2]".
    REQUIRE(registry.ofKind("sg_mw").size() == 4);
    REQUIRE(registry.find("sg_mw", 2)->id().toString() == "sg_mw[2]");
    REQUIRE(registry.find("sg_mw", 9) == nullptr);
    REQUIRE(registry.ofKind("thermometer_ruo2").size() == 2); // MXC and CP (spec 12 §8)
    REQUIRE(registry.ofKind("thermometer_cernox").size() == 3);
    REQUIRE(registry.ofKind("pressure_gauge").size() == 4);
    for (IInstrument* i : registry.all())
        REQUIRE(i->state() != State::Off); // powerOn by default
    registry.powerAll(false);
    for (IInstrument* i : registry.all())
        REQUIRE(i->state() == State::Off);
    registry.powerAll(true);

    // Every kind's schema carries the id its descriptor names, and refresh_hz (spec 12 §1).
    const core::Json schemas = registry.schemas();
    for (auto const& kind : registry.kinds()) {
        IInstrument* i = registry.find(kind);
        REQUIRE(i != nullptr);
        REQUIRE(i->settings().instrument == kind);
        REQUIRE(i->settings().find("refresh_hz") != nullptr);
        REQUIRE(schemas.contains(i->settings().id));
    }
}

TEST_CASE("bindings: component, fridge lines and schedule channels; routing carries the signal") {
    InstrumentRegistry registry;
    REQUIRE(registry.createStandardSet());
    registry.inputs()->publish(instrtest::deviceEnvironment());

    // Spec 12 §11: the rack unit in the 3D scene, the lines it touches, the channels it sources.
    const ComponentId unit{41};
    REQUIRE(registry.bindComponent({"awg", 0}, unit, {"drive_q0"},
                                   {pulse::ChannelId::drive(0), pulse::ChannelId::drive(1)}));
    IInstrument* awg = registry.find("awg", 0);
    REQUIRE(awg->bindings().component == unit);
    REQUIRE(awg->bindings().lines == std::vector<std::string>{"drive_q0"});
    REQUIRE(awg->bindings().channels.size() == 2);
    REQUIRE(registry.findByComponent(unit) == awg);
    REQUIRE(registry.findByComponent(ComponentId{999}) == nullptr);
    REQUIRE(registry.bindComponent({"awg", 7}, unit).error().code == err::UnknownInstrument);
    // Binding keeps the shared hub, bus and routing matrix the registry owns.
    REQUIRE(awg->bindings().inputs == registry.inputs());
    REQUIRE(awg->bindings().routing == &registry.routing());

    // createStandardSet wired AWG output 0 → iq_mixer[0] IF and sg_mw[0] → its LO (spec 12 §4).
    const SignalGraph& graph = registry.routing();
    REQUIRE(graph.hasNode("awg[0].ch[0]"));
    REQUIRE(graph.hasNode("iq_mixer[0].rf"));
    REQUIRE(graph.inputOf("iq_mixer[0].if").has_value());
    REQUIRE(graph.inputOf("iq_mixer[0].if")->from == "awg[0].ch[0]");
    REQUIRE(graph.inputOf("iq_mixer[0].lo")->from == "sg_mw[0].rf");

    // A fridge line hung off the mixer publishes a node per stage (spec 11, spec 12 §11).
    REQUIRE(registry.attachLine("iq_mixer[0].rf", "drive_q0"));
    REQUIRE(graph.hasNode("line.drive_q0.MXC"));
    REQUIRE(registry.attachLine("nowhere", "drive_q0").error().code == err::BadRouting);
    auto tone = instrtest::toneSchedule(200e-9, 0.5);
    RunView run;
    run.schedule = tone;
    registry.inputs()->publish(run);
    REQUIRE(registry.find("sg_mw", 0)->set("rf_on", true));
    SignalRequest request;
    request.samples = 512;
    auto top = graph.signalAt("line.drive_q0.PT1", request);
    auto bottom = graph.signalAt("line.drive_q0.MXC", request);
    REQUIRE(top);
    REQUIRE(bottom);
    REQUIRE(meanPowerWatts(*bottom) < meanPowerWatts(*top)); // attenuators on the way down
    REQUIRE(bottom->noisePsdWPerHz > 0.0);

    // Pulling the LO cable out of the mixer leaves the whole line downstream dark (§11): with no
    // LO there is nothing to up-convert, not even the leakage term an open IF cable would leave.
    REQUIRE(registry.routing().setConnected("sg_mw[0].rf->iq_mixer[0].lo", false));
    auto cut = graph.signalAt("line.drive_q0.MXC", request);
    REQUIRE(cut);
    REQUIRE_FALSE(cut->connected);
    for (auto s : cut->samples)
        REQUIRE(std::abs(s) == 0.0);
    REQUIRE(registry.routing().setConnected("no such cable", false).error().code ==
            err::BadRouting);
}

TEST_CASE("query serves the instr.*, run.* and cryo.* binding paths the App adapts") {
    InstrumentRegistry registry;
    REQUIRE(registry.createStandardSet());
    Environment env = instrtest::deviceEnvironment();
    env.thermal.T_K[static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::MXC))] = 0.011;
    registry.inputs()->publish(env);
    RunView run;
    run.shot = 250;
    run.shots = 1000;
    run.gateCursor = 7;
    run.playheadS = 3.5e-7;
    run.running = true;
    registry.inputs()->publish(run);

    REQUIRE(registry.find("sg_mw", 1)->set("frequency", 6.25e9));
    REQUIRE(*registry.query("instr.gen[1].f") == 6.25e9);
    REQUIRE(*registry.query("gen[1].f") == 6.25e9); // the root is optional
    REQUIRE(*registry.query("instr.gen[1].on") == 0.0);
    REQUIRE(registry.find("sg_mw", 1)->set("rf_on", true));
    REQUIRE(*registry.query("instr.gen[1].on") == 1.0);
    REQUIRE(*registry.query("instr.sg_mw[1].frequency") ==
            6.25e9); // the generic "<kind>[i].<setting>"
    REQUIRE(*registry.query("instr.ref.locked") == 1.0);
    REQUIRE(*registry.query("instr.trig.rate") == Approx(1.0 / 250e-6).epsilon(1e-12));
    REQUIRE(*registry.query("instr.dc.ch[0].I") == 0.0);
    REQUIRE(*registry.query("instr.vna.points") == 1601.0);

    REQUIRE(*registry.query("run.shot") == 250.0);
    REQUIRE(*registry.query("run.shots") == 1000.0);
    REQUIRE(*registry.query("run.progress") == Approx(0.25));
    REQUIRE(*registry.query("run.gate_cursor") == 7.0);
    REQUIRE(*registry.query("run.playhead") == 3.5e-7);
    REQUIRE(*registry.query("run.running") == 1.0);
    REQUIRE_FALSE(registry.query("run.nonsense").has_value());
    REQUIRE_FALSE(registry.query("instr.gen[9].f").has_value());
    REQUIRE_FALSE(registry.query("what.ever").has_value());

    // cryo.thermo[i] walks the thermometers in creation order; a reading appears once acquired.
    REQUIRE_FALSE(registry.query("cryo.thermo[0].T").has_value());
    REQUIRE(acquire(*registry.find("thermometer_ruo2", 0), "T"));
    REQUIRE(*registry.query("cryo.thermo[0].T") ==
            Approx(0.011).epsilon(0.05)); // reading noise + self-heating
}

TEST_CASE("every shipped instrument descriptor passes the spec 12 §14 lint") {
    auto descriptors = loadInstrumentDescriptors();
    REQUIRE(descriptors);
    REQUIRE(descriptors->size() ==
            16); // Assets/Lab/Components/*/component.json with an `instrument` block
    InstrumentRegistry registry;
    std::vector<std::string> problems;
    for (auto const& d : *descriptors) {
        REQUIRE(registry.hasKind(d.kind));
        REQUIRE_FALSE(d.settingsSchema.empty());
        for (auto const& line : lintDescriptor(d, registry))
            problems.push_back(line);
    }
    INFO("lint: " << (problems.empty() ? std::string{"none"} : problems.front()));
    REQUIRE(problems.empty());

    // Spec 12 §15: every physical kind of the registry has a component descriptor.
    for (auto const& kind : registry.kinds(true)) {
        const bool covered =
            std::any_of(descriptors->begin(), descriptors->end(),
                        [&](const InstrumentDescriptor& d) { return d.kind == kind; });
        INFO("uncovered kind: " << kind);
        REQUIRE(covered);
    }

    // A descriptor naming a channel the model has not got is a lint error, not a crash.
    InstrumentDescriptor bogus = descriptors->front();
    bogus.componentId = "bogus";
    bogus.channels.push_back("not_a_channel");
    bogus.specSheet.push_back({"warp factor", "", std::nullopt, ""});
    const auto lines = lintDescriptor(bogus, registry);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0].find("not_a_channel") != std::string::npos);
    REQUIRE(lines[1].find("warp factor") != std::string::npos);
}

TEST_CASE("live mode acquires at refresh_hz on the job system and posts traces on the bus") {
    InstrumentRegistry registry;
    core::EventBus bus;
    core::JobSystem jobs(2);
    registry.setEventBus(&bus);
    REQUIRE(registry.createStandardSet());
    registry.inputs()->publish(instrtest::deviceEnvironment());

    std::vector<std::shared_ptr<const Trace>> traces;
    auto sub = bus.subscribe<TraceReady>([&](const TraceReady& e) { traces.push_back(e.trace); });
    std::size_t failures = 0;
    auto failed = bus.subscribe<AcquireFailed>([&](const AcquireFailed&) { ++failures; });

    LiveRunner live(registry, jobs, bus);
    const InstrumentId controller{"controller", 0};
    REQUIRE(live.enable(controller, "rate"));
    REQUIRE(live.enable({"thermometer_ruo2", 0}, "T"));
    REQUIRE(live.enabled(controller, "rate"));
    REQUIRE(live.liveChannels() == 2);
    REQUIRE(live.enable({"controller", 3}, "rate").error().code == err::UnknownInstrument);
    REQUIRE(live.enable(controller, "s21").error().code == err::UnknownChannel);

    // Each channel is paced by its own instrument's refresh_hz: 20 Hz on the controller, the
    // default 10 Hz on the thermometer (spec 12 §1).
    REQUIRE(registry.find("controller", 0)->set("refresh_hz", 20.0));
    REQUIRE(std::get<double>(registry.find("thermometer_ruo2", 0)->get("refresh_hz")) == 10.0);
    REQUIRE(live.tick(0.0) == 2);
    live.waitIdle();
    REQUIRE(live.tick(0.05) == 1); // only the controller's 50 ms period has elapsed
    live.waitIdle();
    REQUIRE(live.tick(0.06) == 0);
    live.waitIdle();
    REQUIRE(live.tick(0.10) == 2);
    live.waitIdle();
    REQUIRE(live.inFlight() == 0);
    bus.drain();
    REQUIRE(failures == 0);
    REQUIRE(traces.size() == 5);
    std::size_t rates = 0, temperatures = 0;
    for (auto const& t : traces) {
        REQUIRE((t->channel == "rate" || t->channel == "T"));
        (t->channel == "rate" ? rates : temperatures) += 1;
        // `rate` is a scalar reading (one point per acquisition); the thermometer's `T` is the
        // strip chart of every reading taken so far (spec 12 §8), so it is one point longer each
        // time.
        REQUIRE(t->size() == (t->channel == "rate" ? std::size_t{1} : temperatures));
        REQUIRE(t->instrument == (t->channel == "rate" ? "controller[0]" : "thermometer_ruo2[0]"));
    }
    REQUIRE(rates == 3);
    REQUIRE(temperatures == 2);

    // A switched-off instrument is skipped; disabling a channel removes it.
    registry.find("controller", 0)->execute(Command::of(Command::Kind::PowerOff));
    REQUIRE(live.tick(0.30) == 1);
    live.waitIdle();
    REQUIRE(live.enable(controller, "rate", false));
    REQUIRE(live.liveChannels() == 1);
    REQUIRE_FALSE(live.enabled(controller, "rate"));
}

TEST_CASE("registry state round-trips through the project file") {
    InstrumentRegistry a;
    REQUIRE(a.createStandardSet());
    REQUIRE(a.find("vna", 0)->set("points", std::int64_t{801}));
    REQUIRE(a.find("sg_mw", 0)->set("frequency", 7.125e9));
    REQUIRE(a.find("digitizer", 0)->execute(Command::of(Command::Kind::PowerOff)));
    REQUIRE(a.routing().setConnected("awg[0].ch[0]->iq_mixer[0].if", false));
    const core::Json saved = a.saveState();

    InstrumentRegistry b;
    REQUIRE(b.createStandardSet());
    REQUIRE(b.loadState(saved));
    REQUIRE(std::get<std::int64_t>(b.find("vna", 0)->get("points")) == 801);
    REQUIRE(std::get<double>(b.find("sg_mw", 0)->get("frequency")) == 7.125e9);
    REQUIRE(b.find("digitizer", 0)->state() == State::Off);
    REQUIRE(b.find("sg_mw", 0)->state() != State::Off);
    const auto edges = b.routing().edges();
    const auto cut = std::find_if(edges.begin(), edges.end(), [](const RouteEdge& e) {
        return e.id == "awg[0].ch[0]->iq_mixer[0].if";
    });
    REQUIRE(cut != edges.end());
    REQUIRE_FALSE(cut->connected);
    REQUIRE(b.loadState(core::Json::object()).error().code == err::BadSchema);
}
