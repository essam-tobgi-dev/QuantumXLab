// Spec 19 §2/§4/§9 — a headless frame of each workspace: the top bar, the dockspace, every panel of
// the preset and the keyboard table run exactly as in the application, with the draw lists produced
// and discarded. An ImGui assertion (an unbalanced Begin/End, a stray PopStyleColor, a table left
// open) aborts the test, which is the point. The layout the frames leave behind must round-trip.
#include "Data/Fidelity.hpp"
#include "UiHarness.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h> // FindWindowByName: the test inspects which windows the frame submitted

using namespace qlab;
using namespace qlab::ui;

namespace {

// A two-qubit Bell state and a small run result, so the panels draw content rather than only
// placeholders. Nothing here needs a GL context.
struct Fixture {
    std::vector<std::unique_ptr<viz::IStateView>> views = viz::makeAllViews();
    viz::SelectionModel selection;
    viz::ViewInput input;
    runtime::RunResult result;
    compiler::PassMetrics metrics;
    runtime::Estimate estimate;
    std::vector<lang::Diagnostic> diagnostics;
    data::Recorder recorder;

    Fixture() {
        auto snapshot = std::make_shared<qsim::Snapshot>();
        snapshot->nQubits = 2;
        snapshot->gateIndex = 2;
        const double s = 1.0 / std::sqrt(2.0);
        snapshot->amplitudes = std::vector<num::Complex>{s, 0.0, 0.0, s};
        input.snapshot = std::move(snapshot);

        auto counts = std::make_shared<data::Histogram>(2);
        counts->add("00", 512);
        counts->add("11", 488);
        input.counts = counts;

        result.counts = *counts;
        result.device = "sc_fixed_5";
        result.backendReason = "state vector (n = 2)";
        result.shotsCompleted = 1000;
        result.seed = 7;
        result.layout.bits = 2;
        result.layout.registers.push_back(
            runtime::RegisterInfo{"c", 0, 2, ir::RegKind::Bit, false, false});
        for (int i = 0; i < 4; ++i)
            result.memory.push_back(
                runtime::ShotRecord{{static_cast<std::uint8_t>(i & 1), 0}, {}, false});
        result.expectations.push_back(
            runtime::Expectation{"Z0", 0.048, 0.031, data::FidelityClass::Statistical, 0.0});

        metrics.gateCount = 3;
        metrics.twoQubitCount = 1;
        metrics.depth = 2;
        metrics.estimatedDuration = Picoseconds{320'000};
        estimate.device = "sc_fixed_5";
        estimate.wallTime.valueS = 0.42;
        estimate.wallTime.perShotS = 4.2e-4;
        estimate.resources.qubits = 2;
        estimate.assumptions = {"independent_errors", "calibration_static"};

        diagnostics.push_back(lang::Diagnostics::make("QL3007", SourceSpan{3, 1, 3, 4, {}}, "myx"));

        const data::ChannelId channel =
            recorder.add(data::ChannelDesc{"t1.p1", "", "s", data::FidelityClass::Statistical, 1});
        for (int i = 0; i < 64; ++i)
            (void)recorder.push(channel, static_cast<double>(i) * 1e-6,
                                std::exp(-static_cast<double>(i) / 20.0));
    }

    void bind(UiContext& ctx) {
        ctx.views = &views;
        ctx.selection = &selection;
        ctx.viewInput = &input;
        ctx.result = &result;
        ctx.metrics = &metrics;
        ctx.estimate = &estimate;
        ctx.diagnostics = &diagnostics;
        ctx.recorder = &recorder;
        ctx.session_view.device = "sc_fixed_5";
        ctx.session_view.backend = "state vector";
        ctx.session_view.project = "frame test";
        ctx.session_view.status = SessionView::Status::Done;
        ctx.session_view.lastWallMs = 12.5;
        ctx.session_view.shotsDone = 1000;
        ctx.session_view.shotsTotal = 1000;
    }
};

} // namespace

TEST_CASE("Shell: a headless frame of every workspace draws without an ImGui assertion") {
    test::UiHarness ui;
    if (!ui.ready())
        SKIP("the pinned fonts are not available");
    Shell shell;
    Fixture fixture;
    fixture.bind(ui.context());

    for (std::size_t w = 0; w < kWorkspaceCount; ++w) {
        const auto workspace = static_cast<Workspace>(w);
        shell.setWorkspace(workspace);
        INFO("workspace " << workspaceName(workspace));
        // Three frames: the first builds the dock layout, the second draws into it, the third
        // proves the layout is stable.
        for (int frame = 0; frame < 3; ++frame)
            ui.frame(shell);
        CHECK(shell.workspace() == workspace);
        CHECK(ui.vertices() > 500); // the panels really drew something
    }
}

TEST_CASE("Shell: an empty context draws placeholders instead of crashing") {
    test::UiHarness ui;
    if (!ui.ready())
        SKIP("the pinned fonts are not available");
    Shell shell;
    // No session, no scene, no views: every panel must fall back to its placeholder.
    for (std::size_t w = 0; w < kWorkspaceCount; ++w) {
        shell.setWorkspace(static_cast<Workspace>(w));
        for (int frame = 0; frame < 2; ++frame)
            ui.frame(shell);
    }
    CHECK(ui.vertices() > 0);
}

TEST_CASE("Shell: Physical lab mode drops the Simulator-only windows from the frame (spec 19 §2)") {
    test::UiHarness ui;
    if (!ui.ready())
        SKIP("the pinned fonts are not available");
    Shell shell;
    Fixture fixture;
    fixture.bind(ui.context());
    shell.setWorkspace(Workspace::Analysis);
    for (int frame = 0; frame < 2; ++frame)
        ui.frame(shell);

    const auto windowExists = [](const Panel& p) {
        return ImGui::FindWindowByName(p.windowTitle().c_str()) != nullptr;
    };
    const Panel* simulated = nullptr;
    for (const PanelPtr& p : shell.panels())
        if (p->simulatorOnly() && preset(Workspace::Analysis).contains(p->id()))
            simulated = p.get();
    REQUIRE(simulated != nullptr);
    CHECK(windowExists(*simulated));
    const bool wasActive = ImGui::FindWindowByName(simulated->windowTitle().c_str())->WasActive;
    CHECK(wasActive);

    shell.setPhysicalLab(true);
    for (int frame = 0; frame < 2; ++frame)
        ui.frame(shell);
    CHECK_FALSE(shell.visible(*simulated));
    CHECK_FALSE(ImGui::FindWindowByName(simulated->windowTitle().c_str())->WasActive);
    // A Physical panel of the same workspace is still drawn.
    const Panel* results = shell.panel(PanelId::Results);
    REQUIRE(results != nullptr);
    CHECK(ImGui::FindWindowByName(results->windowTitle().c_str())->WasActive);
}

TEST_CASE("Shell: the layout the frames leave behind round-trips (spec 19 §4)") {
    test::UiHarness ui;
    if (!ui.ready())
        SKIP("the pinned fonts are not available");
    Shell shell;
    Fixture fixture;
    fixture.bind(ui.context());

    // Visit every workspace so each one captures an ini, then come back to the first.
    for (std::size_t w = 0; w < kWorkspaceCount; ++w) {
        shell.setWorkspace(static_cast<Workspace>(w));
        for (int frame = 0; frame < 2; ++frame)
            ui.frame(shell);
    }
    shell.setWorkspace(Workspace::Lab);
    ui.frame(shell);

    const LayoutState saved = shell.saveLayout();
    // At least one workspace captured a real ImGui ini (the ones that were left).
    const bool anyIni = std::any_of(saved.workspaces.begin(), saved.workspaces.end(),
                                    [](const WorkspaceLayout& w) { return !w.ini.empty(); });
    CHECK(anyIni);
    for (const WorkspaceLayout& w : saved.workspaces)
        CHECK_FALSE(w.open.empty());

    const std::string text = saved.serialize();
    const auto reloaded = LayoutState::parse(text);
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->toJson() == saved.toJson());

