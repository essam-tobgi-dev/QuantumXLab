#pragma once
// Spec 02 §5 — the composition root. `LabModel` owns every subsystem the application binds
// together and knows nothing about windows, OpenGL or ImGui: the headless CLI modes (`--run`,
// `--selftest`) and the whole test suite drive the same object the GUI does.
//
// What the App owns because the lower modules are deliberately decoupled (SPEC_DEVIATIONS):
//   · `instr::RunView` / `instr::Environment` — Instruments does not depend on Runtime (§tick).
//   · `lab::BindingRegistry` providers for cryo / wiring / device / instr / run / static —
//     Lab depends on neither Runtime nor Instruments (Bindings.hpp).
//   · `viz::ViewInput` and the off-thread `viz::computeReductions` — Viz does not depend on
//     Runtime (Reductions.hpp).
//   · the per-line drive power of a pulse-level run, forwarded to the cryo thermal network.
#include "App/Live.hpp"
#include "Compiler/Incremental.hpp"
#include "Cryo/Cryo.hpp"
#include "Data/Series.hpp"
#include "Instruments/Instruments.hpp"
#include "Lab/Lab.hpp"
#include "Lab/Tour.hpp"
#include "Runtime/Runtime.hpp"
#include "Viz/Viz.hpp"
#include <memory>
#include <string>
#include <vector>

namespace qlab::app {

// The cryogenic stack. Its members hold references to each other (`ThermalNetwork` to the wiring,
// the heat-load model and the material catalog), so the bundle is built in one place, never moved,
// and rebuilt wholesale when the device changes.
class CryoStack {
  public:
    CryoStack(cryo::MaterialCatalog mats, cryo::Wiring w);
    CryoStack(const CryoStack&) = delete;
    CryoStack& operator=(const CryoStack&) = delete;

    cryo::MaterialCatalog materials;
    cryo::CoaxCatalog coax;
    cryo::Wiring wiring;
    cryo::HeatLoadModel loads;    // references `coax` and `materials`
    cryo::ThermalNetwork network; // references `wiring`, `loads` and `materials`
    cryo::GasHandlingSystem ghs;
    cryo::CooldownSequencer sequencer; // references `network` and `ghs`
    cryo::NoiseBudget budget;          // references `coax`
    cryo::ThermalSnapshot snapshot;

    // Spec 11 §8: the fridge is stepped at 10 Hz of lab time, whatever the frame rate.
    static constexpr double kStepSeconds = 0.1;
    // Advances the fridge by whole 10 Hz steps and refreshes `snapshot`. Returns the number taken.
    int advance(double dtSeconds);
    // Starts at the steady state of the current wiring and cooling parameters (a cold fridge).
    void settle();

  private:
    double carry_ = 0.0;
};

// Wiring for a device: `<device>/wiring.json` when it ships one, else the standard chains of
// spec 11 §4 instantiated for the device's drive, flux, readout and DC channels.
Result<cryo::Wiring> wiringFor(const hw::Device& device);
Result<std::unique_ptr<CryoStack>> makeCryoStack(const hw::Device& device);

// The 3D laboratory. `Interaction`, `SceneRenderer` and `LabOverlays` all point at `scene`, so the
// bundle is likewise built once and replaced wholesale.
class LabStack {
  public:
    LabStack(lab::Scene s, const lab::BindingRegistry& bindings);
    LabStack(const LabStack&) = delete;
    LabStack& operator=(const LabStack&) = delete;

    lab::Scene scene;
    lab::Interaction interaction;
    lab::SceneRenderer renderer;
    lab::LabOverlays overlays;
};

class LabModel {
  public:
    struct Config {
        std::string device = "sc_fixed_5";
        std::string layout;   // empty: `defaultLayoutFor(device)`
        bool buildLab = true; // `--run` needs no scene
        bool instruments = true;
        bool physicalLab = false;
    };

    static Result<std::unique_ptr<LabModel>> create(Config config);
    ~LabModel();
    LabModel(const LabModel&) = delete;
    LabModel& operator=(const LabModel&) = delete;

