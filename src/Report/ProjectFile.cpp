// Spec 23 §2 — reading and writing `.qxlab` files: atomic saves, the autosave copy and the
// crash-recovery journal of an untitled project.
#include "Core/Paths.hpp"
#include "Report/Project.hpp"

#include <chrono>
#include <format>

namespace qlab::report {
namespace {

std::filesystem::path withSuffix(const std::filesystem::path& file, std::string_view suffix) {
    std::filesystem::path p = file;
    p += std::string(suffix);
    return p;
}

std::string isoFileTime(std::filesystem::file_time_type t) {
    if constexpr (requires { std::chrono::file_clock::to_sys(t); }) {
        return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(
                                            std::chrono::file_clock::to_sys(t)));
    } else {
        using Sys = std::chrono::system_clock;
        const Sys::time_point sys(std::chrono::duration_cast<Sys::duration>(t.time_since_epoch()));
        return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(sys));
    }
}

} // namespace

std::filesystem::path projectDataDir(const std::filesystem::path& file) {
    return withSuffix(file, ".d");
}
std::filesystem::path projectRunsDir(const std::filesystem::path& file) {
    return projectDataDir(file) / "runs";
}
std::filesystem::path autosavePath(const std::filesystem::path& file) {
    return withSuffix(file, ".autosave");
}
std::filesystem::path recoveryDir() {
    return core::userDataDir() / "recovery";
}
std::filesystem::path recoveryPath(std::string_view id) {
    return recoveryDir() / (std::string(id) + ".qxlab");
}

std::string serializeProject(const Project& p) {
    ensureKinds();
    return core::JsonEnvelope::serialize(kProjectKind, p.toJson(), kProjectSchema);
}

Result<Project> parseProject(const std::string& text) {
    ensureKinds();
    QXL_TRY_ASSIGN(core::Envelope env, core::JsonEnvelope::parse(text, kProjectKind));
    return Project::fromJson(env.data);
}

static Status writeProjectTo(const std::filesystem::path& path, const Project& p) {
    ensureKinds();
    const core::Json data = p.toJson();
    // Spec 23 §1: NaN/Inf are not permitted in a document; nlohmann would write them as `null`.
    if (!allFinite(data))
        return fail(ErrorCode::InvalidArgument, "project holds a non-finite number");
    return core::writeTextFileAtomic(
        path, core::JsonEnvelope::serialize(kProjectKind, data, kProjectSchema));
}

Status saveProject(const std::filesystem::path& file, const Project& p) {
    QXL_TRY(writeProjectTo(file, p));
    // The saved file now holds what the autosave held; keeping it would offer a stale recovery.
    return clearAutosave(file);
}

Result<Project> loadProject(const std::filesystem::path& file) {
    ensureKinds();
    auto env = core::JsonEnvelope::load(file, kProjectKind);
    if (!env)
        return std::unexpected(env.error());
    auto p = Project::fromJson(env->data);
    if (!p)
        p.error().notes.push_back("file: " + file.string());
    return p;
}

Status writeAutosave(const std::filesystem::path& file, const Project& p) {
    return writeProjectTo(autosavePath(file), p);
}

Status clearAutosave(const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::remove(autosavePath(file), ec);
    if (ec)
        return fail(ErrorCode::Io, "cannot remove autosave: " + ec.message());
    return {};
}

Status writeRecoveryJournal(std::string_view id, const Project& p) {
    return writeProjectTo(recoveryPath(id), p);
}

Result<OpenedProject> openProject(const std::filesystem::path& file, RecoveryChoice choice) {
    OpenedProject out;
    out.autosave = autosavePath(file);
    std::error_code ec;
    const bool hasAutosave = std::filesystem::exists(out.autosave, ec) && !ec;
    const bool hasFile = std::filesystem::exists(file, ec) && !ec;
    bool autosaveNewer = hasAutosave;
    if (hasAutosave) {
        const auto at = std::filesystem::last_write_time(out.autosave, ec);
        if (!ec)
            out.autosaveTime = isoFileTime(at);
        if (hasFile) {
            const auto ft = std::filesystem::last_write_time(file, ec);
            if (!ec) {
                out.fileTime = isoFileTime(ft);
                autosaveNewer = at > ft;
            }
        }
    }
    const bool useAutosave = hasAutosave && (choice == RecoveryChoice::UseAutosave ||
                                             (choice == RecoveryChoice::Ask && !hasFile));
    if (useAutosave) {
        QXL_TRY_ASSIGN(out.project, loadProject(out.autosave));
        out.recovered = true;
        return out;
    }
    if (!hasFile)
        return fail(ErrorCode::NotFound, "no project at " + file.string());
    QXL_TRY_ASSIGN(out.project, loadProject(file));
    if (choice == RecoveryChoice::DiscardAutosave) {
        QXL_TRY(clearAutosave(file));
        return out;
    }
    // Spec 23 §2: an autosave newer than the project prompts for recovery; the caller asks the user
    // and reopens with `UseAutosave` or `DiscardAutosave`.
    out.autosavePending = autosaveNewer;
    return out;
}

} // namespace qlab::report
