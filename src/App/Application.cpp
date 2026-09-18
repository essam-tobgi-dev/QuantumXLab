// Spec 02 §6 / 18 §1 / 19 §2 — window, GL context, ImGui backends, model and main loop
// (see Application.hpp).
#include "App/Application.hpp"
#include "App/Model.hpp"
#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include "Core/Timer.hpp"
#include "Core/Version.hpp"
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <format>
#include <imgui.h>
#include <implot.h>

namespace qlab::app {
namespace {

// Spec 18 §1: the GL version the shaders are written against (GLSL 410 core, spec 18 §2).
constexpr const char* kGlslVersion = "#version 410 core";

// Spec 19 §1 `bg.viewport`: the colour behind the tone-mapped laboratory image.
gfx::RendererDesc labRendererDesc(const ui::Theme& theme) {
    gfx::RendererDesc d;
    d.samples = 4;
    d.shadowSize = 4096;         // spec 18 §4: one tight-fitted map, 16-tap rotated Poisson PCF
    d.ibl = true;                // spec 18 §4: the procedural laboratory environment lights the metals
    d.ssao = true;               // spec 18 §5 pass 8
    d.bloom = true;              // spec 18 §5: LEDs and pulse packets glow
    d.aoStrength = 0.8f;
    d.bloomStrength = 0.12f;
    const glm::vec4 clear = theme[ui::Token::BgViewport];
    d.clearColor = glm::vec3(clear);
    return d;
}

} // namespace

Application::~Application() {
    // The subscriptions point into the model's event bus, so they go first: their destructor
    // unsubscribes, and the bus must still exist when it does.
    subs_.clear();
    // Spec 02 §6: cancel the workers, then tear the GL and ImGui state down in creation order.
    if (model_ != nullptr) {
        model_->incremental().cancel();
        model_->live().waitIdle();
        model_->waitReductions();
    }
    if (projects_ != nullptr) projects_->discardAutosave();
    if (model_ != nullptr && model_->sceneRenderer() != nullptr) model_->sceneRenderer()->releaseGpu();
    if (imgui_ != nullptr) {
        ImGui::SetCurrentContext(imgui_);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }
    if (implot_ != nullptr) ImPlot::DestroyContext(implot_);
    if (imgui_ != nullptr) ImGui::DestroyContext(imgui_);
    projects_.reset();
    model_.reset();
    renderer_.reset();
    gl_.reset();
    window_.reset();
}

Result<std::unique_ptr<Application>> Application::create(Options options, bool visible) {
    auto app = std::unique_ptr<Application>(new Application());
    app->options_ = std::move(options);
    QXL_TRY(app->initWindow(visible));
    QXL_TRY(app->initImGui());
    QXL_TRY(app->initModel());
    app->wireCommands();
    app->subscribeEvents();
    return app;
}

Status Application::initWindow(bool visible) {
    gfx::WindowDesc desc;
    desc.width = 1600;
    desc.height = 1000;
    desc.title = std::format("QuantumXLab {}", core::version());
    desc.visible = visible;
    desc.vsync = visible;
    // Everything this application puts on the default framebuffer is already a DISPLAY value: the
    // renderer tone-maps into an RGBA8 texture (spec 18 §4) and every ImGui colour is a theme token
    // in sRGB (spec 19 §1). An sRGB-encoding default framebuffer would gamma-encode both a second
    // time — measured: `bg.panel` #161A20 reached the screen as #535A63.
    desc.srgb = false;
    QXL_TRY_ASSIGN(window_, gfx::Window::create(desc));
    window_->makeCurrent();
    if (!gfx::loadGl(nullptr)) return fail(err::NoContext, "the OpenGL entry points did not load");
    dpiScale_ = std::max(1.0f, window_->contentScale());
    viewportW_ = std::max(64, window_->framebufferWidth() - 320);
    viewportH_ = std::max(64, window_->framebufferHeight() - 200);
    QXL_LOG_INFO(Gfx, "OpenGL {}.{} — {}", window_->caps().major, window_->caps().minor, window_->caps().renderer);

    // Spec 21 §1.2: one GL backend per context, created before the laboratory renderer so that the
    // state views and the viewport each own their framebuffers.
    viz::GlBackendDesc backend;
    backend.samples = 4;
    QXL_TRY_ASSIGN(gl_, viz::GlBackend::create(backend));
    return {};
}

Status Application::initImGui() {
    IMGUI_CHECKVERSION();
    imgui_ = ImGui::CreateContext();
    implot_ = ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(window_->windowWidth()), static_cast<float>(window_->windowHeight()));
    ui::Shell::configureImGui();
    if (auto st = resources_.loadAssets("dark"); !st)
        QXL_LOG_WARN(Ui, "assets: {}", st.error().message);   // fallbacks are in place (spec 19 §1)
    if (auto st = resources_.applyScale(io.Fonts, dpiScale_, fontScale_); !st)
        QXL_LOG_WARN(Ui, "fonts: {}", st.error().message);
    ui::Strings::setGlobal(ui::Strings::load().value_or(ui::Strings{}));

