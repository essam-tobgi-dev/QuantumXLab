#pragma once
// Spec 19 §3 — "panels read snapshots and post commands; they never own model state". This is the
// snapshot half: one immutable-for-the-frame record holding non-owning pointers to everything the
// App has built, plus the command callbacks a panel may invoke. Every pointer may be null — a panel
// with no model draws its placeholder, which is what the headless frame test exercises.
#include "Compiler/Incremental.hpp"
#include "Core/EventBus.hpp"
#include "Core/JobSystem.hpp"
#include "Cryo/Sequencer.hpp"
#include "Data/Series.hpp"
#include "Graphics/Camera.hpp"
#include "Graphics/Renderer.hpp"
#include "Instruments/Live.hpp"
#include "Instruments/Registry.hpp"
#include "Lab/Lab.hpp"
#include "Runtime/Session.hpp"
#include "UI/Fonts.hpp"
#include "UI/Shortcuts.hpp"
#include "UI/Strings.hpp"
#include "UI/Theme.hpp"
#include "UI/Theory/TheoryIndex.hpp"
#include "UI/UndoStack.hpp"
#include "UI/Widgets/Equations.hpp"
#include "UI/Widgets/MathImGui.hpp"
#include "Viz/Viz.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace qlab::lab { class Tour; }

namespace qlab::ui {

struct UiResources;

// Spec 19 §2 top bar: what the run status shows, read once per frame from the session.
struct SessionView {
    enum class Status : std::uint8_t { Idle, Compiling, Running, Done, Failed };
    Status status = Status::Idle;
    std::uint64_t shotsDone = 0, shotsTotal = 0;
    double progress = 0.0;                 // 0 … 1, negative when indeterminate
    double lastWallMs = 0.0;
    std::string project = "Untitled project";
    std::string device, calibration;
    std::string backend = "auto";
    std::string backendReason;
    std::uint32_t errors = 0, warnings = 0;
    bool hasProgram = false, hasResult = false;

    std::string_view statusKey() const;    // strings.en.json key of the status text
};

// Commands a panel posts back to the App. An unset callback is a disabled control.
struct Commands {
    std::function<void()> run, stop, compile, stepGate, stepShot;
    std::function<void()> saveProject, exportResults, settings;
    std::function<void(std::string_view deviceId)> selectDevice;
    std::function<void(runtime::BackendChoice)> selectBackend;
    std::function<void(std::string source, std::string origin)> openProgram;
    // Diagnostics / theory links → the editor and the Theory Browser.
    std::function<void(const SourceSpan&)> gotoSource;
    std::function<void(std::string_view anchor)> openTheory;
    std::function<void(ComponentId)> selectComponent;
    std::function<void(const viz::ExportRequest&)> requestExport;
    // Spec 21 §1 / brief: the State Views panel merges `wants()` over the OPEN views only and posts
    // the union here; the App runs `viz::computeReductions` on the job system and publishes the
    // result in the next `ViewInput`. Views show the previous one with a stale badge until then.
    std::function<void(const viz::ReductionRequest&)> requestReductions;
    // Spec 18 §5 / 19 §4: the viewport FBO is recreated once the panel size has been stable for one
    // frame; the panel reports the new FRAMEBUFFER size (panel size × DPI scale) exactly then.
    std::function<void(int widthPx, int heightPx)> resizeViewport;
    // Spec 19 §5.8: a slim progress bar in the top bar; the UI never blocks.
    std::function<void(std::string_view message, double progress)> setBusy;
};

struct UiContext {
    // ---- presentation
    const Theme* theme = nullptr;               // required
    const UiResources* resources = nullptr;     // the owner of theme/fonts/logo (set by bind)
    const FontSet* fonts = nullptr;
    MathRenderers* math = nullptr;
    const TheoryAssets* assets = nullptr;
    theory::TheoryIndex* theoryIndex = nullptr;
    UndoStack* undo = nullptr;
    const Shortcuts* keys = nullptr;
    float dpiScale = 1.0f, fontScale = 1.0f;
    double timeS = 0.0, deltaS = 1.0 / 60.0;
    bool physicalLab = false;                   // spec 00 §6 / 19 §2
    bool reducedMotion = false, asciiKets = false;
    bool devMode = false;                       // QXL_DEV: FPS counter, perf tab

    // ---- model (owned by the App)
    runtime::Session* session = nullptr;
    core::EventBus* bus = nullptr;
    core::JobSystem* jobs = nullptr;
    compiler::IncrementalCompiler* incremental = nullptr;
    lab::Scene* scene = nullptr;
    lab::Interaction* interaction = nullptr;
    lab::Tour* tour = nullptr;                 // the guided tour (spec 17 §7.10); null when none is loaded
    lab::SceneRenderer* sceneRenderer = nullptr;
    lab::LabOverlays* overlays = nullptr;
    const lab::BindingRegistry* bindings = nullptr;
    instr::InstrumentRegistry* instruments = nullptr;
    instr::LiveRunner* liveRunner = nullptr;
    viz::SelectionModel* selection = nullptr;
    viz::GlBackend* gl = nullptr;
    std::vector<std::unique_ptr<viz::IStateView>>* views = nullptr;
    const viz::ViewInput* viewInput = nullptr;
    const data::Recorder* recorder = nullptr;
    gfx::Renderer* renderer = nullptr;
    gfx::Camera* camera = nullptr;
    cryo::CooldownSequencer* fridge = nullptr;
    cryo::ThermalNetwork* thermalNet = nullptr;   // heater set points are written here (spec 11 §8)
    const cryo::ThermalSnapshot* thermal = nullptr;
    const runtime::RunResult* result = nullptr;
    const runtime::Estimate* estimate = nullptr;
    const compiler::PassMetrics* metrics = nullptr;
    const std::vector<lang::Diagnostic>* diagnostics = nullptr;
    const std::vector<instr::Trace>* traces = nullptr;
    const hw::Device* device = nullptr;
    const hw::Calibration* calibration = nullptr;

    SessionView session_view;
    Commands cmd;

    // ---- helpers every panel uses
    const Theme& th() const { return *theme; }
    // ImGui works in LOGICAL points here: the GLFW backend reports the window size as DisplaySize
    // and the framebuffer ratio as DisplayFramebufferScale, so the platform already magnifies the
    // interface on a HiDPI display. Layout must therefore NOT be multiplied by `dpiScale` — that
    // is only how finely the fonts are rasterised. The user's font scale does scale layout.
    Metrics metrics_px() const { return theme->metricsAt(1.0f, fontScale); }
    // A size written in logical points, scaled by the user's font-size preference only.
    float ui(float logicalPoints) const { return logicalPoints * fontScale; }
    std::string_view text(std::string_view key) const { return Strings::global().get(key); }
    // Spec 21 §1: the draw context the state views consume, filled from this frame.
    viz::DrawContext drawContext(const viz::VizTheme& vizTheme) const;
};

} // namespace qlab::ui
