// Spec 02 §5–§6, 03 §5, 21 §1.1 — the composition root in motion: the shared selection, the
// 10 Hz fridge, the off-thread reductions, one whole application frame, and `--selftest`.
// The GL parts SKIP when no context can be created (CI without a display).
#include "App/App.hpp"
#include "Core/Paths.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <sstream>

using namespace qlab;
using Catch::Approx;

namespace {

std::filesystem::path scratchDir(std::string_view name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "qxl_app_test" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string bellSource() {
    const auto text = core::readTextFile(core::assetDir() / "Programs" / "Examples" / "Basics" / "bell.qasm");
    if (!text) FAIL("bell.qasm is missing");
    return *text;
}

std::unique_ptr<app::LabModel> labModel() {
    app::LabModel::Config config;
    config.device = "sc_fixed_5";
    auto model = app::LabModel::create(config);
    if (!model) FAIL(model.error().format());
    return std::move(*model);
}

// True when the image has more than one distinct colour: something was actually drawn.
bool hasContent(const report::Image& image) {
    if (image.empty()) return false;
    const report::Rgba first = image.get(0, 0);
    for (int y = 0; y < image.height; y += 7)
        for (int x = 0; x < image.width; x += 7)
            if (image.get(x, y) != first) return true;
    return false;
}

} // namespace

TEST_CASE("one selection model ties the state views, the laboratory and the inspector together") {
    // Spec 21 §1.1: clicking a qubit anywhere selects it everywhere, through `Scene::qubitNodes()`.
    auto model = labModel();
    REQUIRE(model->scene() != nullptr);
    const std::span<const ComponentId> pads = model->scene()->qubitNodes();
    REQUIRE(pads.size() >= 2);

    model->selection().selectQubit(QubitIndex{1});
    CHECK(model->selection().component() == pads[1]);
    CHECK(model->selection().primaryQubit() == QubitIndex{1});

    model->selection().selectComponent(pads[0]);
    REQUIRE(model->selection().primaryQubit().has_value());
    CHECK(*model->selection().primaryQubit() == QubitIndex{0});

    // Selecting something that is not a qubit pad (the room) moves the component selection and
    // leaves the qubit selection alone: the Inspector follows the click, the state views do not
    // forget which qubit the user is looking at.
    model->selection().selectComponent(model->scene()->root());
    CHECK(model->selection().component() == model->scene()->root());
    REQUIRE(model->selection().primaryQubit().has_value());
    CHECK(*model->selection().primaryQubit() == QubitIndex{0});
}

TEST_CASE("an ion device opens its own laboratory") {
    // Spec 17 §3.6: an ion chain selects `ion_lab_11` — a vacuum chamber and laser paths, no
    // dilution refrigerator. Its `wiring.json` names optical chains `cryo::ChainCatalog` does not
    // define (SPEC_DEVIATIONS 79); the App must open the laboratory anyway.
    app::LabModel::Config config;
    config.device = "ion_chain_11";
    auto made = app::LabModel::create(config);
    if (!made) FAIL(made.error().format());
    const std::unique_ptr<app::LabModel>& model = *made;
    CHECK(model->layoutId() == "ion_lab_11");
    REQUIRE(model->scene() != nullptr);
    CHECK(model->scene()->size() > 20);   // the room, the optical table, the chamber, the racks
    CHECK(model->scene()->layout().chamberPosition_m.has_value());
    CHECK_FALSE(model->scene()->layout().hasFridge());
    CHECK(model->cryo().wiring.lines.empty());   // no coax chain applies to a trap
    model->tick(0.1);
    CHECK(model->state().environment != nullptr);
}

TEST_CASE("tick advances the fridge at 10 Hz and never faster than the lab clock") {
    auto model = labModel();
    const double t0 = model->cryo().snapshot.time_s;

    // Spec 11 §8: the network is stepped in whole 0.1 s steps, whatever the frame rate. Five
    // sixtieths of a second is less than one step, so nothing moves yet.
    for (int i = 0; i < 5; ++i) model->tick(1.0 / 60.0);
    CHECK(model->labClock() == Approx(5.0 / 60.0).epsilon(1e-9));
    CHECK(model->cryo().snapshot.time_s == Approx(t0).epsilon(1e-12));
    // The remainder carries across frames: 5/60 + 0.25 = 0.3333 s of lab time is three whole
    // steps, and the fridge clock never runs ahead of the lab clock.
    model->tick(0.25);
    CHECK(model->labClock() == Approx(5.0 / 60.0 + 0.25).epsilon(1e-9));
    CHECK(model->cryo().snapshot.time_s == Approx(t0 + 0.3).epsilon(1e-9));
    CHECK(model->cryo().snapshot.time_s - t0 <= model->labClock() + 1e-9);

    // A stalled frame does not replay minutes of fridge time (the catch-up is capped at 1 s).
    const double before = model->cryo().snapshot.time_s;
    model->tick(600.0);
    CHECK(model->cryo().snapshot.time_s - before <= 1.0 + 1e-9);
    // The mixing chamber stays at base: the load has not changed.
    CHECK(model->cryo().snapshot.T_K[static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::MXC))] < 0.05);

    // Spec 22 §1: a frame that stepped the fridge records the stage temperatures, so the Plots
    // panel has data. A frame too short to step it records nothing.
    const auto channel = model->recorder().find("cryo.MXC.T");
    REQUIRE(channel.has_value());
    const std::size_t before2 = model->recorder().size(*channel);
    model->tick(1.0 / 600.0);
    CHECK(model->recorder().size(*channel) == before2);
    for (int i = 0; i < 6; ++i) model->tick(0.1);
    CHECK(model->recorder().size(*channel) == before2 + 6);

    const data::Series series = model->recorder().view(*channel);
    CHECK(series.size() >= 6);
    CHECK(series.desc.unit == "K");
    REQUIRE(series.y.size() == 1);
    CHECK(series.y.front().back() < 0.05);
    for (std::size_t k = 1; k < series.x.size(); ++k) CHECK(series.x[k] > series.x[k - 1]);
}

