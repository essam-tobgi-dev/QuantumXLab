// Spec 19 §2 — the top bar, the dockspace and the panel windows of one frame (see Shell.hpp).
#include "UI/Format.hpp"
#include "UI/Shell.hpp"
#include "UI/UI.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <utility>

namespace qlab::ui {
namespace {

using widgets::iv;
using widgets::u32;

std::string runStatusText(const UiContext& ctx) {
    const SessionView& s = ctx.session_view;
    const Strings& str = Strings::global();
    switch (s.status) {
    case SessionView::Status::Running: {
        const std::array<std::pair<std::string_view, std::string>, 2> args{
            {{"shot", format::integer(s.shotsDone)}, {"shots", format::integer(s.shotsTotal)}}};
        return str.format("run.status_running", args);
    }
    case SessionView::Status::Done: {
        const std::array<std::pair<std::string_view, std::string>, 1> args{
            {{"ms", format::number(s.lastWallMs, 4)}}};
        return str.format("run.status_done", args);
    }
    case SessionView::Status::Failed: {
        const std::array<std::pair<std::string_view, std::string>, 1> args{
            {{"n", std::to_string(s.errors)}}};
        return str.format("diagnostics.errors", args);
    }
    case SessionView::Status::Compiling:
    case SessionView::Status::Idle:
        break;
    }
    return std::string(str.get(s.statusKey()));
}

Token runStatusToken(SessionView::Status s) {
    switch (s) {
    case SessionView::Status::Running:
        return Token::Ok;
    case SessionView::Status::Compiling:
        return Token::Accent;
    case SessionView::Status::Failed:
        return Token::Err;
    case SessionView::Status::Done:
        return Token::Ok;
    case SessionView::Status::Idle:
        break;
    }
    return Token::TextSecondary;
}

} // namespace

void Shell::drawMenus(UiContext& ctx) {
    if (!ImGui::BeginMenuBar())
        return;
    // The product mark and name lead the bar (the sibling project's convention).
    {
        const float h = ImGui::GetFrameHeight() - 6.0f;
        if (ctx.resources != nullptr && ctx.resources->logo) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
            ImGui::Image(static_cast<ImTextureID>(ctx.resources->logo->id()), ImVec2(h, h));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.0f);
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
        }
        FontScope f(*ctx.fonts, FontRole::Strong);
        widgets::text(ctx, Token::Accent, "QuantumXLab");
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        ImGui::Separator();
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
    }
    const auto item = [&](std::string_view labelKey, Action a, const std::function<void()>& fn,
                          bool enabled = true) {
        const std::string label(ctx.text(labelKey));
        const std::string shortcut = keys_.text(a);
        if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), false,
                            enabled && fn != nullptr) &&
            fn)
            fn();
    };
    if (ImGui::BeginMenu(std::string(ctx.text("menu.file")).c_str())) {
        item("menu.save", Action::SaveProject, ctx.cmd.saveProject);
        if (ImGui::BeginMenu(std::string(ctx.text("menu.export")).c_str())) {
            item("menu.export_results", Action::ExportResults, ctx.cmd.exportResults);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(std::string(ctx.text("menu.edit")).c_str())) {
        const bool canUndo = ctx.undo != nullptr && ctx.undo->canUndo();
        const bool canRedo = ctx.undo != nullptr && ctx.undo->canRedo();
        if (ImGui::MenuItem(std::string(ctx.text("menu.undo")).c_str(),
                            keys_.text(Action::Undo).c_str(), false, canUndo))
            ctx.undo->undo();
        if (ImGui::MenuItem(std::string(ctx.text("menu.redo")).c_str(),
                            keys_.text(Action::Redo).c_str(), false, canRedo))
            ctx.undo->redo();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(std::string(ctx.text("menu.view")).c_str())) {
        for (const PanelPtr& p : panels_) {
            if (!preset(workspace_).contains(p->id()))
                continue;
            bool on = isOpen(p->id());
            const bool blocked = hiddenByPhysicalLab(*p);
            ImGui::BeginDisabled(blocked);
            if (ImGui::MenuItem(p->title().c_str(), nullptr, &on) && !blocked)
                setOpen(p->id(), on);
            ImGui::EndDisabled();
            // Spec 19 §2: greyed entries explain themselves.
            if (blocked)
                widgets::simOnlyDisabledTooltip(ctx);
        }
        ImGui::Separator();
        // Spec 19 §7: the interface is laid out in logical points, so this is the only control that
        // changes its size. It costs a font-atlas rebuild, which the App does on the next frame.
        if (ImGui::BeginMenu("Text size")) {
            for (const auto& [label, value] : {std::pair<const char*, float>{"Smaller", 0.8f},
                                               {"Small", 0.9f},
                                               {"Default", 1.0f},
                                               {"Large", 1.15f},
                                               {"Larger", 1.3f},
                                               {"Largest", 1.6f}}) {
                const bool on = std::abs(fontScale_ - value) < 1e-3f;
                if (ImGui::MenuItem(label, nullptr, on) && !on)
                    setFontScale(value);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reset layout"))
            resetLayout(workspace_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(std::string(ctx.text("menu.run")).c_str())) {
        item("menu.compile", Action::Compile, ctx.cmd.compile);
        item("menu.run_program", Action::Run, ctx.cmd.run);
        item("menu.stop", Action::Stop, ctx.cmd.stop);
        item("menu.step_gate", Action::StepGate, ctx.cmd.stepGate);
        item("menu.step_shot", Action::StepShot, ctx.cmd.stepShot);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(std::string(ctx.text("menu.help")).c_str())) {
        if (ImGui::MenuItem(std::string(ctx.text("menu.theory")).c_str()) && ctx.cmd.openTheory)
            ctx.cmd.openTheory("T01");
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void Shell::drawTopBar(UiContext& ctx, float& heightOut) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const Metrics m = ctx.metrics_px();
    const float height = ImGui::GetFrameHeight() * 2.0f + m.spacing(2);
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, height));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m.spacing(2), m.spacing(1)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, iv(ctx.th()[Token::BgPanel]));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##qxl_topbar", nullptr, kFlags)) {
        drawMenus(ctx);
        // ---- workspace tabs
        for (std::size_t i = 0; i < kWorkspaceCount; ++i) {
            const auto w = static_cast<Workspace>(i);
            if (i > 0)
                ImGui::SameLine();
            const std::string label =
                std::string(ctx.text(workspaceLabelKey(w))) + "  " +
                keys_.text(w == Workspace::Lab       ? Action::WorkspaceLab
                           : w == Workspace::Program ? Action::WorkspaceProgram
                                                     : Action::WorkspaceAnalysis);
            bool selected = w == workspace_;
            if (widgets::toggleChip(ctx, label, &selected) && selected)
                setWorkspace(w);
        }
        // ---- device, backend, run status
        ImGui::SameLine(0.0f, m.spacing(4));
        widgets::labelled(ctx, ctx.text("run.device"),
                          ctx.session_view.device.empty() ? "—" : ctx.session_view.device);
        ImGui::SameLine(0.0f, m.spacing(3));
        widgets::labelled(ctx, ctx.text("run.backend"),
                          ctx.session_view.backend.empty() ? ctx.text("run.backend_auto")
                                                           : ctx.session_view.backend);
        ImGui::SameLine(0.0f, m.spacing(3));
        widgets::text(ctx, runStatusToken(ctx.session_view.status), runStatusText(ctx));
        if (ctx.session_view.status == SessionView::Status::Running &&
            ctx.session_view.shotsTotal > 0) {
            ImGui::SameLine();
            widgets::progressBar(ctx,
                                 static_cast<double>(ctx.session_view.shotsDone) /
                                     static_cast<double>(ctx.session_view.shotsTotal),
                                 {}, ctx.ui(120.0f));
        } else if (!busyMessage_.empty()) {
            // Spec 19 §5.8: long operations show a slim progress bar; the UI never blocks.
            ImGui::SameLine();
            widgets::progressBar(ctx, busyProgress_, busyMessage_, ctx.ui(120.0f));
        }
        // ---- Physical lab toggle and project name, right-aligned
        const std::string physical(ctx.text("app.physical_lab"));
        const std::string project = ctx.session_view.project.empty()
                                        ? std::string(ctx.text("app.project_untitled"))
                                        : ctx.session_view.project;
        const float rightWidth =
            widgets::textSize(physical).x + widgets::textSize(project).x + m.spacing(6) * 2.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - rightWidth));
        bool lab = physicalLab_;
        if (widgets::toggleChip(ctx, physical, &lab))
            setPhysicalLab(lab);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip()) {
            ImGui::PushTextWrapPos(ctx.ui(420.0f));
            ImGui::TextUnformatted(ctx.text("app.physical_lab_tooltip").data());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::SameLine(0.0f, m.spacing(3));
        widgets::text(ctx, Token::TextSecondary, project);
    }
    heightOut = ImGui::GetWindowHeight();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void Shell::drawDockspace(UiContext& ctx, float topHeight) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + topHeight));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, std::max(1.0f, vp->WorkSize.y - topHeight)));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##qxl_dockhost", nullptr, kFlags);
    ImGui::PopStyleVar(3);
    const ImGuiID dockspace = ImGui::GetID("qxl_dockspace");
    if (rebuildDock_) {
        buildDock(dockspace);
        rebuildDock_ = false;
    }
    ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, iv(ctx.th()[Token::BgBase]));
    ImGui::DockSpace(dockspace, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::PopStyleColor();
    ImGui::End();
}

