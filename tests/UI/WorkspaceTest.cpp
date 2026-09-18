// Spec 19 §2/§3/§4/§9 — the panel catalog, the workspace presets, the Physical-lab rule and layout
// persistence. All headless: no ImGui context is needed for any of it.
#include "UI/Layout.hpp"
#include "UI/Panel.hpp"
#include "UI/Shell.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <set>

using namespace qlab;
using namespace qlab::ui;

TEST_CASE("Panels: the spec 19 §3 catalog is complete and every panel is identifiable") {
    const std::vector<PanelPtr> panels = makeAllPanels();
    REQUIRE(panels.size() == kPanelCount);
    CHECK(kPanelCount == 20);   // the §3 table has twenty rows (Guided tour added, spec 17 §7.10)

    std::set<std::string> keys, titles;
    for (std::size_t i = 0; i < panels.size(); ++i) {
        const PanelPtr& p = panels[i];
        REQUIRE(p != nullptr);
        INFO("panel " << p->key());
        // The catalog is in table order and the ids agree with the enumeration.
        CHECK(static_cast<std::size_t>(p->id()) == i);
        CHECK(p->key() == panelKey(p->id()));
        CHECK(panelFromKey(p->key()) == p->id());
        CHECK_FALSE(p->key().empty());
        CHECK_FALSE(p->titleKey().empty());
        CHECK_FALSE(p->icon().empty());
        CHECK(p->defaultWorkspace() < Workspace::Count);
        CHECK(keys.insert(std::string(p->key())).second);
        CHECK(titles.insert(p->title()).second);
        // The window id is stable even though the title is localised.
        CHECK(p->windowTitle().find(std::string("###") + std::string(p->key())) != std::string::npos);
        // A panel with no model draws nothing but must still serialize cleanly.
        const core::Json state = p->serialize();
        CHECK((state.is_object() || state.is_null()));
        p->deserialize(state);
        p->deserialize(core::Json{});          // tolerant of rubbish
        p->deserialize(core::Json::array());
    }
    CHECK_FALSE(panelFromKey("no_such_panel").has_value());
}

TEST_CASE("Workspaces: every panel appears in a preset and the presets follow spec 19 §2") {
    std::set<PanelId> covered;
    for (std::size_t w = 0; w < kWorkspaceCount; ++w) {
        const WorkspacePreset& p = preset(static_cast<Workspace>(w));
        CHECK(p.workspace == static_cast<Workspace>(w));
        CHECK_FALSE(p.panels.empty());
        CHECK(p.contains(p.focus));
        for (PanelId id : p.panels) covered.insert(id);
    }
    // Spec 19 §9: "every panel in §3 exists, appears in its workspace preset, and persists its state".
    for (std::size_t i = 0; i < kPanelCount; ++i) {
        INFO("panel " << panelKey(static_cast<PanelId>(i)));
        CHECK(covered.contains(static_cast<PanelId>(i)));
    }
    // The §2 table, panel by panel.
    const WorkspacePreset& lab = preset(Workspace::Lab);
    for (PanelId id : {PanelId::Viewport, PanelId::Inspector, PanelId::ComponentTree, PanelId::Instruments,
                       PanelId::Fridge, PanelId::Log, PanelId::Tour})
        CHECK(lab.contains(id));
    CHECK(lab.focus == PanelId::Viewport);
    const WorkspacePreset& program = preset(Workspace::Program);
    for (PanelId id : {PanelId::CodeEditor, PanelId::Diagnostics, PanelId::Circuit, PanelId::Pulses,
                       PanelId::RunControls, PanelId::Results, PanelId::Examples, PanelId::Log})
        CHECK(program.contains(id));
    CHECK(program.focus == PanelId::CodeEditor);
    const WorkspacePreset& analysis = preset(Workspace::Analysis);
    for (PanelId id : {PanelId::StateViews, PanelId::Plots, PanelId::Fits, PanelId::Results, PanelId::Estimates,
                       PanelId::Theory})
        CHECK(analysis.contains(id));
    CHECK(analysis.focus == PanelId::StateViews);

    CHECK(workspaceFromName("lab") == Workspace::Lab);
    CHECK(workspaceFromName("analysis") == Workspace::Analysis);
    CHECK_FALSE(workspaceFromName("nope").has_value());
    for (std::size_t w = 0; w < kWorkspaceCount; ++w) {
        const auto ws = static_cast<Workspace>(w);
        CHECK(workspaceFromName(workspaceName(ws)) == ws);
    }
}

TEST_CASE("Physical lab hides every Simulator-only panel and nothing else (spec 19 §2, 00 §6)") {
    Shell shell;
    std::size_t simulatorOnly = 0;
    for (const PanelPtr& p : shell.panels())
        if (p->simulatorOnly()) ++simulatorOnly;
    CHECK(simulatorOnly >= 1);      // the catalog really has one

    for (std::size_t w = 0; w < kWorkspaceCount; ++w) {
        const auto ws = static_cast<Workspace>(w);
        shell.setWorkspace(ws);
        shell.setPhysicalLab(false);
        std::vector<std::string> shownSimulated;
        for (const PanelPtr& p : shell.panels())
            if (shell.visible(*p)) shownSimulated.emplace_back(p->key());

        shell.setPhysicalLab(true);
        for (const PanelPtr& p : shell.panels()) {
            INFO("workspace " << workspaceName(ws) << ", panel " << p->key());
            if (p->simulatorOnly()) {
                CHECK_FALSE(shell.visible(*p));           // hidden, whatever the preset says
                CHECK(shell.hiddenByPhysicalLab(*p));
            } else {
                // A Physical panel is untouched by the toggle.
                CHECK(shell.visible(*p) == preset(ws).contains(p->id()));
                CHECK_FALSE(shell.hiddenByPhysicalLab(*p));
            }
        }
        // Exactly the Simulator-only panels disappeared.
        std::vector<std::string> shownPhysical;
        for (const PanelPtr& p : shell.panels())
            if (shell.visible(*p)) shownPhysical.emplace_back(p->key());
        std::sort(shownSimulated.begin(), shownSimulated.end());   // set_difference wants sorted input
        std::sort(shownPhysical.begin(), shownPhysical.end());
        std::vector<std::string> removed;
        std::set_difference(shownSimulated.begin(), shownSimulated.end(), shownPhysical.begin(), shownPhysical.end(),
                            std::back_inserter(removed));
        for (const std::string& key : removed) {
            const auto id = panelFromKey(key);
            REQUIRE(id.has_value());
            CHECK(shell.panel(*id)->simulatorOnly());
        }
        shell.setPhysicalLab(false);
    }
}

