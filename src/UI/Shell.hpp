#pragma once
// Spec 19 §2/§4 — the shell: the top bar (workspace tabs, device, backend, run status with the shot
// counter, the Physical-lab toggle and the project name), the dockspace that fills the window below
// it, the panel windows, the keyboard table and layout persistence.
//
// The App owns the model and the ImGui backend; it calls `draw()` once per frame between
// `ImGui::NewFrame()` and `ImGui::Render()`.
#include "UI/Layout.hpp"
#include "UI/Panel.hpp"
#include "UI/Shortcuts.hpp"
#include "Viz/Selection.hpp"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::ui {

class Shell {
public:
    Shell();                                        // the whole spec 19 §3 catalog
    explicit Shell(std::vector<PanelPtr> panels);   // a subset (tests)
    ~Shell();
    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;

    // ---- panels
    std::span<const PanelPtr> panels() const { return panels_; }
    Panel* panel(PanelId id) const;
    Panel* panel(std::string_view key) const;

    // ---- workspaces (spec 19 §2: switching is instant and preserves panel state)
    Workspace workspace() const { return workspace_; }
    void setWorkspace(Workspace w);
    // Spec 19 §2: hides every Simulator-only panel and overlay.
    bool physicalLab() const { return physicalLab_; }
    void setPhysicalLab(bool on);
    bool hiddenByPhysicalLab(const Panel& p) const { return physicalLab_ && p.simulatorOnly(); }
    // In the preset, not closed by the user, and not hidden by the Physical-lab toggle.
    bool visible(const Panel& p) const;
    void setOpen(PanelId id, bool on);
    bool isOpen(PanelId id) const;

    // ---- one frame
    void draw(UiContext& ctx);
    // Spec 19 §5.8: the slim top-bar progress bar; `progress` < 0 is indeterminate, an empty
    // message clears it.
    void setBusy(std::string_view message, double progress);

    // ---- layout (spec 19 §4)
    // Captures the live ImGui settings of the current workspace first, so saving while sitting
    // in a workspace records the arrangement the user is looking at.
    LayoutState saveLayout();
    Status loadLayout(LayoutState state);
    void resetLayout(Workspace w);     // re-run the preset's DockBuilder script on the next frame
    // Spec 19 §7: the user's text scale. Layout is in logical points, so this is the ONE knob that
    // magnifies the interface; the display scale only decides how finely the faces are rasterised.
    // The App notices a change and re-rasterises the atlas.
    float fontScale() const { return fontScale_; }
    void setFontScale(float s);
    const Shortcuts& keys() const { return keys_; }
    void setKeys(Shortcuts k) { keys_ = std::move(k); }
    // Actions that fired during the last `draw()`; the App handles what the Shell does not.
    std::span<const Action> firedActions() const { return fired_; }

    // Makes ImGui's docking available and disables `imgui.ini` (spec 19 §4). Idempotent; the App
    // may call it once after creating the context, and `draw` calls it defensively.
    static void configureImGui();

private:
    void drawTopBar(UiContext& ctx, float& heightOut);
    void drawMenus(UiContext& ctx);
    void drawDockspace(UiContext& ctx, float topHeight);
    void drawPanels(UiContext& ctx);
    void handleShortcuts(UiContext& ctx);
    void buildDock(unsigned dockspaceId);           // the preset's DockBuilder script
    void captureIni();                              // current ImGui settings → the active workspace
    void applyPending();                            // deferred ini load after a workspace switch

    // Spec 21 §1.1: the bridge remembers what each side looked like last frame, so ONE instance
    // must live across frames; it is rebuilt when the App swaps the scene or the selection.
    std::optional<viz::LabSelectionBridge> bridge_;
    const void* bridgeSelection_ = nullptr;
    const void* bridgeLab_ = nullptr;

    std::vector<PanelPtr> panels_;
    std::array<std::vector<bool>, kWorkspaceCount> open_;   // per workspace, per PanelId
    std::array<WorkspaceLayout, kWorkspaceCount> saved_;
    Workspace workspace_ = Workspace::Lab;
    bool physicalLab_ = false;
    bool rebuildDock_ = true;
    bool applyIni_ = false;
    std::string busyMessage_;
    double busyProgress_ = -1.0;
    Shortcuts keys_ = Shortcuts::defaults();
    std::vector<Action> fired_;
    std::string palette_ = "dark";
    float fontScale_ = 1.0f;
};

} // namespace qlab::ui
