// Spec 23 §2 — the `.qxlab` project lifecycle (see ProjectHost.hpp).
#include "App/ProjectHost.hpp"
#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include <format>
#include <random>

namespace qlab::app {
namespace {

// The UI's full `LayoutState` is richer than the three documented `workspace` fields, so it rides
// in `workspace.extra["ui_layout"]` (spec 23 §1/§12: unknown fields survive a round trip) while
// `preset`, `imguiLayout` and `openPanels` keep describing the active workspace as the spec says.
constexpr std::string_view kLayoutKey = "ui_layout";

std::string journalId() {
    // Deterministic per process, unique per run: the recovery journal only has to not collide.
    static const std::string id =
        std::format("untitled-{:016x}", core::JsonEnvelope::currentSchema("project") +
                                            std::hash<std::string>{}(core::isoNow()));
    return id;
}

} // namespace

ProjectHost::ProjectHost(LabModel& model) : model_(&model) {
    newProject();
}

void ProjectHost::newProject() {
    project_ = report::Project{};
    project_.name = "Untitled project";
    path_.clear();
    recovery_.reset();
    recoveryId_ = journalId();
    sinceAutosave_ = 0.0;
    dirty_ = false;
    capture();
}

std::string ProjectHost::title() const {
    const std::string name = path_.empty() ? project_.name : path_.stem().string();
    return dirty_ ? name + " •" : name;
}

Status ProjectHost::open(const std::filesystem::path& file, report::RecoveryChoice choice) {
    QXL_TRY_ASSIGN(report::OpenedProject opened, report::openProject(file, choice));
    project_ = opened.project;
    path_ = file;
    dirty_ = false;
    sinceAutosave_ = 0.0;
    recovery_ = opened.autosavePending ? std::optional(opened) : std::nullopt;
    return apply();
}

Status ProjectHost::acceptRecovery() {
    if (!recovery_)
        return {};
    const std::filesystem::path file = path_;
    recovery_.reset();
    QXL_TRY_ASSIGN(report::OpenedProject opened,
                   report::openProject(file, report::RecoveryChoice::UseAutosave));
    project_ = opened.project;
    dirty_ = true; // the recovered state has never been saved to the project file
    return apply();
}

void ProjectHost::declineRecovery() {
    if (!recovery_)
        return;
    recovery_.reset();
    (void)report::clearAutosave(path_);
}

Status ProjectHost::save() {
    if (path_.empty())
        return fail(ErrorCode::NotFound, "the project has no file yet: use Save As");
    capture();
    QXL_TRY(report::saveProject(path_, project_));
    dirty_ = false;
    sinceAutosave_ = 0.0;
    return {};
}

Status ProjectHost::saveAs(const std::filesystem::path& file) {
    path_ = file;
    if (project_.name == "Untitled project")
        project_.name = file.stem().string();
    return save();
}

Status ProjectHost::autosaveNow() {
    capture();
    if (path_.empty()) {
        QXL_TRY(report::writeRecoveryJournal(recoveryId_, project_));
        return {};
    }
    return report::writeAutosave(path_, project_);
}

void ProjectHost::tick(double dtSeconds) {
    sinceAutosave_ += dtSeconds;
    if (sinceAutosave_ < static_cast<double>(report::kAutosaveInterval.count()))
        return;
    sinceAutosave_ = 0.0;
    if (!dirty_)
        return;
    if (auto st = autosaveNow(); !st)
        QXL_LOG_WARN(App, "autosave failed: {}", st.error().message);
}

void ProjectHost::discardAutosave() {
    if (path_.empty()) {
        std::error_code ec;
        std::filesystem::remove(report::recoveryPath(recoveryId_), ec);
        return;
    }
    (void)report::clearAutosave(path_);
}

void ProjectHost::setMainProgram(std::string source, std::filesystem::path origin) {
    report::ProgramRef* main = nullptr;
    for (report::ProgramRef& p : project_.programs)
        if (p.isMain)
            main = &p;
    if (main == nullptr) {
        project_.programs.push_back(report::ProgramRef{});
        main = &project_.programs.back();
        main->isMain = true;
    }
    main->inlineSource = std::move(source);
    main->path = origin.empty() || path_.empty()
                     ? std::string{}
                     : report::relativePathString(origin, path_.parent_path());
    dirty_ = true;
}

std::string ProjectHost::mainSource() const {
    const report::ProgramRef* main = project_.mainProgram();
    return main != nullptr ? main->inlineSource : std::string{};
}

Status ProjectHost::recordRun(const runtime::RunResult& run, std::string_view source) {
    report::RunSummary summary;
    summary.time = core::isoNow();
    summary.backend = std::string(report::backendName(run.backend));
    summary.device = run.device;
    summary.programHash = report::hashHex(run.programHash);
    summary.shots = run.options.shots;
    summary.seed = run.seed;
    summary.estimate = run.estimate.toJson();
    if (!path_.empty()) {
        const std::string id = std::format("run-{:06}", project_.runs.size() + 1);
        summary.resultPath = std::format("{}.d/runs/{}.json", path_.filename().string(), id);
        const std::filesystem::path file = report::projectRunsDir(path_) / (id + ".json");
        report::ResultExportOptions options;
        options.source = std::string(source);
        options.writeMemory = run.options.keepMemory && !run.memory.empty();
        QXL_TRY(report::writeRunResult(file, run, options));
    }
    project_.addRun(std::move(summary));
    dirty_ = true;
    // Spec 23 §2: an autosave follows every successful run.
    if (auto st = autosaveNow(); !st)
        QXL_LOG_WARN(App, "autosave after run failed: {}", st.error().message);
    return {};
}

void ProjectHost::storeLayout(const ui::LayoutState& layout) {
    project_.workspace.preset = std::string(ui::workspaceName(layout.active));
    project_.workspace.imguiLayout = layout.of(layout.active).ini;
    core::Json open = core::Json::array();
    for (const std::string& key : layout.of(layout.active).open)
        open.push_back(key);
    project_.workspace.openPanels = std::move(open);
    project_.workspace.extra[std::string(kLayoutKey)] = layout.toJson();
    dirty_ = true;
}

std::optional<ui::LayoutState> ProjectHost::layout() const {
    const auto it = project_.workspace.extra.find(kLayoutKey);
    if (it == project_.workspace.extra.end())
        return std::nullopt;
    auto parsed = ui::LayoutState::fromJson(*it);
    if (!parsed)
        return std::nullopt;
    return std::move(*parsed);
}

void ProjectHost::storeCamera(const core::Json& camera) {
    project_.workspace.camera = camera;
    dirty_ = true;
}

Status ProjectHost::apply() {
    if (!project_.device.id.empty() && project_.device.id != model_->session().device()->id)
        QXL_TRY(model_->selectDevice(project_.device.id));
    model_->session().selectBackend(report::backendChoiceFrom(project_.backend.kind));
    return {};
}

void ProjectHost::capture() {
    if (const hw::Device* d = model_->session().device(); d != nullptr)
        project_.device.id = d->id;
    project_.backend.kind = std::string(report::backendName(model_->session().backendChoice()));
    if (const runtime::RunResult* r = model_->result(); r != nullptr) {
        project_.backend.shots = r->options.shots;
        project_.backend.seed = r->seed;
        project_.backend.noise = r->options.noise != runtime::NoiseSource::Ideal;
    }
}

} // namespace qlab::app