TEST_CASE("Layout: the project JSON round-trips through the envelope (spec 19 §4, 23 §2)") {
    LayoutState state;
    state.active = Workspace::Analysis;
    state.physicalLab = true;
    state.palette = "light";
    state.fontScale = 1.25f;
    state.of(Workspace::Lab).ini = "[Window][Viewport]\nPos=0,0\nSize=400,300\n";
    state.of(Workspace::Lab).customised = true;
    state.of(Workspace::Lab).open = {"viewport", "inspector"};
    state.of(Workspace::Analysis).open = {"state_views", "plots"};
    state.panels["code_editor"] = core::Json::object({{"text", "OPENQASM 3.0;\n"}, {"caret_line", 1}});
    state.panels["results"] = core::Json::object({{"sort_by_count", false}});
    state.shortcuts = Shortcuts::defaults().toJson();

    const std::string text = state.serialize();
    CHECK(text.find("\"ui.layout\"") != std::string::npos);
    const auto reloaded = LayoutState::parse(text);
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->active == Workspace::Analysis);
    CHECK(reloaded->physicalLab);
    CHECK(reloaded->palette == "light");
    CHECK(reloaded->fontScale == 1.25f);
    CHECK(reloaded->of(Workspace::Lab).ini == state.of(Workspace::Lab).ini);
    CHECK(reloaded->of(Workspace::Lab).customised);
    CHECK(reloaded->of(Workspace::Lab).open == state.of(Workspace::Lab).open);
    CHECK(reloaded->of(Workspace::Analysis).open == state.of(Workspace::Analysis).open);
    CHECK(reloaded->panels.at("code_editor") == state.panels.at("code_editor"));
    CHECK(reloaded->panels.at("results") == state.panels.at("results"));
    CHECK(reloaded->toJson() == state.toJson());

    // The user font scale is clamped on load (spec 19 §7).
    core::Json wide = state.toJson();
    wide["font_scale"] = 9.0;
    CHECK(LayoutState::fromJson(wide).value().fontScale == 1.6f);

    // A layout naming a panel that does not exist is an error naming it.
    core::Json broken = state.toJson();
    broken["panels"]["no_such_panel"] = core::Json::object();
    const auto failed = LayoutState::fromJson(broken);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().message.find("no_such_panel") != std::string::npos);
    core::Json badWorkspace = state.toJson();
    badWorkspace["active"] = "nowhere";
    REQUIRE_FALSE(LayoutState::fromJson(badWorkspace).has_value());
}

TEST_CASE("Shell: a layout restores the workspace, the visibility set and each panel's state") {
    Shell shell;
    shell.setWorkspace(Workspace::Program);
    shell.setOpen(PanelId::Examples, false);
    shell.setPhysicalLab(true);
    Panel* editor = shell.panel(PanelId::CodeEditor);
    REQUIRE(editor != nullptr);
    editor->deserialize(core::Json::object({{"text", "qubit[2] q;\n"}, {"caret_line", 1}, {"caret_column", 1}}));

    const LayoutState saved = shell.saveLayout();
    CHECK(saved.active == Workspace::Program);
    CHECK(saved.physicalLab);
    const std::vector<std::string>& open = saved.of(Workspace::Program).open;
    CHECK(std::find(open.begin(), open.end(), "examples") == open.end());
    CHECK(std::find(open.begin(), open.end(), "code_editor") != open.end());
    REQUIRE(saved.panels.contains("code_editor"));
    CHECK(saved.panels.at("code_editor").at("text").get<std::string>() == "qubit[2] q;\n");

    Shell fresh;
    REQUIRE(fresh.loadLayout(saved));
    CHECK(fresh.workspace() == Workspace::Program);
    CHECK(fresh.physicalLab());
    CHECK_FALSE(fresh.isOpen(PanelId::Examples));
    CHECK(fresh.isOpen(PanelId::CodeEditor));
    CHECK(fresh.panel(PanelId::CodeEditor)->serialize().at("text").get<std::string>() == "qubit[2] q;\n");

    // Switching workspaces preserves panel state (spec 19 §2: "switching is instant and preserves
    // panel state").
    fresh.setWorkspace(Workspace::Analysis);
    fresh.setWorkspace(Workspace::Program);
    CHECK(fresh.panel(PanelId::CodeEditor)->serialize().at("text").get<std::string>() == "qubit[2] q;\n");
    CHECK_FALSE(fresh.isOpen(PanelId::Examples));

    // Resetting a workspace restores its preset visibility set.
    fresh.resetLayout(Workspace::Program);
    CHECK(fresh.isOpen(PanelId::Examples));

    // A layout with an unknown panel key is refused rather than silently dropped.
    LayoutState broken = saved;
    broken.of(Workspace::Program).open.emplace_back("ghost_panel");
    Shell third;
    CHECK_FALSE(third.loadLayout(broken));
}