    Shell restored;
    REQUIRE(restored.loadLayout(*reloaded));
    CHECK(restored.workspace() == Workspace::Lab);
    for (int frame = 0; frame < 2; ++frame)
        ui.frame(restored);
    // The restored shell shows the same panels.
    for (const PanelPtr& p : shell.panels())
        CHECK(restored.isOpen(p->id()) == shell.isOpen(p->id()));
}

TEST_CASE("Shell: the keyboard table drives the workspace and Physical-lab toggles (spec 19 §5)") {
    test::UiHarness ui;
    if (!ui.ready())
        SKIP("the pinned fonts are not available");
    Shell shell;
    Fixture fixture;
    fixture.bind(ui.context());
    ui.frame(shell);
    CHECK(shell.workspace() == Workspace::Lab);

    const auto press = [&](ImGuiKey key, bool ctrl) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        io.AddKeyEvent(key, true);
        ui.frame(shell);
        io.AddKeyEvent(key, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        ui.frame(shell);
    };
    press(ImGuiKey_2, true);
    CHECK(shell.workspace() == Workspace::Program);
    press(ImGuiKey_3, true);
    CHECK(shell.workspace() == Workspace::Analysis);
    press(ImGuiKey_1, true);
    CHECK(shell.workspace() == Workspace::Lab);

    CHECK_FALSE(shell.physicalLab());
    press(ImGuiKey_L, true);
    CHECK(shell.physicalLab());
    press(ImGuiKey_L, true);
    CHECK_FALSE(shell.physicalLab());

    // F5 reaches the run command.
    int runs = 0;
    ui.context().cmd.run = [&runs] { ++runs; };
    press(ImGuiKey_F5, false);
    CHECK(runs == 1);
    CHECK(std::find(shell.firedActions().begin(), shell.firedActions().end(), Action::Run) ==
          shell.firedActions().end()); // the second (key-up) frame fired nothing
}