    // ---- subsystems
    core::EventBus& bus() { return bus_; }
    core::JobSystem& jobs() { return *jobs_; }
    runtime::Session& session() { return session_; }
    compiler::IncrementalCompiler& incremental() { return incremental_; }
    instr::InstrumentRegistry& instruments() { return instruments_; }
    instr::LiveRunner& live() { return live_; }
    lab::BindingRegistry& bindings() { return bindings_; }
    lab::StaticProvider& statics() { return statics_; }
    CryoStack& cryo() { return *cryo_; }
    LabStack* lab() { return lab_.get(); }
    lab::Scene* scene() { return lab_ ? &lab_->scene : nullptr; }
    lab::Interaction* interaction() { return lab_ ? &lab_->interaction : nullptr; }
    lab::SceneRenderer* sceneRenderer() { return lab_ ? &lab_->renderer : nullptr; }
    lab::LabOverlays* overlays() { return lab_ ? &lab_->overlays : nullptr; }
    // Spec 17 §7.10: the guided tour of the current layout (`Assets/Lab/Tours/<layout>.json`);
    // null when the layout ships none or the script failed to load (the log says why).
    lab::Tour* tour() { return tour_.get(); }
    viz::SelectionModel& selection() { return selection_; }
    std::vector<std::unique_ptr<viz::IStateView>>& views() { return views_; }
    const viz::ViewInput& viewInput() const { return viewInput_; }
    data::Recorder& recorder() { return recorder_; }
    LiveState& state() { return *live_state_; }
    const LiveState& state() const { return *live_state_; }

    // ---- selections
    const hw::Device* device() const { return session_.device(); }
    const hw::Calibration* calibration() const { return session_.calibration(); }
    const std::string& layoutId() const { return layout_; }
    // Loads the device, rebuilds the wiring, the cryogenic stack, the lab scene and the instrument
    // bindings, and republishes the environment. Diagnostics go to the log.
    Status selectDevice(std::string_view id);
    Status rebuildScene();
    Status loadTour(); // (re)loads the layout's tour script against the current scene
    void setPhysicalLab(bool on);
    bool physicalLab() const { return live_state_->physicalLab; }

    double labClock() const { return live_state_->labTimeS; }
    const std::vector<std::string>& diagnostics() const { return diagnostics_; }

    // ---- the run the UI is looking at
    void setResult(std::shared_ptr<const runtime::RunResult> r);
    const runtime::RunResult* result() const { return live_state_->result.get(); }
    void setCompiled(std::shared_ptr<const compiler::CompiledProgram> p);
    const compiler::CompiledProgram* compiled() const { return live_state_->compiled.get(); }
    // Spec 15 §3.6: the gate the views and the lab overlays are showing.
    void setPlayhead(std::uint64_t gateIndex);
    std::uint64_t playhead() const { return live_state_->playheadGate; }

    // ---- one frame (spec 02 §6). Advances the lab clock, steps the fridge at 10 Hz, ticks live
    // instrument acquisitions, republishes `instr::RunView`/`Environment`, refreshes `ViewInput`
    // and collects a finished reduction job. Never blocks.
    void tick(double dtSeconds);
    // The camera half of a frame: a playing tour flies and drifts the camera (spec 17 §7.10),
    // otherwise the interaction advances the pending focus flight (spec 17 §7.3).
    void tickLab(double dtSeconds, gfx::Camera& camera);

    // Spec 21 §1: the merged `wants()` of the open views; the reduction runs on the job system.
    void requestReductions(const viz::ReductionRequest& request);
    bool reductionsInFlight() const;
    // Waits for an in-flight reduction (tests and `--selftest`).
    void waitReductions();

    // Spec 15 §3.5 (c): per-line drive power of a pulse-level run → the thermal network.
    void applyLinePowers(const cryo::LinePowers& powers);

  private:
    LabModel();
    Status bindInstruments();
    void subscribeEvents();
    void publishInputs();
    void refreshViewInput();
    void collectReductions();
    void recordChannels(); // spec 22 §1: the fridge channels the Plots panel draws
    void recordSamples();

    core::EventBus bus_;
    core::JobSystem* jobs_ = nullptr;
    std::shared_ptr<LiveState> live_state_;
    std::unique_ptr<CryoStack> cryo_;
    // `instr::CryoCatalogs` owns a coax catalogue and the noise budget that points into it, and is
    // neither copyable nor movable: one instance is shared by every Environment the App publishes.
    std::shared_ptr<const instr::CryoCatalogs> catalogs_ =
        std::make_shared<const instr::CryoCatalogs>();
    lab::BindingRegistry bindings_;
    lab::StaticProvider statics_;
    std::unique_ptr<LabStack> lab_;
    std::unique_ptr<lab::Tour> tour_; // after `lab_`: it points into the scene
    instr::InstrumentRegistry instruments_;
    instr::LiveRunner live_;
    runtime::Session session_;
    compiler::IncrementalCompiler incremental_;
    data::Recorder recorder_;
    viz::SelectionModel selection_;
    std::vector<std::unique_ptr<viz::IStateView>> views_;
    viz::ViewInput viewInput_;
    std::array<data::ChannelId, cryo::kStageCount> stageChannels_{};
    data::ChannelId mxcLoadChannel_{0};
    std::string layout_;
    std::vector<std::string> diagnostics_;
    bool buildLab_ = true;

    struct ReductionJob;
    std::unique_ptr<ReductionJob> reduction_;
    std::vector<core::Subscription> subs_;
};

} // namespace qlab::app
