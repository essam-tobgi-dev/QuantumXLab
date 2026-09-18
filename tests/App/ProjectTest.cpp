// Spec 23 §2 / 19 §4 — the project lifecycle the App owns: new / save-as / open, the run history
// in `<stem>.qxlab.d/runs/`, the 60 s autosave and the crash-recovery prompt, and the opaque UI
// workspace slice. The round trip is the assertion: everything put in comes back out, and the
// `run_result` documents written beside the project verify against their own memory blob (§12).
#include "App/App.hpp"
#include "Core/Paths.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>

using namespace qlab;

namespace {

std::filesystem::path scratch(std::string_view name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "qxl_app_test" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::unique_ptr<app::LabModel> headlessModel() {
    app::LabModel::Config config;
    config.device = "sc_fixed_5";
    config.buildLab = false;      // a project round trip needs no scene
    config.instruments = false;
    auto model = app::LabModel::create(config);
    if (!model) FAIL(model.error().format());
    return std::move(*model);
}

} // namespace

TEST_CASE("a project round-trips through .qxlab") {
    const std::filesystem::path dir = scratch("roundtrip");
    const std::filesystem::path file = dir / "bell.qxlab";
    const auto source = core::readTextFile(core::assetDir() / "Programs" / "Examples" / "Basics" / "bell.qasm");
    REQUIRE(source.has_value());

    ui::LayoutState layout;
    {
        auto model = headlessModel();
        app::ProjectHost host(*model);
        CHECK(host.untitled());
        CHECK_FALSE(host.dirty());
        CHECK(host.title() == "Untitled project");

        host.setMainProgram(*source);
        CHECK(host.dirty());
        CHECK(host.title().ends_with("•"));   // spec 19 §2: unsaved changes are visible

        // The shell's layout slice, captured without an ImGui context (spec 19 §4 keeps it pure).
        ui::Shell shell;
        shell.setWorkspace(ui::Workspace::Analysis);
        shell.setPhysicalLab(true);
        shell.setOpen(ui::PanelId::Log, false);
        layout = shell.saveLayout();
        host.storeLayout(layout);
        host.storeCamera(core::Json{{"bookmark", "MXC"}});
        model->session().selectBackend(runtime::BackendChoice::Stabilizer);

        auto st = host.saveAs(file);
        if (!st) FAIL(st.error().format());
        CHECK_FALSE(host.dirty());
        CHECK_FALSE(host.untitled());
        CHECK(host.title() == "bell");
        CHECK(std::filesystem::exists(file));
    }

    // Reopen into a fresh model: the document must restore everything it recorded.
    {
        auto model = headlessModel();
        app::ProjectHost host(*model);
        auto st = host.open(file);
        if (!st) FAIL(st.error().format());
        CHECK(host.project().name == "bell");
        CHECK(host.mainSource() == *source);
        CHECK(host.project().device.id == "sc_fixed_5");
        CHECK(host.project().backend.kind == "stabilizer");
        CHECK(model->session().backendChoice() == runtime::BackendChoice::Stabilizer);
        CHECK(host.camera()["bookmark"] == "MXC");
        CHECK_FALSE(host.recovery().has_value());

        const auto restored = host.layout();
        REQUIRE(restored.has_value());
        CHECK(restored->active == ui::Workspace::Analysis);
        CHECK(restored->physicalLab);
        // The panel the user closed stays closed (spec 19 §4).
        const auto& open = restored->of(ui::Workspace::Analysis).open;
        CHECK(std::find(open.begin(), open.end(), "log") == open.end());
        CHECK(std::find(open.begin(), open.end(), "state_views") != open.end());
        // The documented `workspace` fields describe the active workspace as spec 23 §2 says.
        CHECK(host.project().workspace.preset == "analysis");
        CHECK(host.project().workspace.openPanels.is_array());
    }
}

TEST_CASE("a finished run is recorded beside the project and verifies") {
    const std::filesystem::path dir = scratch("history");
    const std::filesystem::path file = dir / "runs.qxlab";
    auto model = headlessModel();
    app::ProjectHost host(*model);
    if (auto st = host.saveAs(file); !st) FAIL(st.error().format());

    app::Options options;
    options.device = "sc_fixed_5";
    options.shotsGiven = true;
    options.shots = 256;
    const auto source = core::readTextFile(core::assetDir() / "Programs" / "Examples" / "Basics" / "bell.qasm");
    REQUIRE(source.has_value());
    const auto run = app::compileAndRun(model->session(), *source, file, options);
    if (!run) FAIL(run.error().format());

    if (auto st = host.recordRun(run->result, *source); !st) FAIL(st.error().format());
    REQUIRE(host.project().runs.size() == 1);
    const report::RunSummary& summary = host.project().runs.front();
    CHECK(summary.id == "run-000001");
    CHECK(summary.device == "sc_fixed_5");
    CHECK(summary.shots == 256);
    CHECK(summary.seed == run->result.seed);
    CHECK(summary.resultPath == "runs.qxlab.d/runs/run-000001.json");
    CHECK(summary.estimate.contains("wall_time"));

    const std::filesystem::path written = report::projectRunsDir(file) / "run-000001.json";
    REQUIRE(std::filesystem::exists(written));
    // Spec 23 §12: the document and its packed memory must agree on the histogram.
    if (auto st = report::verifyRunResult(written); !st) FAIL(st.error().format());

    // Spec 23 §2: an autosave follows a successful run.
    CHECK(std::filesystem::exists(report::autosavePath(file)));
    host.discardAutosave();
    CHECK_FALSE(std::filesystem::exists(report::autosavePath(file)));
}

TEST_CASE("an autosave newer than the project offers recovery") {
    const std::filesystem::path dir = scratch("recovery");
    const std::filesystem::path file = dir / "crash.qxlab";
    {
        auto model = headlessModel();
        app::ProjectHost host(*model);
        host.setMainProgram("OPENQASM 3.0;\nqubit[1] q;\n");
        if (auto st = host.saveAs(file); !st) FAIL(st.error().format());
        // An edit the user never saved, then the process "crashes".
        host.setMainProgram("OPENQASM 3.0;\nqubit[2] q;\n// recovered\n");
        if (auto st = host.autosaveNow(); !st) FAIL(st.error().format());
        // Make the autosave strictly newer whatever the filesystem's timestamp resolution is.
        std::error_code ec;
        std::filesystem::last_write_time(report::autosavePath(file),
                                         std::filesystem::last_write_time(file, ec) + std::chrono::seconds(2), ec);
    }
    {
        auto model = headlessModel();
        app::ProjectHost host(*model);
        if (auto st = host.open(file); !st) FAIL(st.error().format());
        // Spec 23 §2: the file is loaded and the newer autosave is REPORTED, not applied.
        CHECK(host.mainSource().find("qubit[1]") != std::string::npos);
        REQUIRE(host.recovery().has_value());
        CHECK(host.recovery()->autosavePending);
        CHECK_FALSE(host.recovery()->autosaveTime.empty());

        if (auto st = host.acceptRecovery(); !st) FAIL(st.error().format());
        CHECK(host.mainSource().find("recovered") != std::string::npos);
        CHECK(host.dirty());              // the recovered state is not yet in the project file
        CHECK_FALSE(host.recovery().has_value());
    }
    {
        // Declining instead drops the autosave.
        auto model = headlessModel();
        app::ProjectHost host(*model);
        if (auto st = host.open(file); !st) FAIL(st.error().format());
        REQUIRE(host.recovery().has_value());
        host.declineRecovery();
        CHECK_FALSE(std::filesystem::exists(report::autosavePath(file)));
    }
}

TEST_CASE("the autosave timer fires on the spec 23 §2 interval") {
    const std::filesystem::path dir = scratch("timer");
    const std::filesystem::path file = dir / "timer.qxlab";
    auto model = headlessModel();
    app::ProjectHost host(*model);
    if (auto st = host.saveAs(file); !st) FAIL(st.error().format());
    host.setMainProgram("OPENQASM 3.0;\n");
    const std::filesystem::path autosave = report::autosavePath(file);
    std::error_code ec;
    std::filesystem::remove(autosave, ec);

    host.tick(30.0);
    CHECK_FALSE(std::filesystem::exists(autosave));   // half the interval: nothing written yet
    host.tick(31.0);
    CHECK(std::filesystem::exists(autosave));

    // A clean project writes nothing, however long it sits there.
    std::filesystem::remove(autosave, ec);
    if (auto st = host.save(); !st) FAIL(st.error().format());
    host.tick(120.0);
    CHECK_FALSE(std::filesystem::exists(autosave));
}

TEST_CASE("an untitled project journals to the user recovery directory") {
    auto model = headlessModel();
    app::ProjectHost host(*model);
    host.setMainProgram("OPENQASM 3.0;\n// untitled\n");
    if (auto st = host.autosaveNow(); !st) FAIL(st.error().format());
    // Spec 23 §2: `<userData>/recovery/<id>.qxlab`; the id is this session's.
    std::error_code ec;
    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator(report::recoveryDir(), ec))
        if (entry.path().extension() == ".qxlab") {
            if (auto p = report::loadProject(entry.path()); p && p->mainProgram() != nullptr &&
                                                            p->mainProgram()->inlineSource.find("untitled") !=
                                                                std::string::npos)
                found = true;
        }
    CHECK(found);
    host.discardAutosave();
}
