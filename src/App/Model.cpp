// Spec 02 §5 — the composition root: building the cryogenic stack, the laboratory scene, the
// instrument rack and the binding providers, and rebuilding them when the device changes.
#include "App/Model.hpp"
#include "App/Options.hpp"
#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include <algorithm>
#include <format>

namespace qlab::app {

// ---------------------------------------------------------------- cryogenics

CryoStack::CryoStack(cryo::MaterialCatalog mats, cryo::Wiring w)
    : materials(std::move(mats)), wiring(std::move(w)), loads(coax, materials),
      network(wiring, loads, materials), sequencer(network, ghs), budget(coax) {}

int CryoStack::advance(double dtSeconds) {
    carry_ += std::max(0.0, dtSeconds);
    int steps = 0;
    // A stalled frame must not replay minutes of fridge time: catch up at most one second.
    constexpr int kMaxSteps = 10;
    while (carry_ >= kStepSeconds && steps < kMaxSteps) {
        carry_ -= kStepSeconds;
        if (auto s = sequencer.step(kStepSeconds))
            snapshot = *s;
        ++steps;
    }
    if (carry_ > kMaxSteps * kStepSeconds)
        carry_ = 0.0;
    return steps;
}

void CryoStack::settle() {
    // A laboratory fridge is found cold and circulating. The 22 h cooldown of spec 11 §5 is what
    // `CooldownSequencer` simulates when the user asks for it; at start-up the network is placed at
    // the steady state of the running configuration and the sequencer walked to `Base` from there.
    sequencer.startCooldown();
    ghs.setPump("compressor_3he", true);
    ghs.setValve("v_condense", true);
    ghs.setPump("turbo_still", true);
    ghs.setValve("v_still_pump", true);
    ghs.setLn2Trap(true);
    network.cooling.pulseTubeOn = true;
    network.cooling.circulating = true;
    network.cooling.stillHeater_W = 25e-3;
    network.cooling.n3_mol_s = 1.0e-3;
    if (auto steady = network.steadyState()) {
        network.setTemperatures(steady->T_K);
        snapshot = *steady;
    }
    const std::size_t still = static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::STILL));
    for (int i = 0; i < 48; ++i)
        ghs.step(600.0, network.cooling.stillHeater_W, network.temperatures()[still]);
    for (int i = 0; i < 16 && sequencer.state() != cryo::FridgeState::Base; ++i) {
        if (auto s = sequencer.step(kStepSeconds))
            snapshot = *s;
    }
}

Result<cryo::Wiring> wiringFor(const hw::Device& device) {
    const std::filesystem::path file = device.directory / "wiring.json";
    if (std::filesystem::exists(file)) {
        auto loaded = cryo::loadWiring(file);
        if (loaded)
            return loaded;
        // Spec 11 is the dilution refrigerator: an ion trap's "wiring" is an optical path, and the
        // shipped ion files name chains (`optical_raman_individual`, `trap_rf_drive`, …) that
        // `cryo::ChainCatalog` does not define. That must not stop the laboratory from opening —
        // the ion layout has no fridge to draw anyway.
        QXL_LOG_WARN(Cryo, "{}: {}", file.string(), loaded.error().message);
        if (!hw::isTransmon(device.technology)) {
            cryo::Wiring empty;
            empty.device = device.id;
            return empty;
        }
    }
    // Spec 11 §4: without a usable file the standard chains are instantiated per channel.
    cryo::Wiring w;
    w.device = device.id;
    const auto add = [&](std::string_view chain, std::string id, std::string channel) {
        if (auto line = cryo::ChainCatalog::instantiate(chain, std::move(id), std::move(channel),
                                                        w.stageLengths_m))
            w.lines.push_back(std::move(*line));
    };
    for (std::uint32_t q = 0; q < device.qubitCount(); ++q) {
        add("drive_std", std::format("d{}", q), std::format("d[{}]", q));
        if (device.technology == hw::Technology::TransmonTunable ||
            device.technology == hw::Technology::TransmonTunableCoupler)
            add("flux_std", std::format("f{}", q), std::format("f[{}]", q));
    }
    for (const hw::Feedline& f : device.readout.feedlines) {
        add("readout_in_std", std::format("m{}", f.id), std::format("m[{}]", f.id));
        add("readout_out_std", std::format("a{}", f.id), std::format("a[{}]", f.id));
    }
    return w;
}

