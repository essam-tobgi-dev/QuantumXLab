#pragma once
// Spec 23 §2 — the project lifecycle: new / open / save / save-as of a `.qxlab` file, the 60 s
// autosave, the crash-recovery prompt, the run history in `<stem>.qxlab.d/runs/`, and the opaque
// workspace slice the UI owns (spec 19 §4).
//
// The host holds the document; the model holds the live objects. Loading a project therefore has
// two halves: `apply()` pushes the document into the model (device, backend, compiler settings),
// and `capture()` pulls the model's current settings back into the document before a save.
#include "App/Model.hpp"
#include "Report/Report.hpp"
#include "UI/Layout.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace qlab::app {

class ProjectHost {
  public:
    explicit ProjectHost(LabModel& model);

    // ---- lifecycle
    void newProject();
    // Opens `file`; with `RecoveryChoice::Ask` a newer autosave is reported in `recovery()` rather
    // than loaded, so the caller can put the prompt of spec 23 §2 in front of the user.
    Status open(const std::filesystem::path& file,
                report::RecoveryChoice choice = report::RecoveryChoice::Ask);
    Status save(); // NotFound while the project is untitled
    Status saveAs(const std::filesystem::path& file);
    // Spec 23 §2: autosave every 60 s of wall time and after every successful run. An untitled
    // project journals to `<userData>/recovery/<id>.qxlab` instead.
    void tick(double dtSeconds);
    Status autosaveNow();
    void discardAutosave(); // clean exit

    // ---- state
    report::Project& project() { return project_; }
    const report::Project& project() const { return project_; }
    const std::filesystem::path& path() const { return path_; }
    bool untitled() const { return path_.empty(); }
    bool dirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    std::string title() const;
    // Set when `open` found an autosave newer than the file and did not use it.
    const std::optional<report::OpenedProject>& recovery() const { return recovery_; }
    Status acceptRecovery();
    void declineRecovery();

    // ---- contents
    void setMainProgram(std::string source, std::filesystem::path origin = {});
    std::string mainSource() const;
    // Appends the run summary and, for a titled project, writes the `run_result` document beside
    // it.
    Status recordRun(const runtime::RunResult& run, std::string_view source);

    // ---- workspace slice (spec 19 §4)
    void storeLayout(const ui::LayoutState& layout);
    std::optional<ui::LayoutState> layout() const;
    void storeCamera(const core::Json& camera);
    core::Json camera() const { return project_.workspace.camera; }

    // Pushes the document's device / backend / compiler settings into the model, and pulls them
    // back. `apply` is called after `open`, `capture` before every save.
    Status apply();
    void capture();

  private:
    LabModel* model_;
    report::Project project_;
    std::filesystem::path path_;
    std::string recoveryId_; // journal id of an untitled project
    std::optional<report::OpenedProject> recovery_;
    double sinceAutosave_ = 0.0;
    bool dirty_ = false;
};

} // namespace qlab::app
