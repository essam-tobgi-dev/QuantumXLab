// Spec 23 §2, §12 — the `.qxlab` project file: round trip, unknown-field preservation, version
// refusal, atomic writes, autosave and crash recovery.
#include "ReportTestUtil.hpp"

using namespace qlab;
using namespace qlab::report;

namespace {

Project sample() {
    Project p;
    p.name = "Grover 4-qubit on heavy-hex";
    ProgramRef main;
    main.inlineSource = "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit[2] q;\nh q[0];\n";
    main.isMain = true;
    p.programs.push_back(main);
    ProgramRef linked;
    linked.path = "oracles/marked.qasm";
    p.programs.push_back(linked);
    p.device.id = "sc_heavyhex_27";
    p.device.calibrationOverrides["qubit[3].T1_us"] = 42.0;
    p.backend.kind = "density_matrix";
    p.backend.shots = 4000;
    p.backend.seed = 20250916ull;
    p.backend.noise = true;
    p.backend.snapshotEveryGate = true;
    p.backend.lindblad = {{"levels", 3}, {"dt_ps", 50}};
    p.compiler.optimizationLevel = 2;
    p.compiler.layout = "sabre";
    p.compiler.scheduling = "alap";
    p.workspace.preset = "lab";
    p.workspace.imguiLayout = "[Window][Dockspace]\nPos=0,0\n";
    p.workspace.camera = {{"eye", {1.0, 2.0, 3.0}}, {"fov_deg", 45.0}};
    p.workspace.openPanels = {"Program", "Results", "Lab"};
    RunSummary run;
    run.time = "2026-09-18T10:11:12Z";
    run.backend = "state_vector";
    run.device = "sc_heavyhex_27";
    run.shots = 1000;
    run.seed = 7;
    run.estimate = {{"wall_s", 4.21}, {"fidelity", 0.91}};
    p.addRun(run);
    RecipeRef recipe;
    recipe.id = "t1";
    recipe.qubit = 3;
    recipe.lastResultPath = "runs/t1-000003.json";
    p.recipes.push_back(recipe);
    return p;
}

} // namespace

TEST_CASE("project: save -> load -> save is byte-identical apart from the envelope timestamp") {
    rtest::Sandbox box("project_roundtrip");
    const std::filesystem::path file = box / "grover.qxlab";
    const Project p = sample();
    REQUIRE(saveProject(file, p).has_value());

    auto loaded = loadProject(file);
    INFO((loaded ? std::string() : loaded.error().format()));
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == p.name);
    REQUIRE(loaded->programs.size() == 2);
    CHECK(loaded->programs[0].inlineSource == p.programs[0].inlineSource);
    CHECK(loaded->programs[0].isMain);
    CHECK(loaded->programs[1].path == "oracles/marked.qasm");
    CHECK(loaded->device.id == "sc_heavyhex_27");
    CHECK(loaded->device.calibrationOverrides["qubit[3].T1_us"].get<double>() == 42.0);
    CHECK(loaded->backend.shots == 4000);
    CHECK(loaded->backend.seed == 20250916ull); // §2: the seed reproduces the run
    CHECK(loaded->backend.lindblad["levels"].get<int>() == 3);
    CHECK(loaded->compiler.optimizationLevel == 2);
    CHECK(loaded->workspace.imguiLayout == p.workspace.imguiLayout);
    CHECK(loaded->workspace.openPanels.size() == 3);
    REQUIRE(loaded->runs.size() == 1);
    CHECK(loaded->runs[0].id == "run-000001");
    CHECK(loaded->runs[0].resultPath == "runs/run-000001.json");
    CHECK(loaded->runs[0].estimate["fidelity"].get<double>() == 0.91);
    REQUIRE(loaded->recipes.size() == 1);
    CHECK(loaded->recipes[0].qubit == 3u);

    // The `data` object must be reproduced exactly; only `qxl.created` may differ (spec 23 §12).
    const core::Json first = core::Json::parse(rtest::readFile(file));
    REQUIRE(saveProject(file, *loaded).has_value());
    const core::Json second = core::Json::parse(rtest::readFile(file));
    CHECK(first["data"] == second["data"]);
    CHECK(first["qxl"]["kind"] == "project");
    CHECK(first["qxl"]["schema"].get<int>() == 1);
    CHECK(first["qxl"]["app"] == second["qxl"]["app"]);
}