    QXL_TRY_ASSIGN(renderer_, gfx::Renderer::create(labRendererDesc(resources_.theme)));
    // A lit room (spec 18 §4): a soft key light from above, the environment's irradiance and
    // reflections through the IBL, and the exposure the spec recommends for that environment
    // (E/π ≈ 1.4 from the panels: sunlit white ≈ 2 HDR at exposure 1). The constant ambient only serves the `ibl = false` path.
    renderer_->setSun(glm::normalize(glm::vec3(-0.35f, -0.85f, -0.4f)), glm::vec3(1.0f, 0.98f, 0.94f), 3.0f);
    renderer_->setAmbient(glm::vec3(0.22f));
    renderer_->setExposure(0.55f);

    if (!ImGui_ImplGlfw_InitForOpenGL(window_->handle(), true)) return fail(err::NoContext, "ImGui GLFW backend");
    if (!ImGui_ImplOpenGL3_Init(kGlslVersion)) return fail(err::NoContext, "ImGui OpenGL backend");
    return {};
}

Status Application::initModel() {
    LabModel::Config config;
    config.device = options_.device;
    config.layout = options_.layout;
    config.physicalLab = options_.physicalLab;
    QXL_TRY_ASSIGN(model_, LabModel::create(config));
    projects_ = std::make_unique<ProjectHost>(*model_);
    shell_.setPhysicalLab(options_.physicalLab);
    model_->setPhysicalLab(options_.physicalLab);

    // Frame the laboratory on its overview bookmark (spec 17 §7).
    camera_.setAspect(static_cast<double>(viewportW_) / std::max(1, viewportH_));
    if (lab::Interaction* ui = model_->interaction(); ui != nullptr) {
        if (!ui->applyBookmark("Overview", camera_, 0.0) && model_->scene() != nullptr)
            camera_.frame(model_->scene()->nodes().front().subtreeBounds);
        lab::fitClipPlanes(camera_);
    }
    if (!options_.project.empty()) {
        if (auto st = projects_->open(options_.project); !st) {
            QXL_LOG_ERROR(App, "project: {}", st.error().format());
            lastError_ = st.error().message;
        } else {
            setProgramSource(projects_->mainSource());
        }
    }
    if (programSource().empty()) {
        // Spec 26 phase 3: the application opens on the example the vertical slice starts from.
        const std::filesystem::path bell = core::assetDir() / "Programs" / "Examples" / "Basics" / "bell.qasm";
        if (auto text = core::readTextFile(bell)) setProgramSource(std::move(*text));
    }
    return {};
}

void Application::showWorkspace(ui::Workspace w, int settleFrames) {
    shell_.setWorkspace(w);
    for (int i = 0; i < settleFrames; ++i) frame(1.0 / 60.0);
}

int Application::run() {
    core::Timer clock;
    double last = 0.0;
    while (!window_->shouldClose()) {
        const double now = clock.seconds();
        const double dt = frame_ == 0 ? 1.0 / 60.0 : std::clamp(now - last, 1e-4, 0.25);
        last = now;
        frame(dt);
    }
    // Spec 02 §6: a graceful shutdown cancels every job and drops the autosave.
    if (projects_->dirty()) (void)projects_->autosaveNow();
    model_->incremental().cancel();
    model_->live().waitIdle();
    model_->waitReductions();
    if (runPending_) model_->session().cancel(runHandle_);
    projects_->discardAutosave();
    return 0;
}

int runGui(const Options& options) {
    auto app = Application::create(options, true);
    if (!app) {
        QXL_LOG_ERROR(App, "{}", app.error().format());
        return 1;
    }
    return (*app)->run();
}

} // namespace qlab::app
