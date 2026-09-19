#pragma once
// Spec 19 §4 — layout persistence. `imgui.ini` is disabled (`io.IniFilename = nullptr`); the dock
// layout is serialised through `ImGui::SaveIniSettingsToMemory` into this record, one ini string
// per workspace, together with the visibility set and each panel's own `serialize()` slice. The
// record goes into `<project>.qxlab` (spec 23 §2) and into the user config for the default project.
//
// Pure data: no ImGui here, so the round-trip test runs headless.
#include "Core/Error.hpp"
#include "Core/Json.hpp"
#include "UI/Panel.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace qlab::ui {

struct WorkspaceLayout {
    std::string ini;               // ImGui dock/window settings of this workspace
    std::vector<std::string> open; // panel keys the user left visible
    bool customised = false;       // false: the preset's DockBuilder script is re-run
};

struct LayoutState {
    static constexpr int kSchema = 1;

    std::array<WorkspaceLayout, kWorkspaceCount> workspaces;
    std::map<std::string, core::Json, std::less<>> panels; // panel key → its `serialize()`
    Workspace active = Workspace::Lab;
    bool physicalLab = false;     // spec 19 §2 toggle
    std::string palette = "dark"; // theme variant
    float fontScale = 1.0f;       // spec 19 §7 user font scale
    core::Json shortcuts;         // `Shortcuts::toJson()`, empty = defaults

    core::Json toJson() const;
    static Result<LayoutState> fromJson(const core::Json& j);
    // `core::JsonEnvelope` of kind "ui.layout" (spec 23 §2).
    std::string serialize() const;
    static Result<LayoutState> parse(const std::string& text);

    const WorkspaceLayout& of(Workspace w) const { return workspaces[static_cast<std::size_t>(w)]; }
    WorkspaceLayout& of(Workspace w) { return workspaces[static_cast<std::size_t>(w)]; }
};

// Registers the "ui.layout" envelope kind once (idempotent).
void registerLayoutSchema();

} // namespace qlab::ui