Result<std::unique_ptr<CryoStack>> makeCryoStack(const hw::Device& device) {
    QXL_TRY_ASSIGN(cryo::MaterialCatalog mats, cryo::MaterialCatalog::load());
    QXL_TRY_ASSIGN(cryo::Wiring wiring, wiringFor(device));
    auto stack = std::make_unique<CryoStack>(std::move(mats), std::move(wiring));
    stack->settle();
    return stack;
}

// ---------------------------------------------------------------- laboratory

LabStack::LabStack(lab::Scene s, const lab::BindingRegistry& bindings)
    : scene(std::move(s)), interaction(scene, &bindings), renderer(scene),
      overlays(scene, bindings) {}

// ---------------------------------------------------------------- model

// LabModel's constructor and destructor live in ModelTick.cpp, where `ReductionJob` is complete.

Result<std::unique_ptr<LabModel>> LabModel::create(Config config) {
    auto model = std::unique_ptr<LabModel>(new LabModel());
    model->buildLab_ = config.buildLab;
    model->layout_ = config.layout.empty() ? defaultLayoutFor(config.device) : config.layout;
    model->live_state_->physicalLab = config.physicalLab;
    model->live_state_->registry = config.instruments ? &model->instruments_ : nullptr;
    model->views_ = viz::makeAllViews();
    model->recordChannels();
    registerBindingProviders(model->bindings_, model->live_state_, model->statics_);
    if (config.instruments)
        model->instruments_.setEventBus(&model->bus_);
    QXL_TRY(model->selectDevice(config.device));
    if (config.instruments) {
        instr::StandardSetOptions opts;
        opts.probes = true; // the registry hides them in Physical-lab mode (spec 12 §12)
        QXL_TRY(model->instruments_.createStandardSet(opts));
        QXL_TRY(model->bindInstruments());
    }
    model->subscribeEvents();
    model->publishInputs();
    return model;
}

void LabModel::subscribeEvents() {
    // Spec 02 §6 / 15 §1: the session's events are drained on the main thread and land in the one
    // record the panels and the bindings read.
    subs_.push_back(bus_.subscribe<runtime::RunStarted>([this](const runtime::RunStarted& e) {
        LiveState& s = *live_state_;
        s.running = true;
        s.shotsDone = 0;
        s.shotsTotal = e.shots;
        s.seed = e.seed;
    }));
    subs_.push_back(bus_.subscribe<runtime::RunProgress>([this](const runtime::RunProgress& e) {
        live_state_->shotsDone = e.done;
        live_state_->shotsTotal = e.total;
    }));
    subs_.push_back(bus_.subscribe<runtime::RunFinished>(
        [this](const runtime::RunFinished&) { live_state_->running = false; }));
    // Spec 15 §3.5 (c) / 11 §5: a pulse-level run publishes the average power it puts on each
    // fridge line at 10 Hz; the thermal network re-solves its attenuator dissipations from it, so
    // the stage temperatures respond to the program that is playing.
    subs_.push_back(bus_.subscribe<runtime::LinePowerEvent>(
        [this](const runtime::LinePowerEvent& e) { applyLinePowers(e.powers); }));
}

Status LabModel::selectDevice(std::string_view id) {
    QXL_TRY(session_.selectDevice(id));
    const hw::Device* device = session_.device();
    if (device == nullptr)
        return fail(ErrorCode::NotFound, std::format("device '{}' did not load", id));
    QXL_TRY_ASSIGN(cryo_, makeCryoStack(*device));
    LiveState& s = *live_state_;
    s.device = viz::borrow(*device);
    s.calibration = viz::borrow(*session_.calibration());
    s.wiring = &cryo_->wiring;
    s.coax = &cryo_->coax;
    s.budget = &cryo_->budget;
    s.heat = &cryo_->loads;
    s.materials = &cryo_->materials;
    s.cooling = cryo_->network.cooling;
    s.thermal = cryo_->snapshot;
    s.ghs = cryo_->ghs.snapshot();
    s.fridge = cryo_->sequencer.state();
    incremental_.setTarget(device, session_.calibration(), {});
    // Spec 17 §5: constants of the device and its layout that no snapshot carries.
    statics_.setNumber("device.qubits", static_cast<double>(device->qubitCount()));
    statics_.setNumber("device.edges", static_cast<double>(device->edges.size()));
    layout_ = defaultLayoutFor(device->id);
    if (buildLab_)
        QXL_TRY(rebuildScene());
    return {};
}

