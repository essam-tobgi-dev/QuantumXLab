// Spec 19 §3 / 02 §5 — command routing: what the panels post, and what the event bus reports back.
// Every command in `ui::Commands` ends here; the App is the only place that knows which subsystem
// owns which verb.
#include "App/Application.hpp"
#include "App/RunProgram.hpp"
#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include "Report/Report.hpp"
#include <format>
#include <imgui.h>

namespace qlab::app {
namespace {

// The Run Controls panel owns shots / seed / noise / cadence as its state slice (spec 19 §3); the
// App reads them back through `Panel::serialize()` when the Run button is pressed.
struct RunPanelState {
    std::uint32_t shots = 1024;
    std::optional<std::uint64_t> seed;
    runtime::NoiseSource noise = runtime::NoiseSource::Calibrated;
    runtime::SnapshotCadence cadence = runtime::SnapshotCadence::Layer;
    bool pulseLevel = false;
    std::optional<runtime::BackendChoice> backend;
};

RunPanelState runPanelState(const ui::Shell& shell) {
    RunPanelState s;
    const ui::Panel* panel = shell.panel(ui::PanelId::RunControls);
    if (panel == nullptr)
        return s;
    const core::Json j = panel->serialize();
    if (!j.is_object())
        return s;
    if (const auto it = j.find("shots"); it != j.end() && it->is_number())
        s.shots = static_cast<std::uint32_t>(std::max<std::int64_t>(1, it->get<std::int64_t>()));
    if (j.value("use_seed", false))
        s.seed = static_cast<std::uint64_t>(j.value("seed", 0));
    const int noise = j.value("noise", 1);
    s.noise = noise == 0
                  ? runtime::NoiseSource::Ideal
                  : (noise == 2 ? runtime::NoiseSource::Custom : runtime::NoiseSource::Calibrated);
    s.cadence = static_cast<runtime::SnapshotCadence>(std::clamp(j.value("cadence", 2), 0, 4));
    s.pulseLevel = j.value("pulse_level", false);
    const int backend = j.value("backend", 0);
    if (backend > 0)
        s.backend = static_cast<runtime::BackendChoice>(backend);
    return s;
}

} // namespace

// ---------------------------------------------------------------- the program buffer

std::string Application::programSource() const {
    const ui::Panel* editor = shell_.panel(ui::PanelId::CodeEditor);
    if (editor == nullptr)
        return {};
    const core::Json j = editor->serialize();
    return j.is_object() ? j.value("text", std::string{}) : std::string{};
}

void Application::setProgramSource(std::string source) {
    if (ui::Panel* editor = shell_.panel(ui::PanelId::CodeEditor); editor != nullptr) {
        core::Json j = core::Json::object();
        j["text"] = source;
        editor->deserialize(j);
    }
    projects_->setMainProgram(source);
    model_->incremental().submit(std::move(source), "editor.qasm");
}

Status Application::openProgramFile(const std::filesystem::path& file) {
    QXL_TRY_ASSIGN(report::ImportedProgram imported, report::importProgram(file));
    for (const lang::Diagnostic& d : imported.diagnostics)
        diagnostics_.push_back(d);
    setProgramSource(imported.source);
    projects_->setMainProgram(imported.source, file);
    return {};
}

// ---------------------------------------------------------------- compile and run

void Application::requestCompile() {
    const std::string source = programSource();
    if (source.empty())
        return;
    auto id = model_->session().loadProgram(source, "editor.qasm");
    if (!id) {
        lastError_ = id.error().message;
        return;
    }
    auto handle = model_->session().compile(*id, compileOptionsFor(options_));
    if (!handle) {
        lastError_ = handle.error().message;
        return;
    }
    compile_ = *handle;
    compilePending_ = true;
    lastError_.clear();
}

void Application::requestRun() {
    if (runPending_)
        return;
    runAfterCompile_ = true;
    requestCompile();
}

void Application::requestStop() {
    if (runPending_)
        model_->session().cancel(runHandle_);
    if (compilePending_)
        model_->session().cancelCompile(compile_);
    runAfterCompile_ = false;
}

void Application::stepGate(int delta) {
    const runtime::RunResult* r = model_->result();
    if (r == nullptr || r->snapshots.empty())
        return;
    // Spec 15 §3.6: the playhead moves between the snapshots the run published.
    std::size_t at = 0;
    for (std::size_t k = 0; k < r->snapshots.size(); ++k)
        if (r->snapshots[k].gateIndex <= model_->playhead())
            at = k;
    const std::size_t next =
        delta >= 0 ? std::min(at + 1, r->snapshots.size() - 1) : (at == 0 ? 0 : at - 1);
    model_->setPlayhead(r->snapshots[next].gateIndex);
}

void Application::stepShot(int delta) {
    const runtime::RunResult* r = model_->result();
    if (r == nullptr || r->memory.empty())
        return;
    LiveState& s = model_->state();
    const std::uint64_t last = r->memory.size() - 1;
    s.shotsDone = delta >= 0 ? std::min<std::uint64_t>(s.shotsDone + 1, last)
                             : (s.shotsDone == 0 ? 0 : s.shotsDone - 1);
    s.shotsTotal = r->memory.size();
}

// ---------------------------------------------------------------- project and exports

Status Application::saveProject() {
    projects_->setMainProgram(programSource());
    projects_->storeLayout(shell_.saveLayout());
    if (lab::Interaction* ui = model_->interaction(); ui != nullptr)
        projects_->storeCamera(ui->saveViewState());
    if (projects_->untitled()) {
        // Spec 23 §2: an untitled project is written beside the user data, not silently nowhere.
        const std::filesystem::path file = core::userDataDir() / "Untitled.qxlab";
        return projects_->saveAs(file);
    }
    return projects_->save();
}

Status Application::exportResults(const std::filesystem::path& directory) {
    const runtime::RunResult* r = model_->result();
    if (r == nullptr)
        return fail(ErrorCode::NotFound, "there is no result to export");
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    report::ResultExportOptions options;
    options.source = programSource();
    QXL_TRY(report::writeRunResult(directory / "run_result.json", *r, options));
    report::RunReportInput input;
    input.project = projects_->title();
    input.run = r;
    input.source = options.source;
    QXL_TRY(report::writeRunReport(directory / "run_report.html", input));
    return {};
}

// ---------------------------------------------------------------- wiring

void Application::wireCommands() {
    ui::Commands& c = ctx_.cmd;
    c.run = [this] { requestRun(); };
    c.stop = [this] { requestStop(); };
    c.compile = [this] { requestCompile(); };
    c.stepGate = [this] { stepGate(+1); };
    c.stepShot = [this] { stepShot(+1); };
    c.saveProject = [this] {
        if (auto st = saveProject(); !st)
            lastError_ = st.error().message;
    };
    c.exportResults = [this] {
        const std::filesystem::path dir = core::userDataDir() / "exports";
        if (auto st = exportResults(dir); !st)
            lastError_ = st.error().message;
        else
            QXL_LOG_INFO(App, "results exported to {}", dir.string());
    };
    c.selectDevice = [this](std::string_view id) {
        if (auto st = model_->selectDevice(id); !st)
            lastError_ = st.error().message;
        else if (lab::Interaction* ui = model_->interaction(); ui != nullptr)
            (void)ui->applyBookmark("Overview", camera_, 0.0);
    };
    c.selectBackend = [this](runtime::BackendChoice choice) {
        model_->session().selectBackend(choice);
    };
    c.selectComponent = [this](ComponentId id) {
        model_->selection().selectComponent(id);
        if (lab::Interaction* ui = model_->interaction(); ui != nullptr)
            ui->select(id);
    };
    c.requestReductions = [this](const viz::ReductionRequest& request) {
        model_->requestReductions(request);
    };
    c.resizeViewport = [this](int w, int h) {
        pendingW_ = w;
        pendingH_ = h;
    };
    c.requestExport = [this](const viz::ExportRequest& request) {
        const std::filesystem::path dir = core::userDataDir() / "exports";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (request.format == viz::ExportRequest::Format::Csv) {
            const std::filesystem::path file = dir / (request.viewId + ".csv");
            if (auto st = core::writeTextFileAtomic(file, request.csv); !st)
                lastError_ = st.error().message;
            return;
        }
        if (auto st = captureViewport(dir / (request.viewId + ".png")); !st)
            lastError_ = st.error().message;
    };
    // `gotoSource`, `openProgram`, `openTheory` and `setBusy` belong to the panels that own the
    // buffer they act on; they claim them on their first draw (spec 19 §3).
}

void Application::subscribeEvents() {
    // `LabModel` already subscribes to the session's run events and to the pulse-level line
    // powers; what is left here is what only the interface needs.
    core::EventBus& bus = model_->bus();
    subs_.push_back(bus.subscribe<runtime::RunFinished>([this](const runtime::RunFinished& e) {
        if (!e.ok)
            lastError_ = "the run failed";
    }));
    subs_.push_back(bus.subscribe<instr::TraceReady>([this](const instr::TraceReady& e) {
        if (!e.trace)
            return;
        traces_.push_back(*e.trace);
        if (traces_.size() > kMaxTraces)
            traces_.erase(traces_.begin(), traces_.begin() + 1);
    }));
    subs_.push_back(bus.subscribe<instr::InstrumentFault>([](const instr::InstrumentFault& e) {
        QXL_LOG_WARN(Instr, "{}: {}", e.instrument.toString(), e.message);
    }));
    subs_.push_back(bus.subscribe<instr::SettingClamped>([](const instr::SettingClamped& e) {
        QXL_LOG_INFO(Instr, "{}: {}", e.report.instrument.toString(), e.report.message);
    }));
}

void Application::pollSession() {
    runtime::Session& session = model_->session();
    // The incremental compiler publishes on a worker; the newest update is taken here.
    if (std::shared_ptr<const compiler::IncrementalCompiler::Update> u =
            model_->incremental().latest();
        u && u->generation != editorGeneration_) {
        editorGeneration_ = u->generation;
        diagnostics_ = u->diagnostics;
        if (u->program) {
            metrics_ = u->program->metrics;
            model_->setCompiled(std::shared_ptr<const compiler::CompiledProgram>(u, &*u->program));
        }
    }
    if (compilePending_ && session.compileDone(compile_)) {
        compilePending_ = false;
        diagnostics_ = session.compileDiagnostics(compile_);
        const compiler::CompiledProgram* compiled = session.compiled(compile_);
        if (compiled == nullptr) {
            runAfterCompile_ = false;
            lastError_ = "the program did not compile";
        } else {
            metrics_ = compiled->metrics;
            model_->setCompiled(viz::borrow(*compiled));
            if (runAfterCompile_) {
                runAfterCompile_ = false;
                runtime::RunOptions options;
                const RunPanelState panel = runPanelState(shell_);
                options.shots = panel.shots;
                options.seed = panel.seed;
                options.noise = panel.noise;
                options.cadence = panel.cadence;
                options.backend = panel.backend;
                if (auto handle = session.run(compile_, options)) {
                    runHandle_ = *handle;
                    runPending_ = true;
                    lastError_.clear();
                } else {
                    lastError_ = handle.error().message;
                }
            }
        }
    }
    if (runPending_ && session.done(runHandle_)) {
        runPending_ = false;
        const runtime::RunResult* result = session.result(runHandle_);
        if (result == nullptr) {
            lastError_ = "the run produced no result";
            return;
        }
        model_->setResult(viz::borrow(*result));
        metrics_ = result->metrics;
        for (const lang::Diagnostic& d : result->diagnostics)
            diagnostics_.push_back(d);
        if (auto st = projects_->recordRun(*result, programSource()); !st)
            QXL_LOG_WARN(App, "run history: {}", st.error().message);
    }
}

} // namespace qlab::app
