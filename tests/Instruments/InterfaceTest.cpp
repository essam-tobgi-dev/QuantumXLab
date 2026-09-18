// Spec 12 §1 — the instrument interface: schema validation with clamp-and-report, the state
// machine Off → Idle → Armed → Acquiring → Idle, Fault with a message, settings round trips.
#include "InstrTestSupport.hpp"

#include <catch2/catch_approx.hpp>

using namespace qlab;
using namespace qlab::instr;
using Catch::Approx;
using instrtest::powerOn;

TEST_CASE("settings outside the schema are clamped, stored and reported") {
    Generator gen;
    core::EventBus bus;
    Bindings b;
    b.bus = &bus;
    gen.bind(b);
    std::vector<SettingReport> posted;
    auto sub = bus.subscribe<SettingClamped>([&](const SettingClamped& e) { posted.push_back(e.report); });

    // In range: stored as is, nothing reported. Frequency resolution is 1 Hz (silent rounding).
    REQUIRE(gen.set("frequency", 6.1234567894e9));
    REQUIRE(std::get<double>(gen.get("frequency")) == 6123456789.0);
    REQUIRE(gen.reports().empty());

    // Above the 20 GHz limit of spec 12 §2: clamped, the call succeeds, the clamp is reported.
    REQUIRE(gen.set("frequency", 25e9));
    REQUIRE(std::get<double>(gen.get("frequency")) == 20e9);
    REQUIRE(gen.set("power", -35.0));
    REQUIRE(std::get<double>(gen.get("power")) == -20.0);
    auto reports = gen.reports();
    REQUIRE(reports.size() == 2);
    REQUIRE(reports[0].key == "frequency");
    REQUIRE(std::get<double>(reports[0].requested) == 25e9);
    REQUIRE(std::get<double>(reports[0].applied) == 20e9);
    REQUIRE(reports[0].message.find("above the maximum") != std::string::npos);
    REQUIRE(reports[1].key == "power");
    REQUIRE(reports[1].sequence == reports[0].sequence + 1);
    bus.drain();
    REQUIRE(posted.size() == 2);
    REQUIRE(posted[1].message == reports[1].message);

    // An integer is accepted for a real setting; a discrete setting snaps to the nearest value.
    REQUIRE(gen.set("power", std::int64_t{5}));
    REQUIRE(std::get<double>(gen.get("power")) == 5.0);
    Awg awg;
    REQUIRE(awg.set("sample_rate", 3.0e9));
    REQUIRE(std::get<double>(awg.get("sample_rate")) == 2e9);
    REQUIRE(awg.reports().size() == 1);
    REQUIRE(awg.reports()[0].message.find("not an available value") != std::string::npos);

    // Errors, not clamps: unknown key, wrong type, NaN, unknown enum option, read-only constant.
    REQUIRE(gen.set("colour", 1.0).error().code == err::UnknownSetting);
    REQUIRE(gen.set("frequency", std::string("fast")).error().code == err::BadSettingType);
    REQUIRE(gen.set("frequency", std::nan("")).error().code == err::BadSettingType);
    REQUIRE(gen.set("ref", std::string("gps")).error().code == err::BadSettingType);
    REQUIRE(gen.set("rf_on", 2.5).error().code == err::BadSettingType);
    REQUIRE(awg.set("granularity_samples", std::int64_t{8}).error().code == err::BadSettingType);
    REQUIRE(std::holds_alternative<std::monostate>(gen.get("colour")));
    REQUIRE(std::get<double>(gen.get("frequency")) == 20e9); // failed sets change nothing
}

TEST_CASE("state machine: Off -> Idle -> Armed -> Acquiring -> Idle, with events") {
    Awg awg;
    core::EventBus bus;
    auto hub = instrtest::makeHub({}, [] {
        RunView r;
        r.schedule = instrtest::toneSchedule(64e-9);
        return r;
    }());
    Bindings b;
    b.inputs = hub;
    b.bus = &bus;
    awg.bind(b);
    std::vector<std::pair<State, State>> transitions;
    auto sub = bus.subscribe<StateChanged>([&](const StateChanged& e) { transitions.push_back({e.from, e.to}); });

    REQUIRE(awg.state() == State::Off);
    auto off = acquire(awg, "ch[0].waveform");
    REQUIRE_FALSE(off);
    REQUIRE(off.error().code == err::PoweredOff);
    REQUIRE(awg.execute(Command::of(Command::Kind::Arm)).error().code == err::BadState);

    powerOn(awg);
    REQUIRE(awg.state() == State::Idle);
    REQUIRE(awg.set("trigger", std::string("internal"))); // a trigger source is set: Idle → Armed
    REQUIRE(awg.state() == State::Armed);
    auto t = acquire(awg, "ch[0].waveform");
    REQUIRE(t);
    REQUIRE(awg.state() == State::Idle); // Armed → Acquiring → Idle per acquisition
    REQUIRE(t->t.sequence == 1);

    // A free-running acquisition from Idle is allowed and returns to Idle.
    REQUIRE(acquire(awg, "running"));
    REQUIRE(awg.state() == State::Idle);
    REQUIRE(awg.execute(Command::of(Command::Kind::Arm)));
    REQUIRE(awg.state() == State::Armed);
    REQUIRE(awg.execute(Command::of(Command::Kind::Disarm)));
    REQUIRE(awg.state() == State::Idle);
    REQUIRE(awg.execute(Command::of(Command::Kind::PowerOff)));
    REQUIRE(awg.state() == State::Off);

    bus.drain();
    const std::vector<std::pair<State, State>> want = {
        {State::Off, State::Idle},       {State::Idle, State::Armed},     {State::Armed, State::Acquiring},
        {State::Acquiring, State::Idle}, {State::Idle, State::Acquiring}, {State::Acquiring, State::Idle},
        {State::Idle, State::Armed},     {State::Armed, State::Idle},     {State::Idle, State::Off}};
    REQUIRE(transitions == want);
    REQUIRE(acquire(awg, "no_such_channel").error().code == err::UnknownChannel);
}