TEST_CASE("reductions are computed on the job system and land in the next ViewInput") {
    auto model = labModel();
    app::Options options;
    options.device = "sc_fixed_5";
    options.ideal = true;
    options.shotsGiven = true;
    options.shots = 32;
    const auto run = app::compileAndRun(model->session(), bellSource(), "bell.qasm", options);
    if (!run) FAIL(run.error().format());
    model->setResult(std::make_shared<const runtime::RunResult>(run->result));
    model->tick(1.0 / 60.0);

    // Spec 21 §1: the snapshot arrives first, the reductions follow — a view is "stale" between.
    REQUIRE(model->viewInput().snapshot != nullptr);
    CHECK(model->viewInput().reductions == nullptr);

    viz::ReductionRequest request;
    request.singles = true;
    request.pairs = true;
    model->requestReductions(request);
    model->waitReductions();
    model->tick(1.0 / 60.0);
    REQUIRE(model->viewInput().reductions != nullptr);
    const viz::Reductions& r = *model->viewInput().reductions;
    REQUIRE(r.singles.size() == 2);
    // A Bell pair: each qubit is maximally mixed (purity ½, one bit of entropy) and the pair
    // carries two bits of mutual information (T01 §5).
    for (const viz::SingleReduction& s : r.singles) {
        CHECK(s.purity == Approx(0.5).epsilon(1e-9));
        CHECK(s.entropyBits == Approx(1.0).epsilon(1e-9));
        CHECK(s.bloch.norm() < 1e-9);
    }
    REQUIRE(r.pairs.size() == 1);
    CHECK(r.pairs.front().measures.mutualInformation == Approx(2.0).epsilon(1e-9));

    // The reductions belong to the snapshot they were computed for.
    CHECK(r.gateIndex == model->viewInput().snapshot->gateIndex);
    CHECK_FALSE(model->viewInput().reductionsStale());
    CHECK_FALSE(model->reductionsInFlight());
}