TEST_CASE("project: unknown fields survive load and save (spec 23 §1, §12)") {
    rtest::Sandbox box("project_unknown");
    const std::filesystem::path file = box / "future.qxlab";
    Project p = sample();
    REQUIRE(saveProject(file, p).has_value());

    // Inject fields a newer application might write, at the top level and inside three records.
    core::Json doc = core::Json::parse(rtest::readFile(file));
    doc["data"]["future"] = 1;
    doc["data"]["holodeck"] = {{"enabled", true}};
    doc["data"]["device"]["crosstalkOverrides"] = {{"q0-q1", 0.5}};
    doc["data"]["backend"]["trajectories"] = 128;
    doc["data"]["programs"][0]["bookmarks"] = {3, 7};
    doc["data"]["runs"][0]["tags"] = {"nightly"};
    REQUIRE(core::writeTextFileAtomic(file, doc.dump(2)).has_value());

    auto loaded = loadProject(file);
    REQUIRE(loaded.has_value());
    CHECK(loaded->extra["future"].get<int>() == 1);
    CHECK(loaded->device.extra["crosstalkOverrides"]["q0-q1"].get<double>() == 0.5);
    CHECK(loaded->backend.extra["trajectories"].get<int>() == 128);
    CHECK(loaded->programs[0].extra["bookmarks"][1].get<int>() == 7);

    REQUIRE(saveProject(file, *loaded).has_value());
    const core::Json again = core::Json::parse(rtest::readFile(file));
    CHECK(again["data"]["future"].get<int>() == 1);
    CHECK(again["data"]["holodeck"]["enabled"].get<bool>() == true);
    CHECK(again["data"]["device"]["crosstalkOverrides"]["q0-q1"].get<double>() == 0.5);
    CHECK(again["data"]["backend"]["trajectories"].get<int>() == 128);
    CHECK(again["data"]["programs"][0]["bookmarks"] == core::Json({3, 7}));
    CHECK(again["data"]["runs"][0]["tags"][0] == "nightly");
    // The known fields are still there and still win over anything in `extra`.
    CHECK(again["data"]["device"]["id"] == "sc_heavyhex_27");
    CHECK(again["data"]["backend"]["shots"].get<int>() == 4000);
}

TEST_CASE("project: a schema newer than this application is refused by name (spec 23 §1, §12)") {
    rtest::Sandbox box("project_version");
    const std::filesystem::path file = box / "newer.qxlab";
    core::Json doc;
    doc["qxl"] = {
        {"kind", "project"}, {"schema", 99}, {"app", "9.9.9"}, {"created", "2030-01-01T00:00:00Z"}};
    doc["data"] = {{"name", "from the future"}};
    REQUIRE(core::writeTextFileAtomic(file, doc.dump(2)).has_value());

    auto loaded = loadProject(file);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == ErrorCode::Unsupported);
    const std::string message = loaded.error().format();
    INFO(message);
    CHECK(message.find("99") != std::string::npos);    // the version it refuses
    CHECK(message.find("9.9.9") != std::string::npos); // the application that wrote it
    CHECK(message.find("project") != std::string::npos);

    // A file of the wrong kind is refused too, naming both kinds.
    doc["qxl"]["kind"] = "device";
    doc["qxl"]["schema"] = 1;
    REQUIRE(core::writeTextFileAtomic(file, doc.dump(2)).has_value());
    auto wrong = loadProject(file);
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().message.find("device") != std::string::npos);
}

TEST_CASE("project: autosave is offered on open and used only when the caller accepts it") {
    rtest::Sandbox box("project_autosave");
    const std::filesystem::path file = box / "lab.qxlab";
    Project p = sample();
    REQUIRE(saveProject(file, p).has_value());
    CHECK(autosavePath(file) == box / "lab.qxlab.autosave");
    CHECK(projectRunsDir(file) == box / "lab.qxlab.d" / "runs");
    CHECK_FALSE(std::filesystem::exists(autosavePath(file)));

    // Work done after the last save lands in the autosave, which is then newer than the project.
    p.name = "unsaved work";
    p.backend.shots = 8192;
    REQUIRE(writeAutosave(file, p).has_value());
    REQUIRE(std::filesystem::exists(autosavePath(file)));
    const auto fileTime = std::filesystem::last_write_time(file);
    std::filesystem::last_write_time(autosavePath(file), fileTime + std::chrono::seconds(30));

    auto asked = openProject(file, RecoveryChoice::Ask);
    REQUIRE(asked.has_value());
    CHECK(asked->autosavePending); // the caller prompts
    CHECK_FALSE(asked->recovered);
    CHECK(asked->project.name == "Grover 4-qubit on heavy-hex");
    CHECK_FALSE(asked->autosaveTime.empty());
    CHECK_FALSE(asked->fileTime.empty());

    auto recovered = openProject(file, RecoveryChoice::UseAutosave);
    REQUIRE(recovered.has_value());
    CHECK(recovered->recovered);
    CHECK(recovered->project.name == "unsaved work");
    CHECK(recovered->project.backend.shots == 8192);

    auto discarded = openProject(file, RecoveryChoice::DiscardAutosave);
    REQUIRE(discarded.has_value());
    CHECK_FALSE(discarded->recovered);
    CHECK_FALSE(std::filesystem::exists(autosavePath(file)));
    CHECK(discarded->project.name == "Grover 4-qubit on heavy-hex");

    // A save drops a stale autosave; a clean exit does the same through `clearAutosave`.
    REQUIRE(writeAutosave(file, p).has_value());
    REQUIRE(saveProject(file, p).has_value());
    CHECK_FALSE(std::filesystem::exists(autosavePath(file)));
    CHECK(clearAutosave(file).has_value()); // idempotent
}

