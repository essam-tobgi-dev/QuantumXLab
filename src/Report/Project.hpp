#pragma once
// Spec 23 §2 — the `.qxlab` project file: programs, device and its overrides, backend and compiler
// settings, the opaque UI workspace state, run-history summaries and analysis recipes.
//
// Every record keeps the unknown fields it was loaded with (`extra`) and writes them back, so a
// project written by a newer application survives a round trip through this one (§1, §12).
#include "Core/Json.hpp"
#include "Report/Types.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace qlab::report {

// Spec 23 §2: programs are stored inline by default; `path` is set when the user linked an
// external file, which is then watched and reloaded on change.
struct ProgramRef {
    std::string path;      // relative to the project directory, forward slashes; empty when inline
    std::string inlineSource;
    bool isMain = false;
    core::Json extra = core::Json::object();
};

struct DeviceRef {
    std::string id;
    core::Json calibrationOverrides = core::Json::object();  // {"qubit[3].T1_us": 42.0}
    core::Json wiringOverrides = core::Json::object();       // overrides on the base lab layout (§4)
    core::Json extra = core::Json::object();
};

struct BackendSettings {
    std::string kind = "auto";      // auto | state_vector | density_matrix | stabilizer | lindblad
    std::uint32_t shots = 1024;
    std::uint64_t seed = 0;         // stored so a reopened project reproduces every Statistical result
    bool noise = true;
    bool snapshotEveryGate = false;
    core::Json lindblad = core::Json::object();   // {"levels": 3, "dt_ps": 50}
    core::Json extra = core::Json::object();
};

struct CompilerSettings {
    int optimizationLevel = 1;
    std::string layout = "noise_aware";
    std::string routing = "sabre";
    std::string scheduling = "asap";
    core::Json passOverrides = core::Json::object();
    core::Json extra = core::Json::object();
};

// Opaque to this module: the UI owns the shape of these fields (spec 23 §2, 19 §9).
struct WorkspaceState {
    std::string preset;
    std::string imguiLayout;
    core::Json camera = core::Json::object();
    core::Json openPanels = core::Json::array();
    core::Json extra = core::Json::object();
};

// Spec 23 §2: results live in `<name>.qxlab.d/runs/`, never inline; the project keeps summaries.
struct RunSummary {
    std::string id;                 // "run-000017"
    std::string time;               // ISO 8601 UTC
    std::string backend;
    std::string device;
    std::string programHash;
    std::uint32_t shots = 0;
    std::uint64_t seed = 0;
    std::string resultPath;         // "runs/run-000017.json", relative to the project directory
    core::Json estimate = core::Json::object();
    core::Json extra = core::Json::object();
};

struct RecipeRef {
    std::string id;
    std::optional<std::uint32_t> qubit;
    std::string lastResultPath;
    core::Json extra = core::Json::object();
};

struct Project {
    std::string name;
    std::vector<ProgramRef> programs;
    DeviceRef device;
    BackendSettings backend;
    CompilerSettings compiler;
    WorkspaceState workspace;
    std::vector<RunSummary> runs;
    std::vector<RecipeRef> recipes;
    core::Json extra = core::Json::object();   // unknown top-level fields of `data`

    const ProgramRef* mainProgram() const;
    const RunSummary* run(std::string_view id) const;
    // Appends a summary and returns its generated id ("run-000017", next free number).
    std::string addRun(RunSummary summary);

    core::Json toJson() const;
    static Result<Project> fromJson(const core::Json& data);
};

// ---------------------------------------------------------------- files (spec 23 §2)
// The satellite paths of a project file: `<stem>.qxlab.d/` for run results and `<stem>.qxlab.autosave`
// for the crash-recovery copy.
std::filesystem::path projectDataDir(const std::filesystem::path& file);
std::filesystem::path projectRunsDir(const std::filesystem::path& file);
std::filesystem::path autosavePath(const std::filesystem::path& file);
// Crash-recovery journal of an untitled project (§2): `<userData>/recovery/<id>.qxlab`.
std::filesystem::path recoveryDir();
std::filesystem::path recoveryPath(std::string_view id);

std::string serializeProject(const Project& p);                       // envelope kind "project"
Result<Project> parseProject(const std::string& text);                // refuses schema > current
Status saveProject(const std::filesystem::path& file, const Project& p);  // atomic; drops the autosave
Result<Project> loadProject(const std::filesystem::path& file);

// Autosave (§2): every 60 s and after every successful run; deleted on clean exit.
inline constexpr std::chrono::seconds kAutosaveInterval{60};
Status writeAutosave(const std::filesystem::path& file, const Project& p);
Status clearAutosave(const std::filesystem::path& file);
// Untitled projects journal to the user data directory instead.
Status writeRecoveryJournal(std::string_view id, const Project& p);

enum class RecoveryChoice : std::uint8_t {
    Ask,              // load the project file; report a newer autosave for the caller to prompt about
    UseAutosave,      // load the autosave (the user accepted recovery)
    DiscardAutosave,  // load the project file and delete the autosave
};

struct OpenedProject {
    Project project;
    bool recovered = false;          // the autosave was the source
    bool autosavePending = false;    // an autosave newer than the file exists and was not used
    std::filesystem::path autosave;
    std::string autosaveTime, fileTime;   // ISO 8601 UTC, for the recovery prompt
};

// Spec 23 §2: on open, an autosave newer than the project prompts for recovery.
Result<OpenedProject> openProject(const std::filesystem::path& file, RecoveryChoice choice = RecoveryChoice::Ask);

} // namespace qlab::report
