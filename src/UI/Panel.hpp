#pragma once
// Spec 19 §3 — "each panel is a class deriving ui::Panel { id, title, icon, defaultWorkspace,
// draw(), serialize() }. Panels read snapshots and post commands; they never own model state."
//
// A panel's `key()` is its stable identity: it names the ImGui window (through the `###key` suffix,
// so the visible title may be localised without breaking the layout), it is the key of its slice of
// the project's layout JSON, and it is what the workspace presets list.
#include "Core/Json.hpp"
#include "UI/Context.hpp"
#include "Viz/Types.hpp"
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::ui {

// Spec 19 §2 — the three built-in workspace presets.
enum class Workspace : std::uint8_t { Lab, Program, Analysis, Count };
inline constexpr std::size_t kWorkspaceCount = static_cast<std::size_t>(Workspace::Count);
std::string_view workspaceName(Workspace w);      // "lab", "program", "analysis"
std::string_view workspaceLabelKey(Workspace w);  // strings.en.json key
std::optional<Workspace> workspaceFromName(std::string_view name);

// Spec 19 §3 panel catalog, in table order.
enum class PanelId : std::uint8_t {
    Viewport, Inspector, ComponentTree, CodeEditor, Diagnostics, Circuit, Pulses, RunControls,
    Results, StateViews, Plots, Fits, Instruments, Fridge, Estimates, Theory, Examples, Project, Log, Tour,
    Count
};
inline constexpr std::size_t kPanelCount = static_cast<std::size_t>(PanelId::Count);

class Panel {
public:
    virtual ~Panel() = default;

    // ---- identity (spec 19 §3)
    virtual PanelId id() const = 0;
    virtual std::string_view key() const = 0;         // "viewport", "code_editor", …
    virtual std::string_view titleKey() const = 0;    // strings.en.json key of the visible title
    virtual std::string_view icon() const = 0;        // one glyph from the atlas ranges
    virtual Workspace defaultWorkspace() const = 0;
    // Spec 00 §6 / 19 §2: a Simulator-only panel is hidden by the Physical-lab toggle.
    virtual viz::Observability observability() const { return viz::Observability::Physical; }

    virtual void draw(UiContext& ctx) = 0;
    // Spec 19 §4/§9: the panel's own state inside the project layout JSON.
    virtual core::Json serialize() const { return core::Json::object(); }
    virtual void deserialize(const core::Json&) {}

    bool simulatorOnly() const { return observability() == viz::Observability::SimulatorOnly; }
    std::string title() const;         // resolved through the global strings table
    std::string windowTitle() const;   // "⬒ Viewport###viewport" — a stable ImGui window id
    // Whether the panel is in the workspace's visibility set; the Shell owns this flag.
    bool open = true;
};

using PanelPtr = std::unique_ptr<Panel>;

// Identity boilerplate for the catalog panels: everything but `draw` (and, where it has state,
// `serialize`/`deserialize`) comes from the constructor.
class BasicPanel : public Panel {
public:
    BasicPanel(PanelId id, std::string_view key, std::string_view titleKey, std::string_view icon, Workspace ws,
               viz::Observability obs = viz::Observability::Physical)
        : id_(id), key_(key), titleKey_(titleKey), icon_(icon), workspace_(ws), obs_(obs) {}

    PanelId id() const final { return id_; }
    std::string_view key() const final { return key_; }
    std::string_view titleKey() const final { return titleKey_; }
    std::string_view icon() const final { return icon_; }
    Workspace defaultWorkspace() const final { return workspace_; }
    viz::Observability observability() const final { return obs_; }

private:
    PanelId id_;
    std::string_view key_, titleKey_, icon_;
    Workspace workspace_;
    viz::Observability obs_;
};

// The whole §3 catalog, one instance each, in table order. Every panel is constructible without a
// model: an empty context draws placeholders.
std::vector<PanelPtr> makeAllPanels();
// One panel by id (the App may build a subset).
PanelPtr makePanel(PanelId id);
std::string_view panelKey(PanelId id);
std::optional<PanelId> panelFromKey(std::string_view key);

// Spec 19 §2 — a workspace preset: the panels it shows and the one raised on switch.
struct WorkspacePreset {
    Workspace workspace = Workspace::Lab;
    std::span<const PanelId> panels;
    PanelId focus = PanelId::Viewport;
    bool contains(PanelId id) const;
};
const WorkspacePreset& preset(Workspace w);

} // namespace qlab::ui