void Shell::drawPanels(UiContext& ctx) {
    for (const PanelPtr& p : panels_) {
        if (!visible(*p))
            continue;
        bool open = true;
        const std::string title = p->windowTitle();
        ImGui::PushStyleColor(ImGuiCol_WindowBg, iv(ctx.th()[Token::BgPanel]));
        const bool shown = ImGui::Begin(title.c_str(), &open);
        if (shown) {
            FontScope f(*ctx.fonts, FontRole::Body);
            p->draw(ctx);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        if (!open)
            setOpen(p->id(), false);
    }
}

void Shell::handleShortcuts(UiContext& ctx) {
    const bool typing = ImGui::GetIO().WantTextInput;
    fired_ = pollShortcuts(keys_, typing);
    for (Action a : fired_) {
        switch (a) {
        case Action::WorkspaceLab:
            setWorkspace(Workspace::Lab);
            break;
        case Action::WorkspaceProgram:
            setWorkspace(Workspace::Program);
            break;
        case Action::WorkspaceAnalysis:
            setWorkspace(Workspace::Analysis);
            break;
        case Action::TogglePhysicalLab:
            setPhysicalLab(!physicalLab_);
            break;
        case Action::Undo:
            if (ctx.undo != nullptr)
                ctx.undo->undo();
            break;
        case Action::Redo:
            if (ctx.undo != nullptr)
                ctx.undo->redo();
            break;
        case Action::Run:
            if (ctx.cmd.run)
                ctx.cmd.run();
            break;
        case Action::Stop:
            if (ctx.cmd.stop)
                ctx.cmd.stop();
            break;
        case Action::Compile:
            if (ctx.cmd.compile)
                ctx.cmd.compile();
            break;
        case Action::StepGate:
            if (ctx.cmd.stepGate)
                ctx.cmd.stepGate();
            break;
        case Action::StepShot:
            if (ctx.cmd.stepShot)
                ctx.cmd.stepShot();
            break;
        case Action::SaveProject:
            if (ctx.cmd.saveProject)
                ctx.cmd.saveProject();
            break;
        case Action::ExportResults:
            if (ctx.cmd.exportResults)
                ctx.cmd.exportResults();
            break;
        case Action::Settings:
            if (ctx.cmd.settings)
                ctx.cmd.settings();
            break;
        default:
            break; // the panels handle the rest (focus, X-ray, autocomplete, …)
        }
    }
}

void Shell::draw(UiContext& ctx) {
    if (ImGui::GetCurrentContext() == nullptr || ctx.theme == nullptr || ctx.fonts == nullptr)
        return;
    configureImGui();
    applyPending();
    ctx.physicalLab = physicalLab_;
    ctx.keys = &keys_;
    // Spec 21 §1.1: the 3D lab and the state views select together — whichever side changed since
    // the last call is copied to the other, once per frame, before anything draws.
    if (ctx.selection != nullptr && ctx.interaction != nullptr) {
        if (!bridge_ || bridgeSelection_ != ctx.selection || bridgeLab_ != ctx.interaction) {
            bridge_.emplace(*ctx.selection, *ctx.interaction);
            bridgeSelection_ = ctx.selection;
            bridgeLab_ = ctx.interaction;
        }
        bridge_->sync();
    }
    // Spec 19 §2 / 17 §8: the Physical-lab toggle hides the Simulator-only OVERLAYS as well as the
    // Simulator-only panels — the Bloch markers above the transmon pads are the probe of spec 00
    // §6.
    if (ctx.overlays != nullptr)
        ctx.overlays->setEnabled(ctx.overlays->temperatureTint(), true, !physicalLab_, true);
    if (!ctx.cmd.setBusy)
        ctx.cmd.setBusy = [this](std::string_view msg, double p) { setBusy(msg, p); };
    float topHeight = 0.0f;
    drawTopBar(ctx, topHeight);
    drawDockspace(ctx, topHeight);
    drawPanels(ctx);
    handleShortcuts(ctx);
}

} // namespace qlab::ui
