// Spec 19 §2/§3 — the panel catalog and the workspace presets (see Panel.hpp).
#include "UI/Panel.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Strings.hpp"
#include <algorithm>
#include <array>

namespace qlab::ui {
namespace {

struct Entry {
    PanelId id;
    std::string_view key;
    PanelPtr (*make)();
};
constexpr std::array<Entry, kPanelCount> kCatalog{{
    {PanelId::Viewport, "viewport", &makeViewportPanel},
    {PanelId::Inspector, "inspector", &makeInspectorPanel},
    {PanelId::ComponentTree, "component_tree", &makeComponentTreePanel},
    {PanelId::CodeEditor, "code_editor", &makeCodeEditorPanel},
    {PanelId::Diagnostics, "diagnostics", &makeDiagnosticsPanel},
    {PanelId::Circuit, "circuit", &makeCircuitPanel},
    {PanelId::Pulses, "pulses", &makePulsePanel},
    {PanelId::RunControls, "run_controls", &makeRunControlsPanel},
    {PanelId::Results, "results", &makeResultsPanel},
    {PanelId::StateViews, "state_views", &makeStateViewsPanel},
    {PanelId::Plots, "plots", &makePlotsPanel},
    {PanelId::Fits, "fits", &makeFitsPanel},
    {PanelId::Instruments, "instruments", &makeInstrumentsPanel},
    {PanelId::Fridge, "fridge_dashboard", &makeFridgePanel},
    {PanelId::Estimates, "estimates", &makeEstimatesPanel},
    {PanelId::Theory, "theory", &makeTheoryPanel},
    {PanelId::Examples, "examples", &makeExamplesPanel},
    {PanelId::Project, "project", &makeProjectPanel},
    {PanelId::Log, "log", &makeLogPanel},
    {PanelId::Tour, "tour", &makeTourPanel},
}};

// Spec 19 §2 visibility sets. Two panels the §2 table does not place: the Project / Device Manager
// (device inspection belongs beside the lab) and the Instruments panel in Analysis, which is where
// §2 lists "Tomography / Benchmarking (from 12 §7)" — that tooling lives in the instrument registry
// rather than in a panel of its own (SPEC_DEVIATIONS.md).
constexpr std::array<PanelId, 8> kLabPanels{PanelId::Viewport,   PanelId::Inspector, PanelId::ComponentTree,
                                            PanelId::Instruments, PanelId::Fridge,    PanelId::Project,
                                            PanelId::Log,         PanelId::Tour};
constexpr std::array<PanelId, 8> kProgramPanels{PanelId::CodeEditor,  PanelId::Diagnostics, PanelId::Circuit,
                                                PanelId::Pulses,      PanelId::RunControls, PanelId::Results,
                                                PanelId::Examples,    PanelId::Log};
constexpr std::array<PanelId, 7> kAnalysisPanels{PanelId::StateViews, PanelId::Plots,     PanelId::Fits,
                                                 PanelId::Results,    PanelId::Estimates, PanelId::Theory,
                                                 PanelId::Instruments};

constexpr std::array<WorkspacePreset, kWorkspaceCount> kPresets{{
    {Workspace::Lab, kLabPanels, PanelId::Viewport},
    {Workspace::Program, kProgramPanels, PanelId::CodeEditor},
    {Workspace::Analysis, kAnalysisPanels, PanelId::StateViews},
}};

} // namespace

std::string_view workspaceName(Workspace w) {
    switch (w) {
    case Workspace::Lab: return "lab";
    case Workspace::Program: return "program";
    case Workspace::Analysis: return "analysis";
    case Workspace::Count: break;
    }
    return "lab";
}

std::string_view workspaceLabelKey(Workspace w) {
    switch (w) {
    case Workspace::Lab: return "workspaces.lab";
    case Workspace::Program: return "workspaces.program";
    case Workspace::Analysis: return "workspaces.analysis";
    case Workspace::Count: break;
    }
    return "workspaces.lab";
}

std::optional<Workspace> workspaceFromName(std::string_view name) {
    for (std::size_t i = 0; i < kWorkspaceCount; ++i) {
        const auto w = static_cast<Workspace>(i);
        if (workspaceName(w) == name) return w;
    }
    return std::nullopt;
}

bool WorkspacePreset::contains(PanelId id) const {
    return std::find(panels.begin(), panels.end(), id) != panels.end();
}

const WorkspacePreset& preset(Workspace w) {
    const auto i = std::min(static_cast<std::size_t>(w), kWorkspaceCount - 1);
    return kPresets[i];
}

std::string_view panelKey(PanelId id) {
    for (const Entry& e : kCatalog)
        if (e.id == id) return e.key;
    return "?";
}

std::optional<PanelId> panelFromKey(std::string_view key) {
    for (const Entry& e : kCatalog)
        if (e.key == key) return e.id;
    return std::nullopt;
}

PanelPtr makePanel(PanelId id) {
    for (const Entry& e : kCatalog)
        if (e.id == id) return e.make();
    return nullptr;
}

std::vector<PanelPtr> makeAllPanels() {
    std::vector<PanelPtr> out;
    out.reserve(kCatalog.size());
    for (const Entry& e : kCatalog) out.push_back(e.make());
    return out;
}

std::string Panel::title() const { return std::string(Strings::global().get(titleKey())); }

std::string Panel::windowTitle() const {
    // `###key` keeps the ImGui window identity stable while the visible title is localised.
    return std::string(icon()) + "  " + title() + "###" + std::string(key());
}

} // namespace qlab::ui
