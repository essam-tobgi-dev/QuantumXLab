// Spec 19 §2/§4 — shell state, docking scripts and layout persistence (see Shell.hpp).
#include "UI/Shell.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h> // DockBuilder (spec 19 §4); the docking branch keeps it internal

namespace qlab::ui {
namespace {

std::size_t index(PanelId id) {
    return static_cast<std::size_t>(id);
}
std::size_t index(Workspace w) {
    return static_cast<std::size_t>(w);
}

} // namespace

Shell::Shell() : Shell(makeAllPanels()) {}

Shell::Shell(std::vector<PanelPtr> panels) : panels_(std::move(panels)) {
    for (std::size_t w = 0; w < kWorkspaceCount; ++w) {
        open_[w].assign(kPanelCount, false);
        const WorkspacePreset& p = preset(static_cast<Workspace>(w));
        for (PanelId id : p.panels)
            open_[w][index(id)] = true;
    }
    registerLayoutSchema();
}

Shell::~Shell() = default;

Panel* Shell::panel(PanelId id) const {
    for (const PanelPtr& p : panels_)
        if (p->id() == id)
            return p.get();
    return nullptr;
}

Panel* Shell::panel(std::string_view key) const {
    for (const PanelPtr& p : panels_)
        if (p->key() == key)
            return p.get();
    return nullptr;
}

void Shell::setWorkspace(Workspace w) {
    if (w == workspace_ || w >= Workspace::Count)
        return;
    captureIni();
    workspace_ = w;
    // Spec 19 §4: a workspace the user has arranged is restored from its ini; otherwise the
    // preset's DockBuilder script runs again.
    if (saved_[index(w)].customised && !saved_[index(w)].ini.empty())
        applyIni_ = true;
    else
        rebuildDock_ = true;
}

void Shell::setPhysicalLab(bool on) {
    physicalLab_ = on;
}

bool Shell::visible(const Panel& p) const {
    if (hiddenByPhysicalLab(p))
        return false;
    return open_[index(workspace_)][index(p.id())];
}

void Shell::setOpen(PanelId id, bool on) {
    open_[index(workspace_)][index(id)] = on;
}
bool Shell::isOpen(PanelId id) const {
    return open_[index(workspace_)][index(id)];
}

void Shell::setBusy(std::string_view message, double progress) {
    busyMessage_ = std::string(message);
    busyProgress_ = progress;
}

void Shell::configureImGui() {
    if (ImGui::GetCurrentContext() == nullptr)
        return;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // spec 19 §4
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // spec 19 §8
    io.IniFilename = nullptr;                             // spec 19 §4: imgui.ini is not used
    io.ConfigWindowsMoveFromTitleBarOnly = true;
}

// ---------------------------------------------------------------- layout

void Shell::captureIni() {
    if (ImGui::GetCurrentContext() == nullptr)
        return;
    std::size_t size = 0;
    const char* data = ImGui::SaveIniSettingsToMemory(&size);
    if (data == nullptr)
        return;
    WorkspaceLayout& into = saved_[index(workspace_)];
    into.ini.assign(data, size);
    into.customised = true;
    into.open.clear();
    for (const PanelPtr& p : panels_)
        if (open_[index(workspace_)][index(p->id())])
            into.open.emplace_back(p->key());
}

void Shell::applyPending() {
    if (!applyIni_)
        return;
    applyIni_ = false;
    const WorkspaceLayout& from = saved_[index(workspace_)];
    if (from.ini.empty()) {
        rebuildDock_ = true;
        return;
    }
    // Called at the top of the frame, before any window of this frame has been submitted.
    ImGui::LoadIniSettingsFromMemory(from.ini.data(), from.ini.size());
}

void Shell::resetLayout(Workspace w) {
    saved_[index(w)].customised = false;
    saved_[index(w)].ini.clear();
    const WorkspacePreset& p = preset(w);
    open_[index(w)].assign(kPanelCount, false);
    for (PanelId id : p.panels)
        open_[index(w)][index(id)] = true;
    if (w == workspace_)
        rebuildDock_ = true;
}

void Shell::setFontScale(float s) {
    fontScale_ = std::clamp(s, 0.8f, 1.6f);
} // spec 19 §7

LayoutState Shell::saveLayout() {
    captureIni();
    LayoutState s;
    s.workspaces = saved_;
    s.active = workspace_;
    s.physicalLab = physicalLab_;
    s.palette = palette_;
    s.fontScale = fontScale_;
    s.shortcuts = keys_.toJson();
    for (std::size_t w = 0; w < kWorkspaceCount; ++w) {
        WorkspaceLayout& into = s.workspaces[w];
        into.open.clear();
        for (const PanelPtr& p : panels_)
            if (open_[w][index(p->id())])
                into.open.emplace_back(p->key());
    }
    for (const PanelPtr& p : panels_) {
        core::Json j = p->serialize();
        if (!j.is_null() && !j.empty())
            s.panels[std::string(p->key())] = std::move(j);
    }
    return s;
}

Status Shell::loadLayout(LayoutState state) {
    saved_ = state.workspaces;
    workspace_ = state.active;
    physicalLab_ = state.physicalLab;
    palette_ = state.palette;
    setFontScale(state.fontScale);
    if (!state.shortcuts.is_null() && !state.shortcuts.empty()) {
        QXL_TRY_ASSIGN(Shortcuts k, Shortcuts::fromJson(state.shortcuts));
        keys_ = std::move(k);
    }
    for (std::size_t w = 0; w < kWorkspaceCount; ++w) {
        const WorkspaceLayout& from = saved_[w];
        if (from.open.empty() && !from.customised)
            continue; // keep the preset's set
        open_[w].assign(kPanelCount, false);
        for (const std::string& key : from.open) {
            const auto id = panelFromKey(key);
            if (!id)
                return fail(err::UnknownPanel, "layout: unknown panel '" + key + "'");
            open_[w][index(*id)] = true;
        }
    }
    for (const PanelPtr& p : panels_) {
        const auto it = state.panels.find(p->key());
        if (it != state.panels.end())
            p->deserialize(it->second);
    }
    applyIni_ = saved_[index(workspace_)].customised && !saved_[index(workspace_)].ini.empty();
    rebuildDock_ = !applyIni_;
    return {};
}

// ---------------------------------------------------------------- docking (spec 19 §2)

void Shell::buildDock(unsigned dockspaceId) {
    const auto root = static_cast<ImGuiID>(dockspaceId);
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->WorkSize);