TEST_CASE("AWG memory overflow is a Fault carrying the sample count, cleared by a larger memory") {
    Awg awg;
    core::EventBus bus;
    RunView run;
    run.schedule = instrtest::toneSchedule(8e-6); // 8 µs at 1 GS/s = 8000 samples
    Bindings b;
    b.inputs = instrtest::makeHub({}, run);
    b.bus = &bus;
    awg.bind(b);
    powerOn(awg);
    REQUIRE(awg.set("sample_rate", 1e9));
    REQUIRE(awg.set("memory_samples", std::int64_t{4096}));
    std::vector<std::string> faults;
    auto sub = bus.subscribe<InstrumentFault>([&](const InstrumentFault& f) { faults.push_back(f.message); });

    auto t = acquire(awg, "ch[0].waveform");
    REQUIRE_FALSE(t);
    REQUIRE(t.error().code == err::MemoryOverflow);
    REQUIRE(awg.state() == State::Fault);
    REQUIRE(awg.faultMessage().find("8000 samples") != std::string::npos);
    REQUIRE(awg.faultMessage().find("4096") != std::string::npos);
    auto again = acquire(awg, "ch[0].waveform");
    REQUIRE(again.error().code == err::Faulted);
    REQUIRE(again.error().message.find("8000 samples") != std::string::npos);
    REQUIRE(awg.execute(Command::of(Command::Kind::ClearFault))); // nothing changed: the next acquire faults again
    REQUIRE(acquire(awg, "ch[0].waveform").error().code == err::MemoryOverflow);
    bus.drain();
    REQUIRE(faults.size() == 2);

    // Spec 12 §3: the overflow is a Fault whoever discovers it. Reading the AWG's output node
    // through the routing matrix (what a scope or analyzer does) must latch it just the same.
    REQUIRE(awg.execute(Command::of(Command::Kind::ClearFault)));
    REQUIRE(awg.render(0).error().code == err::MemoryOverflow);
    REQUIRE(awg.state() == State::Fault);
    REQUIRE(awg.faultMessage().find("8000 samples") != std::string::npos);
    SignalGraph graph;
    graph.addNode("awg[0].ch[0]", &awg, "ch[0]");
    REQUIRE(awg.execute(Command::of(Command::Kind::ClearFault)));
    SignalRequest request;
    request.samples = 64;
    REQUIRE(graph.signalAt("awg[0].ch[0]", request).error().code == err::MemoryOverflow);
    REQUIRE(awg.state() == State::Fault);

    REQUIRE(awg.set("memory_samples", std::int64_t{16384})); // a new configuration leaves Fault
    REQUIRE(awg.state() == State::Idle);
    REQUIRE(awg.faultMessage().empty());
    auto ok = acquire(awg, "ch[0].waveform");
    REQUIRE(ok);
    REQUIRE(ok->size() == 8000);
    REQUIRE(ok->marker("memory_used")->value == 8000.0);
}

TEST_CASE("settings schemas and values round-trip through JSON") {
    for (const SettingSchema& schema : {Generator::makeSchema(), Awg::makeSchema()}) {
        const std::string text = schema.toJson().dump();
        auto back = SettingSchema::fromJson(core::Json::parse(text));
        REQUIRE(back);
        REQUIRE(*back == schema);
        REQUIRE(back->find("refresh_hz") != nullptr);
        REQUIRE(std::get<double>(back->find("refresh_hz")->defaultValue) == 10.0); // spec 12 §1 default
    }
    REQUIRE(SettingSchema::fromJson(core::Json::parse(R"({"title":"x"})")).error().code == err::BadSchema);
    auto noDefault = SettingSchema::fromJson(core::Json::parse(R"({"properties":{"f":{"type":"number"}}})"));
    REQUIRE(noDefault.error().message.find("properties.f.default") != std::string::npos);

    Generator a, b;
    REQUIRE(a.set("frequency", 7.25e9));
    REQUIRE(a.set("rf_on", true));
    REQUIRE(a.set("ref", std::string("internal")));
    core::Json saved = core::Json::parse(a.saveSettings().dump());
    saved["future_field"] = 3; // unknown fields are tolerated
    REQUIRE(b.loadSettings(saved));
    REQUIRE(b.saveSettings() == a.saveSettings());
    REQUIRE(std::get<bool>(b.get("rf_on")));
    saved["power"] = 99.0; // loading clamps and reports like any set()
    REQUIRE(b.loadSettings(saved));
    REQUIRE(std::get<double>(b.get("power")) == 20.0);
    REQUIRE(b.reports().size() == 1);
    saved["rf_on"] = "yes";
    REQUIRE(b.loadSettings(saved).error().code == err::BadSettingType);

    // Preset restores the defaults.
    REQUIRE(b.execute(Command::of(Command::Kind::Preset)));
    REQUIRE(std::get<double>(b.get("frequency")) == 5.0e9);
    REQUIRE(b.reports().empty());
}
