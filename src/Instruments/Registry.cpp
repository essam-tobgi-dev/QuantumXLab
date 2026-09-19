#include "Instruments/Registry.hpp"
#include "Instruments/Awg.hpp"
#include "Instruments/Controller.hpp"
#include "Instruments/DcSource.hpp"
#include "Instruments/Digitizer.hpp"
#include "Instruments/Gauges.hpp"
#include "Instruments/Generator.hpp"
#include "Instruments/IqMixer.hpp"
#include "Instruments/Oscilloscope.hpp"
#include "Instruments/Probes.hpp"
#include "Instruments/SpectrumAnalyzer.hpp"
#include "Instruments/Thermometer.hpp"
#include "Instruments/Vna.hpp"
#include <algorithm>
#include <format>

namespace qlab::instr {
namespace {
template <class T> InstrumentRegistry::Factory make() {
    return [](std::uint32_t index) -> std::unique_ptr<IInstrument> {
        return std::make_unique<T>(index);
    };
}
} // namespace

InstrumentRegistry::InstrumentRegistry() {
    registerKind("sg_mw", make<Generator>());
    registerKind("awg", make<Awg>());
    registerKind("iq_mixer", make<IqMixer>());
    registerKind("digitizer", make<Digitizer>());
    registerKind("vna", make<Vna>());
    registerKind("spectrum_analyzer", make<SpectrumAnalyzer>());
    registerKind("oscilloscope", make<Oscilloscope>());
    registerKind("thermometer_ruo2", [](std::uint32_t i) -> std::unique_ptr<IInstrument> {
        return std::make_unique<Thermometer>(SensorKind::RuO2, i);
    });
    registerKind("thermometer_cernox", [](std::uint32_t i) -> std::unique_ptr<IInstrument> {
        return std::make_unique<Thermometer>(SensorKind::Cernox, i);
    });
    registerKind("pressure_gauge", make<PressureGauge>());
    registerKind("flow_meter", make<FlowMeter>());
    registerKind("power_meter", make<PowerMeter>());
    registerKind("dc_source", make<DcSource>());
    registerKind("controller", make<Controller>());
    registerKind("probe_state", make<StateProbe>(), true);
    registerKind("probe_entanglement", make<EntanglementProbe>(), true);
    registerKind("probe_fidelity", make<FidelityProbe>(), true);
    registerKind("probe_trajectory", make<TrajectoryProbe>(), true);
    registerKind("probe_thermal_truth", make<ThermalTruthProbe>(), true);
    registerKind("probe_leakage", make<LeakageProbe>(), true);
}

InstrumentRegistry::~InstrumentRegistry() = default;

void InstrumentRegistry::registerKind(std::string kind, Factory factory, bool simulatorOnly) {
    if (kinds_.find(kind) == kinds_.end())
        kindOrder_.push_back(kind);
    kinds_[std::move(kind)] = KindInfo{std::move(factory), simulatorOnly};
}

bool InstrumentRegistry::hasKind(std::string_view kind) const {
    return kinds_.find(kind) != kinds_.end();
}

std::vector<std::string> InstrumentRegistry::kinds(bool physicalLab) const {
    std::vector<std::string> out;
    for (auto const& k : kindOrder_)
        if (!physicalLab || !kinds_.find(k)->second.simulatorOnly)
            out.push_back(k);
    return out;
}

core::Json InstrumentRegistry::schemas() const {
    core::Json out = core::Json::object();
    for (auto const& k : kindOrder_) {
        const auto instrument = kinds_.find(k)->second.factory(0);
        const SettingSchema& s = instrument->settings();
        // The two thermometers share instr/thermometer.schema.json; their schemas differ only in
        // defaults, so the file holds them side by side under the instrument kind.
        if (out.contains(s.id))
            out[s.id]["x-variants"][s.instrument] = s.toJson();
        else
            out[s.id] = s.toJson();
    }
    return out;
}

Bindings InstrumentRegistry::bindingsFor(const IInstrument* existing) const {
    Bindings b = existing ? existing->bindings() : Bindings{};
    b.inputs = hub_;
    b.bus = bus_;
    b.routing = &graph_;
    return b;
}

Result<IInstrument*> InstrumentRegistry::create(std::string_view kind) {
    auto it = kinds_.find(kind);
    if (it == kinds_.end())
        return fail(err::UnknownInstrument, std::format("no instrument kind '{}'", kind));
    const auto index = static_cast<std::uint32_t>(ofKind(kind).size());
    std::unique_ptr<IInstrument> instrument = it->second.factory(index);
    instrument->bind(bindingsFor(nullptr));
    IInstrument* raw = instrument.get();
    instruments_.push_back(std::move(instrument));
    const std::string name = raw->id().toString();
    // Signal sources publish their output ports as routing nodes.
    if (auto* awg = dynamic_cast<Awg*>(raw))
        for (std::uint32_t k = 0; k < Awg::kChannels; ++k)
            graph_.addNode(std::format("{}.ch[{}]", name, k), awg, std::format("ch[{}]", k));
    if (auto* gen = dynamic_cast<Generator*>(raw))
        graph_.addNode(name + ".rf", gen, "rf");
    if (auto* mixer = dynamic_cast<IqMixer*>(raw)) {
        graph_.addNode(mixer->rfNode(), mixer, "rf");
        graph_.addNode(mixer->ifNode());
        graph_.addNode(mixer->loNode());
    }
    if (auto* dig = dynamic_cast<Digitizer*>(raw)) {
        graph_.addNode(name + ".in", dig, "in");
        graph_.addNode(name + ".demod", dig, "demod");
    }
    if (dynamic_cast<Thermometer*>(raw) || dynamic_cast<ThermalTruthProbe*>(raw))
        attachThermometers();
    return raw;
}

void InstrumentRegistry::attachThermometers() {
    std::vector<const Thermometer*> thermometers;
    for (auto const& i : instruments_)
        if (auto* t = dynamic_cast<const Thermometer*>(i.get()))
            thermometers.push_back(t);
    for (auto const& i : instruments_)
        if (auto* p = dynamic_cast<ThermalTruthProbe*>(i.get()))
            p->attach(thermometers);
}

IInstrument* InstrumentRegistry::find(std::string_view kind, std::uint32_t index) const {
    for (auto const& i : instruments_)
        if (i->id().kind == kind && i->id().index == index)
            return i.get();
    return nullptr;
}

std::vector<IInstrument*> InstrumentRegistry::all(bool physicalLab) const {
    std::vector<IInstrument*> out;
    for (auto const& i : instruments_)
        if (!physicalLab || !i->simulatorOnly())
            out.push_back(i.get());
    return out;
}

std::vector<IInstrument*> InstrumentRegistry::ofKind(std::string_view kind) const {
    std::vector<IInstrument*> out;
    for (auto const& i : instruments_)
        if (i->id().kind == kind)
            out.push_back(i.get());
    return out;
}

Result<void> InstrumentRegistry::bindComponent(const InstrumentId& id, ComponentId component,
                                               std::vector<std::string> lines,
                                               std::vector<pulse::ChannelId> channels) {
    IInstrument* instrument = find(id);
    if (!instrument)
        return fail(err::UnknownInstrument, "no instrument " + id.toString());
    Bindings b = bindingsFor(instrument);
    b.component = component;
    b.lines = std::move(lines);
    b.channels = std::move(channels);
    instrument->bind(b);
    return {};
}

IInstrument* InstrumentRegistry::findByComponent(ComponentId component) const {
    for (auto const& i : instruments_)
        if (i->bindings().component == component && component.get() != 0)
            return i.get();
    return nullptr;
}

void InstrumentRegistry::setEventBus(core::EventBus* bus) {
    bus_ = bus;
    for (auto const& i : instruments_)
        i->bind(bindingsFor(i.get()));
}

Result<void> InstrumentRegistry::connectUpconversion(std::uint32_t awg, std::uint32_t port,
                                                     std::uint32_t generator, std::uint32_t mixer) {
    auto* a = get<Awg>("awg", awg);
    auto* g = get<Generator>("sg_mw", generator);
    auto* m = get<IqMixer>("iq_mixer", mixer);
    if (!a || !g || !m)
        return fail(err::UnknownInstrument,
                    std::format("up-conversion needs awg[{}], sg_mw[{}] and iq_mixer[{}]", awg,
                                generator, mixer));
    if (port >= Awg::kChannels)
        return fail(err::UnknownChannel, std::format("awg[{}] has no output ch[{}]", awg, port));
    graph_.addEdge(std::format("awg[{}].ch[{}]", awg, port), m->ifNode());
    graph_.addEdge(std::format("sg_mw[{}].rf", generator), m->loNode());
    m->attach(a, port, g);
    return {};
}

Result<void> InstrumentRegistry::attachLine(std::string upstreamNode, const std::string& lineId) {
    if (!graph_.hasNode(upstreamNode))
        return fail(err::BadRouting, std::format("routing: unknown node '{}'", upstreamNode));
    for (cryo::Stage stage : {cryo::Stage::PT1, cryo::Stage::PT2, cryo::Stage::STILL,
                              cryo::Stage::CP, cryo::Stage::MXC}) {
        taps_.push_back(std::make_unique<LineTap>(graph_, upstreamNode, hub_, lineId, stage));
        graph_.addNode(std::format("line.{}.{}", lineId, cryo::stageName(stage)),
                       taps_.back().get());
    }
    return {};
}

Result<void> InstrumentRegistry::createStandardSet(const StandardSetOptions& o) {
    auto many = [&](std::string_view kind, std::uint32_t count) -> Result<void> {
        for (std::uint32_t k = 0; k < count; ++k)
            QXL_TRY(create(kind));
        return {};
    };
    QXL_TRY(many("controller", 1));
    QXL_TRY(many("sg_mw", o.generators));
    QXL_TRY(many("awg", o.awgs));
    QXL_TRY(many("iq_mixer", o.mixers));
    for (std::string_view kind : {"digitizer", "vna", "spectrum_analyzer", "oscilloscope",
                                  "dc_source", "flow_meter", "power_meter"})
        QXL_TRY(many(kind, 1));
    // RuO₂ on the mixing chamber and cold plate, Cernox on the pulse-tube stages and the still
    // (spec 12 §8).
    for (const char* stage : {"MXC", "CP"}) {
        QXL_TRY_ASSIGN(IInstrument * t, create("thermometer_ruo2"));
        QXL_TRY(t->set("stage", std::string(stage)));
    }
    for (const char* stage : {"PT1", "PT2", "STILL"}) {
        QXL_TRY_ASSIGN(IInstrument * t, create("thermometer_cernox"));
        QXL_TRY(t->set("stage", std::string(stage)));
    }
    for (const char* node : {"ovc", "still", "condense", "dump"}) {
        QXL_TRY_ASSIGN(IInstrument * g, create("pressure_gauge"));
        QXL_TRY(g->set("node", std::string(node)));
    }
    if (o.probes)
        for (auto const& kind : kinds())
            if (kinds_.find(kind)->second.simulatorOnly)
                QXL_TRY(many(kind, 1));
    // Mixer m takes AWG output m (four per unit) and shares the generators round-robin.
    for (std::uint32_t m = 0; m < o.mixers && o.generators > 0 && m / Awg::kChannels < o.awgs; ++m)
        QXL_TRY(connectUpconversion(m / Awg::kChannels, m % Awg::kChannels, m % o.generators, m));
    if (o.powerOn)
        powerAll(true);
    return {};
}

void InstrumentRegistry::powerAll(bool on) {
    for (auto const& i : instruments_)
        (void)i->execute(Command::of(on ? Command::Kind::PowerOn : Command::Kind::PowerOff));
}

Result<void> InstrumentRegistry::execute(const InstrumentId& id, const Command& command) {
    IInstrument* instrument = find(id);
    if (!instrument)
        return fail(err::UnknownInstrument, "no instrument " + id.toString());
    return instrument->execute(command);
}

core::Json InstrumentRegistry::saveState() const {
    core::Json instruments = core::Json::array();
    for (auto const& i : instruments_)
        instruments.push_back({{"kind", i->id().kind},
                               {"index", i->id().index},
                               {"on", i->state() != State::Off},
                               {"settings", i->saveSettings()}});
    return core::Json{{"instruments", instruments}, {"routing", graph_.toJson()}};
}

Result<void> InstrumentRegistry::loadState(const core::Json& state) {
    if (!state.is_object() || !state.contains("instruments") || !state["instruments"].is_array())
        return fail(err::BadSchema, "instrument state: 'instruments' missing");
    std::size_t k = 0;
    for (auto const& j : state["instruments"]) {
        const std::string path = std::format("instruments[{}]", k++);
        if (!j.is_object() || !j.contains("kind") || !j["kind"].is_string())
            return fail(err::BadSchema, std::format("instrument state: '{}.kind' missing", path));
        IInstrument* instrument = find(j["kind"].get<std::string>(), j.value("index", 0u));
        if (!instrument)
            continue; // an instrument this rack does not have: tolerated
        if (j.contains("settings"))
            QXL_TRY(instrument->loadSettings(j["settings"]));
        (void)instrument->execute(
            Command::of(j.value("on", true) ? Command::Kind::PowerOn : Command::Kind::PowerOff));
    }
    if (state.contains("routing") && state["routing"].contains("edges"))
        for (auto const& e :
             state["routing"]["edges"]) // cables are restored by id; sources stay attached
            if (e.contains("id") && e["id"].is_string())
                (void)graph_.setConnected(e["id"].get<std::string>(), e.value("connected", true));
    return {};
}

} // namespace qlab::instr