TEST_CASE("project: an untitled project journals under the user data directory (spec 23 §2)") {
    const std::filesystem::path p = recoveryPath("8e1f-uuid");
    CHECK(p.parent_path() == recoveryDir());
    CHECK(p.filename() == "8e1f-uuid.qxlab");
    CHECK(recoveryDir().parent_path() == core::userDataDir());

    // The journal itself is written into a sandbox: the same code path, a different root.
    rtest::Sandbox box("project_journal");
    const std::filesystem::path journal = box / "8e1f-uuid.qxlab";
    Project untitled;
    untitled.name = "untitled";
    REQUIRE(saveProject(journal, untitled).has_value());
    auto back = loadProject(journal);
    REQUIRE(back.has_value());
    CHECK(back->name == "untitled");
}

TEST_CASE("project: a save is atomic and leaves no temporary behind") {
    rtest::Sandbox box("project_atomic");
    const std::filesystem::path file = box / "atomic.qxlab";
    Project p = sample();
    REQUIRE(saveProject(file, p).has_value());
    CHECK(std::filesystem::exists(file));
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    CHECK_FALSE(std::filesystem::exists(tmp));

    // A non-finite number never reaches the file (spec 23 §1).
    p.device.calibrationOverrides["qubit[0].T1_us"] = std::numeric_limits<double>::infinity();
    auto bad = saveProject(file, p);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::InvalidArgument);
    auto still = loadProject(file);
    REQUIRE(still.has_value());
    CHECK(still->device.calibrationOverrides.contains("qubit[3].T1_us"));
    CHECK_FALSE(still->device.calibrationOverrides.contains("qubit[0].T1_us"));
}

TEST_CASE("project: paths inside a file are relative to it, with forward slashes (spec 23 §1)") {
    const std::filesystem::path base = "/lab/projects";
    CHECK(relativePathString(base / "runs" / "run-000001.json", base) == "runs/run-000001.json");
    CHECK(relativePathString(base / "a" / "b" / "c.qasm", base) == "a/b/c.qasm");
    CHECK(relativePathString("/lab/shared/lib.inc", base) == "../shared/lib.inc");
    CHECK(relativePathString(base, base) == ".");
    CHECK(kAutosaveInterval == std::chrono::seconds{60}); // spec 23 §2 autosave cadence
}

TEST_CASE("project: run ids continue the existing numbering") {
    Project p;
    CHECK(p.addRun({}) == "run-000001");
    CHECK(p.addRun({}) == "run-000002");
    RunSummary named;
    named.id = "run-000017";
    p.addRun(named);
    CHECK(p.addRun({}) == "run-000018");
    CHECK(p.run("run-000017") != nullptr);
    CHECK(p.run("run-000042") == nullptr);
    CHECK(p.mainProgram() == nullptr);

    // An id that is not a number this application could have written never derails the numbering
    // (and never throws: the whole module returns `Result`, spec 04 §2).
    RunSummary strange;
    strange.id = "run-99999999999999999999";
    p.addRun(strange);
    RunSummary named2;
    named2.id = "run-sweep";
    p.addRun(named2);
    CHECK(p.addRun({}) == "run-000019");
}

TEST_CASE("project: a malformed field falls back to its default instead of wrapping") {
    core::Json doc;
    doc["qxl"] = {
        {"kind", "project"}, {"schema", 1}, {"app", "0.1.0"}, {"created", "2026-09-18T00:00:00Z"}};
    doc["data"] = {{"name", 42},                                   // wrong type
                   {"backend", {{"shots", -1}, {"seed", "many"}}}, // negative / wrong type
                   {"programs", core::Json::array({"not an object"})}};
    auto p = parseProject(doc.dump());
    REQUIRE(p.has_value());
    CHECK(p->name.empty());
    CHECK(p->backend.shots == 1024u); // the default, not 2^32 - 1
    CHECK(p->backend.seed == 0u);
    CHECK(p->programs.empty());

    // A `programs` field that is not an array is an error naming the path (spec 23 §1).
    doc["data"]["programs"] = 7;
    auto bad = parseProject(doc.dump());
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("data.programs") != std::string::npos);
}