TEST_CASE("the ViewInput carries the run, the program and the device") {
    auto model = labModel();
    app::Options options;
    options.device = "sc_fixed_5";
    options.shotsGiven = true;
    options.shots = 128;
    const auto run = app::compileAndRun(model->session(), bellSource(), "bell.qasm", options);
    if (!run) FAIL(run.error().format());
    model->setCompiled(std::make_shared<const compiler::CompiledProgram>(*model->session().compiled(run->compile)));
    model->setResult(std::make_shared<const runtime::RunResult>(run->result));
    model->tick(1.0 / 60.0);

    const viz::ViewInput& in = model->viewInput();
    REQUIRE(in.counts != nullptr);
    CHECK(in.counts->total() == 128);
    CHECK(in.device != nullptr);
    CHECK(in.calibration != nullptr);
    CHECK(in.qubitCount() == 2);
    // Spec 19 §3 "Circuit Diagram": the source and the compiled stage are both available.
    REQUIRE(in.circuits.at(viz::CircuitStage::Source) != nullptr);
    REQUIRE(in.circuits.latest() != nullptr);
    CHECK(in.circuits.layout.size() == 2);
    REQUIRE(in.measurement != nullptr);
    CHECK(in.measurement->bitOfQubit.size() == 2);
    CHECK(in.measurement->measures(0, 'Z'));
    CHECK(in.hasPlayhead);

    // Every open view accepts the input and asks for what it needs without touching a backend.
    viz::ReductionRequest merged;
    for (const std::unique_ptr<viz::IStateView>& v : model->views()) {
        REQUIRE(v != nullptr);
        merged.merge(v->wants(in));
        v->update(in);
    }
    CHECK_FALSE(merged.empty());
}

TEST_CASE("the instruments see the run and the fridge the App publishes") {
    // Spec 12 §11: Instruments does not depend on Runtime — this is the hand-off.
    auto model = labModel();
    app::Options options;
    options.device = "sc_fixed_5";
    options.shotsGiven = true;
    options.shots = 16;
    const auto run = app::compileAndRun(model->session(), bellSource(), "bell.qasm", options);
    // (a calibrated run: the per-shot model projects its terminal measurement, which is exactly
    //  why the playhead opens before it — see `openingGate`)
    if (!run) FAIL(run.error().format());
    model->setResult(std::make_shared<const runtime::RunResult>(run->result));
    model->tick(0.1);
    model->live().waitIdle();
    model->tick(0.1);

    const std::shared_ptr<const instr::RunView> view = model->instruments().inputs()->run();
    REQUIRE(view != nullptr);
    CHECK(view->nQubits == 2);
    CHECK(view->state != nullptr);
    const std::shared_ptr<const instr::Environment> env = model->instruments().inputs()->environment();
    REQUIRE(env != nullptr);
    REQUIRE(env->device != nullptr);
    CHECK(env->device->id == "sc_fixed_5");
    REQUIRE(env->wiring != nullptr);
    CHECK(env->wiring->lines.size() > 4);
    CHECK(env->labTimeS > 0.0);
    CHECK(env->stageTemperatures()[static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::MXC))] < 0.05);

    // The simulator-only state probe answers from that RunView alone. The playhead sits on the
    // prepared Bell state, whose single-qubit reduction is (nearly) maximally mixed: |r| ≈ 0 and
    // the purity is ≈ ½, not 1 (which is what a collapsed outcome would give).
    const auto bloch = model->instruments().query("probe.state.qubit[0].bloch[2]");
    REQUIRE(bloch.has_value());
    CHECK(std::abs(*bloch) < 0.1);
    const auto purity = model->instruments().query("probe.state.qubit[0].purity");
    REQUIRE(purity.has_value());
    CHECK(*purity < 0.6);
    CHECK(*purity > 0.4);
    // …and is hidden in Physical-lab mode (spec 12 §12).
    CHECK_FALSE(model->instruments().query("probe.state.qubit[0].bloch[2]", true).has_value());
}

