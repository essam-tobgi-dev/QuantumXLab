// Spec 02 §6 — one frame: drain, advance the model, render the laboratory, build the ImGui frame,
// present (see Application.hpp for why the order is this way).
#include "App/Application.hpp"
#include "Core/Log.hpp"
#include "Graphics/GlLoader.hpp"
#include "Report/Report.hpp"
#include <algorithm>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <format>
#include <imgui.h>

namespace qlab::app {
namespace {

ui::SessionView::Status statusOf(bool compiling, bool running, bool hasResult, bool failed) {
    if (failed)
        return ui::SessionView::Status::Failed;
    if (running)
        return ui::SessionView::Status::Running;
    if (compiling)
        return ui::SessionView::Status::Compiling;
    return hasResult ? ui::SessionView::Status::Done : ui::SessionView::Status::Idle;
}

} // namespace

void Application::refreshContext(double dtSeconds) {
    resources_.bind(ctx_, dpiScale_, fontScale_);
    ctx_.timeS = timeS_;
    ctx_.deltaS = dtSeconds;
#if defined(QXL_DEV)
    ctx_.devMode = true;
#endif
    LabModel& m = *model_;
    ctx_.session = &m.session();
    ctx_.bus = &m.bus();
    ctx_.jobs = &m.jobs();
    ctx_.incremental = &m.incremental();
    ctx_.scene = m.scene();
    ctx_.interaction = m.interaction();
    ctx_.tour = m.tour();
    ctx_.sceneRenderer = m.sceneRenderer();
    ctx_.overlays = m.overlays();
    ctx_.bindings = &m.bindings();
    ctx_.instruments = &m.instruments();
    ctx_.liveRunner = &m.live();
    ctx_.selection = &m.selection();
    ctx_.gl = gl_.get();
    ctx_.views = &m.views();
    ctx_.viewInput = &m.viewInput();
    ctx_.recorder = &m.recorder();
    ctx_.renderer = renderer_.get();
    ctx_.camera = &camera_;
    ctx_.fridge = &m.cryo().sequencer;
    ctx_.thermalNet = &m.cryo().network;
    ctx_.thermal = &m.state().thermal;
    ctx_.device = m.device();
    ctx_.calibration = m.calibration();
    ctx_.result = m.result();
    ctx_.estimate = ctx_.result != nullptr ? &ctx_.result->estimate : nullptr;
    ctx_.metrics = metrics_.has_value() ? &*metrics_ : nullptr;
    ctx_.diagnostics = &diagnostics_;
    ctx_.traces = &traces_;

    ui::SessionView& v = ctx_.session_view;
    v.status = statusOf(compilePending_, runPending_, m.result() != nullptr,
                        !lastError_.empty() && !runPending_);
    v.shotsDone = m.state().shotsDone;
    v.shotsTotal = m.state().shotsTotal;
    v.progress = v.shotsTotal > 0
                     ? static_cast<double>(v.shotsDone) / static_cast<double>(v.shotsTotal)
                     : -1.0;
    v.lastWallMs = m.state().lastRunMs;
    v.project = projects_->title();
    v.device = m.device() != nullptr ? m.device()->id : std::string{};
    v.calibration = m.calibration() != nullptr ? m.calibration()->timestamp : std::string{};
    v.backend = std::string(runtime::backendChoiceName(m.session().backendChoice()));
    v.backendReason = m.result() != nullptr ? m.result()->backendReason : std::string{};
    v.errors = 0;
    v.warnings = 0;
    for (const lang::Diagnostic& d : diagnostics_) {
        if (d.severity == lang::Severity::Error)
            ++v.errors;
        else if (d.severity == lang::Severity::Warning)
            ++v.warnings;
    }
    v.hasProgram = !programSource().empty();
    v.hasResult = m.result() != nullptr;
}

void Application::renderLab() {
    if (pendingW_ > 0 && pendingH_ > 0) {
        viewportW_ = std::clamp(pendingW_, 64, 8192);
        viewportH_ = std::clamp(pendingH_, 64, 8192);
        pendingW_ = pendingH_ = 0;
    }
    lab::SceneRenderer* scene = model_->sceneRenderer();
    lab::Interaction* ui = model_->interaction();
    if (scene == nullptr || ui == nullptr)
        return;
    camera_.setAspect(static_cast<double>(viewportW_) / std::max(1, viewportH_));
    renderer_->beginFrame(viewportW_, viewportH_, camera_, timeS_);
    lab::LabOverlays* overlays = model_->overlays();
    scene->prepare(camera_, *ui, overlays != nullptr ? &overlays->visuals() : nullptr);
    scene->submit(*renderer_, *ui);
    if (overlays != nullptr)
        scene->submitOverlays(*renderer_, *overlays);
    renderer_->endFrame();
}

void Application::present() {
    gfx::Framebuffer::bindDefault();
    const int w = window_->framebufferWidth(), h = window_->framebufferHeight();
    glViewport(0, 0, w, h);
    const glm::vec4 base = resources_.theme[ui::Token::BgBase];
    glClearColor(base.r, base.g, base.b, 1.0f);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    window_->swapBuffers();
}

void Application::frame(double dtSeconds) {
    ImGui::SetCurrentContext(imgui_);
    window_->input().beginPoll();
    window_->pollEvents();
    timeS_ += dtSeconds;

    // Spec 04 §4: everything the workers posted is delivered here, on the main thread.
    model_->bus().drain();
    pollSession();
    model_->setPhysicalLab(shell_.physicalLab());
    model_->tick(dtSeconds);
    projects_->tick(dtSeconds);
    model_->tickLab(dtSeconds, camera_);

    // Spec 19 §7: a window dragged to another display rebuilds the atlas at the new DPI, and so
    // does the View ▸ Text size menu. Only the second one changes the SIZE of the interface; the
    // first only changes how finely the faces are rasterised.
    const float scale = std::max(1.0f, window_->contentScale());
    const float userScale = shell_.fontScale();
    if (std::abs(scale - dpiScale_) > 1e-3f || std::abs(userScale - fontScale_) > 1e-3f) {
        dpiScale_ = scale;
        fontScale_ = userScale;
        if (auto st = resources_.applyScale(ImGui::GetIO().Fonts, dpiScale_, fontScale_); st)
            ImGui_ImplOpenGL3_DestroyFontsTexture();
        else
            QXL_LOG_WARN(Ui, "could not re-rasterise the fonts at dpi {} scale {}: {}", dpiScale_,
                         fontScale_, st.error().message);
    }

    renderLab();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    refreshContext(dtSeconds);
    shell_.draw(ctx_);
    ImGui::Render();
    present();
    ++frame_;
}

// ---------------------------------------------------------------- captures (spec 23 §8)

Status Application::captureViewport(const std::filesystem::path& png, int width, int height,
                                    bool annotate) {
    const int keepW = viewportW_, keepH = viewportH_;
    // A resize the Viewport panel posted must not override the size asked for here.
    pendingW_ = pendingH_ = 0;
    viewportW_ = width > 0 ? width : viewportW_;
    viewportH_ = height > 0 ? height : viewportH_;
    renderLab();
    viewportW_ = keepW;
    viewportH_ = keepH;
    QXL_TRY(renderer_->screenshot(png));
    if (!annotate)
        return {};
    report::Annotation a;
    // `report::Image::drawText` is a 5×7 ASCII bitmap (spec 23 §8): the strip carries no glyph
    // outside 32…126, so the title is plain ASCII and the unsaved-changes bullet is dropped.
    a.title = std::format("{} / {}", projects_->project().name, model_->layoutId());
    a.device = model_->device() != nullptr ? model_->device()->id : std::string{};
    a.timestamp = core::isoNow();
    for (const std::unique_ptr<viz::IStateView>& v : model_->views()) {
        if (!v)
            continue;
        report::ViewBadge badge;
        badge.view = std::string(v->title());
        badge.cls = v->fidelity(model_->viewInput());
        badge.simulatorOnly = v->observability() == viz::Observability::SimulatorOnly;
        if (a.badges.size() < 4)
            a.badges.push_back(std::move(badge));
    }
    return report::annotatePngFile(png, a);
}

Status Application::captureWindow(const std::filesystem::path& png) {
    const int w = window_->framebufferWidth(), h = window_->framebufferHeight();
    if (w <= 0 || h <= 0)
        return fail(err::NoContext, "the window has no framebuffer");
    report::Image image(w, h);
    gfx::Framebuffer::bindDefault(GL_READ_FRAMEBUFFER);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(w) * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    // glReadPixels gives row 0 at the bottom; `report::Image` has row 0 at the top.
    for (int y = 0; y < h / 2; ++y) {
        std::uint8_t* top = image.pixel(0, y);
        std::uint8_t* bottom = image.pixel(0, h - 1 - y);
        std::copy(top, top + row.size(), row.begin());
        std::copy(bottom, bottom + row.size(), top);
        std::copy(row.begin(), row.end(), bottom);
    }
    return report::writePng(png, image);
}

} // namespace qlab::app