Status LabModel::rebuildScene() {
    const hw::Device* device = session_.device();
    if (device == nullptr)
        return fail(ErrorCode::NotFound, "no device selected");
    lab::BuildOptions options;
    options.deviceOverride = device->id;
    QXL_TRY_ASSIGN(lab::Scene scene, lab::buildScene(layout_, options));
    diagnostics_ = scene.diagnostics();
    tour_.reset(); // points into the scene being replaced
    lab_ = std::make_unique<LabStack>(std::move(scene), bindings_);
    // Spec 21 §1.1: the qubit ↔ component map is read once, at device load.
    selection_.bindScene(lab_->scene);
    for (const std::string& d : diagnostics_)
        QXL_LOG_DEBUG(App, "scene: {}", d);
    if (auto st = loadTour(); !st)
        QXL_LOG_WARN(App, "tour: {}", st.error().message);
    return {};
}

Status LabModel::loadTour() {
    tour_.reset();
    if (lab_ == nullptr)
        return {};
    const std::filesystem::path file = core::assetDir() / "Lab" / "Tours" / (layout_ + ".json");
    if (!std::filesystem::exists(file))
        return {}; // a layout without a tour is not an error
    QXL_TRY_ASSIGN(lab::Tour tour,
                   lab::Tour::load(file, lab_->scene, lab_->interaction, &bindings_));
    tour.setPhysicalLab(live_state_->physicalLab);
    for (const std::string& w : tour.warnings())
        QXL_LOG_INFO(App, "tour: {}", w);
    tour_ = std::make_unique<lab::Tour>(std::move(tour));
    return {};
}

Status LabModel::bindInstruments() {
    // Spec 12 §11: an instrument is bound to the rack unit or sensor that represents it in the
    // scene, and to the fridge lines its output reaches. The kind ↔ component mapping comes from
    // the descriptors, so a new instrument needs no code here (spec 02 §7).
    QXL_TRY_ASSIGN(auto descriptors, instr::loadInstrumentDescriptors());
    std::map<std::string, std::string, std::less<>> componentOf;
    for (const instr::InstrumentDescriptor& d : descriptors)
        componentOf[d.kind] = d.componentId;

    if (lab_ != nullptr) {
        std::map<std::string, std::size_t, std::less<>> used;
        for (instr::IInstrument* i : instruments_.all()) {
            const auto it = componentOf.find(i->id().kind);
            if (it == componentOf.end())
                continue;
            const std::vector<ComponentId> nodes = lab_->scene.findByDescriptor(it->second);
            const std::size_t k = used[it->second]++;
            if (k >= nodes.size())
                continue;
            (void)instruments_.bindComponent(i->id(), nodes[k]);
        }
    }
    // Spec 12 §1/§11: thermometry, pressure and flow are always reading in a working laboratory, so
    // their live channels are on from the start (`cryo.thermo[i].T` and friends have no value until
    // the sensor has acquired once). Every other channel goes live only when the user asks.
    for (const char* kind :
         {"thermometer_ruo2", "thermometer_cernox", "pressure_gauge", "flow_meter"})
        for (instr::IInstrument* i : instruments_.ofKind(kind))
            for (const instr::ChannelDesc& channel : i->channels())
                if (channel.name == "T" || channel.name == "p" || channel.name == "n3")
                    (void)live_.enable(i->id(), channel.name, true);

    // The fridge lines fed by each up-converted mixer output (spec 12 §4, §11).
    if (live_state_->wiring != nullptr) {
        std::uint32_t mixer = 0;
        for (const cryo::WiringLine& line : live_state_->wiring->lines) {
            if (line.kind == cryo::LineKind::ReadoutOut || line.kind == cryo::LineKind::DC)
                continue;
            const std::string node = std::format("iq_mixer[{}].rf", mixer);
            if (!instruments_.routing().hasNode(node))
                break;
            (void)instruments_.attachLine(node, line.id);
            ++mixer;
        }
    }
    return {};
}

void LabModel::setPhysicalLab(bool on) {
    live_state_->physicalLab = on;
    if (lab_ != nullptr)
        lab_->overlays.setEnabled(lab_->overlays.temperatureTint(), true, !on, true);
    if (tour_ != nullptr)
        tour_->setPhysicalLab(on); // Simulator-only stops are skipped (spec 19 §2)
}

} // namespace qlab::app