TEST_CASE("the application draws a frame, runs a program and records it") {
    app::Options options;
    options.device = "sc_fixed_5";
    auto app = app::Application::create(options, false);
    if (!app) SKIP("no GL context available: " + app.error().message);
    app::Application& a = **app;

    a.frame(1.0 / 60.0);
    CHECK(a.model().scene() != nullptr);
    CHECK(a.model().views().size() > 8);
    CHECK_FALSE(a.programSource().empty());   // the application opens on bell.qasm

    a.setProgramSource(bellSource());
    a.requestRun();
    for (int i = 0; i < 900 && a.model().result() == nullptr; ++i) a.frame(1.0 / 60.0);
    REQUIRE(a.model().result() != nullptr);
    const runtime::RunResult& r = *a.model().result();
    CHECK(r.counts.total() == 1024);          // `pragma qlab.shots 1024` of the example
    CHECK(r.counts.probability("00") + r.counts.probability("11") > 0.85);

    // Spec 23 §2: the run went into the project history.
    REQUIRE(a.projects().project().runs.size() == 1);
    CHECK(a.projects().project().runs.front().device == "sc_fixed_5");

    // Every workspace draws without a model gap.
    for (std::size_t w = 0; w < ui::kWorkspaceCount; ++w) a.showWorkspace(static_cast<ui::Workspace>(w), 2);

    // Spec 17 §8: with the reductions in, the Bloch markers above the transmon pads are live.
    a.showWorkspace(ui::Workspace::Analysis, 3);
    a.model().waitReductions();
    a.frame(1.0 / 60.0);
    REQUIRE(a.model().overlays() != nullptr);
    // Spec 17 §8: the stage-temperature tint is opt-in (the metals keep their finish by default).
    CHECK(a.model().overlays()->stageTemperatures().empty());
    CHECK_FALSE(a.model().overlays()->temperatureTint());
    a.model().overlays()->setTemperatureTint(true);
    a.frame(1.0 / 60.0);
    CHECK_FALSE(a.model().overlays()->stageTemperatures().empty());
    CHECK_FALSE(a.model().overlays()->blochMarkers().empty());

    // Physical-lab mode removes the Simulator-only overlay (spec 00 §6).
    a.shell().setPhysicalLab(true);
    a.frame(1.0 / 60.0);
    a.frame(1.0 / 60.0);
    CHECK(a.model().overlays()->blochMarkers().empty());
    CHECK_FALSE(a.model().overlays()->stageTemperatures().empty());

    a.projects().discardAutosave();
}

// The name must not start with `--`: `catch_discover_tests` passes it to Catch2 as the first
// argument, where a leading double dash is an option token ("Unrecognised token: --selftest") and
// CTest could never run this case, however green the binary looked when run without a filter.
TEST_CASE("the selftest mode writes a PNG for every workspace and every bookmark") {
    app::Options options;
    options.device = "sc_fixed_5";
    options.layout = "sc_lab_standard";
    options.outDir = scratchDir("selftest");
    std::ostringstream log;
    const auto report = app::runSelfTest(options, log);
    if (!report) FAIL(report.error().format());

    // The example oracle runs with or without a display, and it runs over the WHOLE corpus: a
    // shortlist of three hid the examples that did not fit the default device (see ExampleTest).
    REQUIRE(report->examples.size() >= 25);
    for (const app::ExampleCheck& e : report->examples) {
        INFO(e.name << " on '" << e.device << "': " << e.detail);
        if (e.skipped) {       // a fit or a reconstruction: no fixed distribution to compare
            CHECK_FALSE(e.ran);
            continue;
        }
        CHECK(e.ran);
        CHECK(e.passed);
    }
    if (!report->renderedScenes) SKIP("no GL context available for the renders");
    CHECK(report->ok());

    for (const char* name : {"lab", "program", "analysis"}) {
        const std::filesystem::path file = options.outDir / (std::string("workspace_") + name + ".png");
        INFO(file.string());
        REQUIRE(std::filesystem::exists(file));
        const auto image = report::readPng(file);
        if (!image) FAIL(image.error().format());
        CHECK(image->width > 200);
        CHECK(image->height > 200);
        CHECK(hasContent(*image));
    }
    // One per bookmark of the shipped layout (Overview, Fridge, MXC, Chip, Rack, GHS).
    for (const char* name : {"overview", "fridge", "mxc", "chip", "rack", "ghs"}) {
        const std::filesystem::path file = options.outDir / (std::string("bookmark_") + name + ".png");
        INFO(file.string());
        REQUIRE(std::filesystem::exists(file));
        const auto image = report::readPng(file);
        if (!image) FAIL(image.error().format());
        CHECK(image->width == 1280);
        // Spec 23 §8: the annotation strip is baked under the captured viewport.
        CHECK(image->height > 800);
        CHECK(hasContent(*image));
    }
    CHECK(report->screenshots.size() == 10);   // 3 workspaces, the tour card, 6 bookmarks
    CHECK(log.str().find("example bell") != std::string::npos);
}
