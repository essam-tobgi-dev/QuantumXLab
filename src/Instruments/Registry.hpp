#pragma once
// Spec 12 §1, §11 — the instrument registry: a factory keyed by the instrument ids
//   sg_mw, awg, iq_mixer, digitizer, vna, spectrum_analyzer, oscilloscope, thermometer_ruo2,
//   thermometer_cernox, pressure_gauge, flow_meter, power_meter, dc_source, controller
// plus the simulator-only probes `probe_*` (spec 12 §12). It owns the instruments, the shared
// InputHub, the routing matrix, and the fridge-line taps; it routes panel commands and answers the
// scalar `query(path)` that the App adapts into `instr.*` / `run.*` binding providers (Lab does not
// depend on Instruments). New kinds register a factory (spec 02 §7 extension point).
#include "Instruments/Instrument.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace qlab::instr {

// A fridge drive line seen at one stage: the upstream node's signal through the line's cumulative
// attenuation (attenuators and cable loss down to that stage), with the thermal noise density
// n̄(stage) h f that the photon budget of spec 11 §6 leaves there. Node "line.<id>.<STAGE>".
class LineTap final : public ISignalSource {
public:
    LineTap(const SignalGraph& graph, std::string upstream, std::shared_ptr<const InputHub> inputs, std::string lineId, cryo::Stage stage);
    Result<Signal> signal(std::string_view port, const SignalRequest& request) const override;
    double sampleRateHz(std::string_view port) const override;

private:
    const SignalGraph& graph_;
    std::string upstream_;
    std::shared_ptr<const InputHub> inputs_;
    std::string lineId_;
    cryo::Stage stage_;
};

struct StandardSetOptions {
    std::uint32_t generators = 4;  // rack A: mw_generator ×4
    std::uint32_t awgs = 2;        // control chassis: ⌈N_lines / 4⌉ units
    std::uint32_t mixers = 8;      // iq_mixer_board ×8
    bool probes = true;            // simulator-only probes (absent from the Physical-lab workspace)
    bool powerOn = true;
};

class InstrumentRegistry {
public:
    using Factory = std::function<std::unique_ptr<IInstrument>(std::uint32_t index)>;

    InstrumentRegistry(); // the built-in kinds are registered
    ~InstrumentRegistry();
    InstrumentRegistry(const InstrumentRegistry&) = delete;
    InstrumentRegistry& operator=(const InstrumentRegistry&) = delete;

    // ---- kinds
    void registerKind(std::string kind, Factory factory, bool simulatorOnly = false);
    bool hasKind(std::string_view kind) const;
    // `physicalLab`: without the simulator-only probes (spec 00 §6).
    std::vector<std::string> kinds(bool physicalLab = false) const;
    // {"instr/sg_mw.schema.json": {…}, …}: the settings schema files the descriptors name.
    core::Json schemas() const;

    // ---- instances
    // Creates the next instance of `kind`, bound to the registry's hub, bus and routing matrix.
    Result<IInstrument*> create(std::string_view kind);
    IInstrument* find(std::string_view kind, std::uint32_t index = 0) const;
    IInstrument* find(const InstrumentId& id) const { return find(id.kind, id.index); }
    template <class T> T* get(std::string_view kind, std::uint32_t index = 0) const { return dynamic_cast<T*>(find(kind, index)); }
    std::vector<IInstrument*> all(bool physicalLab = false) const;
    std::vector<IInstrument*> ofKind(std::string_view kind) const;
    // Spec 12 §11: the rack unit / sensor of the scene, its fridge lines and schedule channels.
    Result<void> bindComponent(const InstrumentId& id, ComponentId component, std::vector<std::string> lines = {},
                               std::vector<pulse::ChannelId> channels = {});
    IInstrument* findByComponent(ComponentId component) const;

    // ---- shared inputs and routing
    const std::shared_ptr<InputHub>& inputs() const { return hub_; }
    void setEventBus(core::EventBus* bus); // re-binds every instrument
    core::EventBus* eventBus() const { return bus_; }
    SignalGraph& routing() { return graph_; }
    const SignalGraph& routing() const { return graph_; }
    // AWG output → mixer IF, generator → mixer LO, plus the mixer's direct links (spec 12 §4).
    Result<void> connectUpconversion(std::uint32_t awg, std::uint32_t port, std::uint32_t generator, std::uint32_t mixer);
    // Adds the nodes "line.<lineId>.<STAGE>" (PT1 … MXC) fed by `upstreamNode` (a mixer's RF output).
    Result<void> attachLine(std::string upstreamNode, const std::string& lineId);

    // One of every instrument of the lab rack (layout sc_lab_standard), thermometers on every stage,
    // gauges on every gas node, the probes, and the standard up-conversion routing.
    Result<void> createStandardSet(const StandardSetOptions& options = {});
    void powerAll(bool on);

    // ---- commands and readings
    Result<void> execute(const InstrumentId& id, const Command& command);
    // Scalar reading for a binding path, with or without the root: "instr.gen[0].f", "gen[0].f",
    // "instr.awg.running", "instr.seq.shot", "instr.dig.ch[0].iq", "instr.mixer[1].lo_leak",
    // "instr.dc.ch[2].I", "instr.vna.span", "instr.sa.trace", "instr.scope.ch[0]",
    // "instr.ref.locked", "instr.trig.rate", "cryo.thermo[0].T", "cryo.gauge[1].p", "cryo.flow.n3",
    // "run.shot", "run.gate_cursor", "run.wall_time", "run.playhead", "run.running", "run.progress",
    // "probe.state.qubit[0].bloch[2]" (Simulator-only: nullopt when `physicalLab`), and the generic
    // "<kind>[i].<reading or setting>". nullopt when the path names nothing.
    std::optional<double> query(std::string_view path, bool physicalLab = false) const;

    // Settings of every instrument and the routing state, for the project file (spec 23).
    core::Json saveState() const;
    Result<void> loadState(const core::Json& state);

private:
    struct KindInfo { Factory factory; bool simulatorOnly = false; };
    Bindings bindingsFor(const IInstrument* existing) const;
    void attachThermometers();
    std::map<std::string, KindInfo, std::less<>> kinds_;
    std::vector<std::string> kindOrder_;
    std::vector<std::unique_ptr<IInstrument>> instruments_;
    std::vector<std::unique_ptr<LineTap>> taps_;
    std::shared_ptr<InputHub> hub_ = std::make_shared<InputHub>();
    core::EventBus* bus_ = nullptr;
    SignalGraph graph_;
};

} // namespace qlab::instr
