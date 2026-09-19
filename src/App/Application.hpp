#pragma once
// Spec 02 §6 / 18 §1 / 19 §2 — the application: one window with its GL context, the laboratory
// renderer, the Dear ImGui backends, the shell with its panels, and the main loop that ties the
// fixed UI frame to the worker jobs.
//
// Frame order (the reason it is this way):
//   1. poll input, drain the event bus on the main thread (spec 04 §4),
//   2. advance the model: lab clock, fridge at 10 Hz, live acquisitions, ViewInput (Model.hpp),
//   3. render the laboratory into the renderer's own framebuffer — BEFORE the ImGui frame, because
//      the Viewport panel records `renderer->colorTexture()` while it draws and a resize between
//      the two would leave that texture name dangling,
//   4. build the ImGui frame (`Shell::draw`), during which the GL state views render into their
//      own canvases through `viz::GlBackend`,
//   5. present: the default framebuffer, then `ImGui_ImplOpenGL3_RenderDrawData`.
#include "App/Options.hpp"
#include "App/ProjectHost.hpp"
#include "Graphics/Graphics.hpp"
#include "UI/UI.hpp"
#include "Viz/GlCanvas.hpp"
#include <filesystem>
#include <memory>
#include <string>

struct ImGuiContext;
struct ImPlotContext;

namespace qlab::app {

class Application {
  public:
    // `visible = false` gives a hidden window: the `--selftest` mode and the tests use it. Fails
    // with `err::NoContext` when no GL context can be created at all.
    static Result<std::unique_ptr<Application>> create(Options options, bool visible = true);
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // The main loop: runs until the window closes, then shuts down (jobs cancelled, autosave
    // dropped). Returns the process exit code.
    int run();
    // One whole frame, from input to buffer swap. `--selftest` and the tests call this directly.
    void frame(double dtSeconds);

    LabModel& model() { return *model_; }
    ProjectHost& projects() { return *projects_; }
    ui::Shell& shell() { return shell_; }
    ui::UiContext& context() { return ctx_; }
    gfx::Window& window() { return *window_; }
    gfx::Renderer& renderer() { return *renderer_; }
    gfx::Camera& camera() { return camera_; }
    viz::GlBackend* gl() { return gl_.get(); }

    // ---- commands the shell posts (AppCommands.cpp)
    void requestRun();
    void requestStop();
    void requestCompile();
    void stepGate(int delta);
    void stepShot(int delta);
    Status openProgramFile(const std::filesystem::path& file);
    void setProgramSource(std::string source);
    std::string programSource() const;
    Status saveProject();
    Status exportResults(const std::filesystem::path& directory);

    // ---- captures (spec 23 §8)
    // Renders the laboratory at `width × height` and writes it as PNG with the annotation strip.
    Status captureViewport(const std::filesystem::path& png, int width = 0, int height = 0,
                           bool annotate = true);
    // The whole window as the user sees it (the ImGui frame included), for `--selftest`.
    Status captureWindow(const std::filesystem::path& png);

    // Sets the workspace and lets the dock layout settle, so a capture shows the finished layout.
    void showWorkspace(ui::Workspace w, int settleFrames = 3);

  private:
    Application() = default;
    Status initWindow(bool visible);
    Status initImGui();
    Status initModel();
    void wireCommands();
    void subscribeEvents();
    void refreshContext(double dtSeconds);
    void renderLab();
    void present();
    void pollSession();

    Options options_;
    std::unique_ptr<gfx::Window> window_;
    std::unique_ptr<viz::GlBackend> gl_;      // created first: one per GL context (spec 21 §1.2)
    std::unique_ptr<gfx::Renderer> renderer_; // the laboratory viewport (spec 18 §4)
    ImGuiContext* imgui_ = nullptr;
    ImPlotContext* implot_ = nullptr;
    ui::UiResources resources_;
    ui::Shell shell_;
    ui::UiContext ctx_;
    std::unique_ptr<LabModel> model_;
    std::unique_ptr<ProjectHost> projects_;
    gfx::Camera camera_;
    std::vector<core::Subscription> subs_;

    int viewportW_ = 1280, viewportH_ = 720; // framebuffer pixels of the lab viewport
    int pendingW_ = 0, pendingH_ = 0;
    float dpiScale_ = 1.0f, fontScale_ = 1.0f;
    double timeS_ = 0.0;
    std::uint64_t frame_ = 0;

    // In-flight session handles (spec 15 §1): one compile and one run at a time from the UI.
    runtime::CompileHandle compile_{0};
    runtime::RunHandle runHandle_{0};
    bool compilePending_ = false, runPending_ = false;
    bool runAfterCompile_ = false;
    std::string lastError_;

    // What the panels read through the context but the App collects.
    std::vector<lang::Diagnostic> diagnostics_;
    std::optional<compiler::PassMetrics> metrics_;
    std::vector<instr::Trace> traces_;   // the last `kMaxTraces` live acquisitions
    std::uint64_t editorGeneration_ = 0; // last `IncrementalCompiler::Update` taken
    static constexpr std::size_t kMaxTraces = 64;
};

// Spec 03 §3: the GUI mode of the executable.
int runGui(const Options& options);

} // namespace qlab::app