    const auto dock = [&](PanelId id, ImGuiID node) {
        if (const Panel* p = panel(id); p != nullptr)
            ImGui::DockBuilderDockWindow(p->windowTitle().c_str(), node);
    };
    ImGuiID centre = root, left = 0, right = 0, bottom = 0, rightBottom = 0, centreBottom = 0;

    switch (workspace_) {
    case Workspace::Lab:
        // Viewport 70 % centre; Inspector right 22 %; Component Tree left 14 %; Instruments strip.
        left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.14f, nullptr, &centre);
        right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.26f, nullptr, &centre);
        bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.28f, nullptr, &centre);
        dock(PanelId::ComponentTree, left);
        dock(PanelId::Inspector, right);
        dock(PanelId::Fridge, right); // tab beside the Inspector (spec 19 §2)
        dock(PanelId::Project, right);
        dock(PanelId::Instruments, bottom);
        dock(PanelId::Tour, bottom); // under the viewport, beside Instruments (spec 17 §7.10)
        dock(PanelId::Log, bottom);
        dock(PanelId::Viewport, centre);
        break;
    case Workspace::Program:
        // Code Editor centre 55 %; Diagnostics below it; Circuit right 30 % with the Pulse tab;
        // Run Controls bottom-right; Examples left drawer.
        left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.16f, nullptr, &centre);
        right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.36f, nullptr, &centre);
        rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, nullptr, &right);
        centreBottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.28f, nullptr, &centre);
        dock(PanelId::Examples, left);
        dock(PanelId::Circuit, right);
        dock(PanelId::Pulses, right);
        dock(PanelId::RunControls, rightBottom);
        dock(PanelId::Results, rightBottom);
        dock(PanelId::Diagnostics, centreBottom);
        dock(PanelId::Log, centreBottom);
        dock(PanelId::CodeEditor, centre);
        break;
    case Workspace::Analysis:
    case Workspace::Count:
        // State Views centre; Plots right with Fits; Results bottom; Estimates right-bottom.
        right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.32f, nullptr, &centre);
        rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, nullptr, &right);
        bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.30f, nullptr, &centre);
        dock(PanelId::Plots, right);
        dock(PanelId::Fits, right);
        dock(PanelId::Estimates, rightBottom);
        dock(PanelId::Instruments, rightBottom);
        dock(PanelId::Theory,
             rightBottom); // spec 19 §2 lists it in Analysis; without this it floats
        dock(PanelId::Results, bottom);
        dock(PanelId::StateViews, centre);
        break;
    }
    ImGui::DockBuilderFinish(root);
}

} // namespace qlab::ui
