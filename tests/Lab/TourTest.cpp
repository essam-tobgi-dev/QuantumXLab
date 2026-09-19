// Spec 17 §7.10 — the guided tour, headless: the scripts load and resolve every stop and theory
// anchor, a 60 Hz play-through visits every stop with the focus framed, stop() restores the view
// exactly, camera input pauses, Physical-lab skipping, and refusal of a broken script.
#include "Lab/Tour.hpp"
#include "Core/Paths.hpp"
#include "Lab/Lab.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <set>

using namespace qlab;
using namespace qlab::lab;

namespace {
Scene build(const char* layout) {
    auto s = buildScene(layout);
    if (!s)
        FAIL(s.error().format());
    return std::move(*s);
}
std::filesystem::path tourFile(const std::string& id) {
    return core::assetDir() / "Lab" / "Tours" / (id + ".json");
}
std::filesystem::path theoryDir() {
    return std::filesystem::path(QXL_SOURCE_DIR) / "docs" / "theory";
}

Tour loadTour(const char* id, const Scene& scene, Interaction& ui) {
    auto t = Tour::load(tourFile(id), scene, ui, nullptr, theoryDir());
    if (!t)
        FAIL(t.error().format());
    return std::move(*t);
}

// The AABB the step frames (spec 18 §6): the focus node's subtree, or its own geometry.
std::optional<gfx::Aabb> focusBox(const Scene& scene, const Tour& tour) {
    const Node* n = scene.node(tour.focusId());
    if (n == nullptr)
        return std::nullopt;
    const gfx::Aabb box = n->subtreeBounds.valid() ? n->subtreeBounds : n->worldBounds;
    return box.valid() ? std::optional<gfx::Aabb>{box} : std::nullopt;
}

constexpr int kW = 1600, kH = 900;
constexpr double kDt = 1.0 / 60.0;
} // namespace

TEST_CASE("tour: the superconducting script loads, resolves every stop and every theory anchor") {
    Scene scene = build("sc_lab_standard");
    Interaction ui(scene);
    const Tour tour = loadTour("sc_lab_standard", scene, ui);
    CHECK(tour.id() == "sc_lab_standard");
    CHECK(tour.size() >= 24); // the brief's minimum

    const auto anchors = theoryAnchorsIn(theoryDir());
    REQUIRE(anchors.size() >= 10);
    std::set<std::string> ids;
    for (std::size_t k = 0; k < tour.size(); ++k) {
        const TourStep& s = tour.steps()[k];
        INFO("step " << k << " '" << s.id << "'");
        CHECK(ids.insert(s.id).second);
        CHECK_FALSE(s.title.empty());
        // 3–6 sentences, physically exact: at least three full stops and a number somewhere.
        CHECK(std::count(s.narration.begin(), s.narration.end(), '.') >= 3);
        CHECK(s.narration.find_first_of("0123456789") != std::string::npos);
        if (!s.focusInstance.empty() || !s.focusDescriptor.empty()) {
            const ComponentId id = tour.focusId(k);
            REQUIRE(id.value != 0);
            const Node* n = scene.node(id);
            REQUIRE(n != nullptr);
            if (!s.focusInstance.empty())
                CHECK(n->instanceName == s.focusInstance);
        }
        if (s.bookmark)
            CHECK(ui.bookmark(*s.bookmark) != nullptr);
        const std::size_t hash = s.theory.find('#');
        REQUIRE(hash == 3);
        const auto doc = anchors.find(s.theory.substr(0, hash));
        REQUIRE(doc != anchors.end());
        CHECK(doc->second.contains(s.theory.substr(hash + 1)));
        for (const std::string& h : s.highlight)
            CHECK(scene.findByInstance(h).value != 0);
        CHECK(s.dwell_s > 0.0);
        CHECK(s.marginFactor > 0.0);
    }
    // Signal order: room → racks → fridge → stages → chip → qubit → readout → digitizer → closing.
    const auto index = [&](std::string_view id) {
        for (std::size_t k = 0; k < tour.size(); ++k)
            if (tour.steps()[k].id == id)
                return k;
        FAIL("missing step " << id);
        return std::size_t{0};
    };
    CHECK(index("room") < index("racks"));
    CHECK(index("racks") < index("fridge_outside"));
    CHECK(index("fridge_outside") < index("open"));
    CHECK(index("open") < index("stage_4K"));
    CHECK(index("stage_4K") < index("still"));
    CHECK(index("still") < index("mxc"));
    CHECK(index("mxc") < index("drive_line"));
    CHECK(index("drive_line") < index("isolators"));
    CHECK(index("isolators") < index("hemt"));
    CHECK(index("hemt") < index("transmon"));
    CHECK(index("transmon") < index("resonator"));
    CHECK(index("resonator") < index("digitizer"));
    CHECK(index("digitizer") < index("software"));
    CHECK(index("software") < index("closing"));
    CHECK(index("closing") == tour.size() - 1);
}

TEST_CASE("tour: a 60 Hz play-through visits every stop, frames each focus and restores the view") {
    Scene scene = build("sc_lab_standard");
    Interaction ui(scene);
    Tour tour = loadTour("sc_lab_standard", scene, ui);
    gfx::Camera cam;
    cam.setAspect(static_cast<double>(kW) / kH);
    cam.set(ui.bookmark("Overview")->view);

    ui.setXray(true); // something to restore
    ui.setLayerVisible(Group::Rack, false);
    const core::Json before = ui.saveViewState();
    const ViewState v0 = ui.view();

    CHECK_FALSE(tour.playing());
    tour.play();
    REQUIRE(tour.playing());
    CHECK(tour.step() == 0);

    double expected = 0.0;
    for (const TourStep& s : tour.steps())
        expected += Tour::kFlight_s + s.dwell_s;
    std::set<std::size_t> visited;
    std::size_t framedChecks = 0;
    int frames = 0;
    std::optional<std::size_t> lastDwelling;
    while (tour.playing() && frames < 60 * 3600) {
        tour.update(kDt, cam);
        ++frames;
        if (tour.phase() == Tour::Phase::Dwelling && lastDwelling != tour.step()) {
            // First dwell frame of a stop: the flight has just landed, the focus is framed.
            lastDwelling = tour.step();
            visited.insert(tour.step());
            INFO("step " << tour.step() << " '" << tour.current().id << "'");
            CHECK(tour.stepProgress() > 0.0);
            CHECK(tour.stepProgress() < 1.0);
            if (const auto box = focusBox(scene, tour)) {
                glm::dvec2 px;
                const bool inFront = cam.project(box->center(), kW, kH, px);
                CHECK(inFront);
                CHECK(px.x >= 0.0);
                CHECK(px.x <= kW);
                CHECK(px.y >= 0.0);
                CHECK(px.y <= kH);
                ++framedChecks;
                // A focus step (no bookmark) lands with the target on the AABB centre.
                if (!tour.current().bookmark)
                    CHECK(glm::length(cam.target() - box->center()) < 1e-6 * (1.0 + box->radius()));
            }
            // The Inspector follows: the focus is selected when it is pickable.
            if (const Node* n = scene.node(tour.focusId()); n != nullptr && n->pickable)
                CHECK(ui.selected() == tour.focusId());
        }
    }
    CHECK_FALSE(tour.playing());
    CHECK(tour.finished());
    CHECK(tour.step() == tour.size() - 1);
    CHECK(visited.size() == tour.size());
    CHECK(framedChecks == tour.size());
    CHECK(static_cast<double>(frames) * kDt == Catch::Approx(expected).margin(0.5));
    CHECK(tour.stepProgress() == 1.0);

    // Spec 17 §7.10: the view state the tour changed is restored exactly.
    CHECK(ui.saveViewState() == before);
    CHECK(ui.view().xray == v0.xray);
    CHECK(ui.view().cutaway == v0.cutaway);
    CHECK(ui.view().cansVisible == v0.cansVisible);
    CHECK(ui.view().layers == v0.layers);
    CHECK(ui.view().explode == v0.explode);
    CHECK(ui.layerVisible(Group::Rack) == false);
    CHECK(ui.xray());

    // Playing again after the end restarts from the first stop.
    tour.play();
    CHECK(tour.step() == 0);
    CHECK(tour.playing());
    tour.stop();
    CHECK_FALSE(tour.active());
    CHECK(ui.saveViewState() == before);
}

TEST_CASE("tour: any camera input pauses, play resumes, transport seeks and stop restores") {
    Scene scene = build("sc_lab_standard");
    Interaction ui(scene);
    Tour tour = loadTour("sc_lab_standard", scene, ui);
    gfx::Camera cam;
    cam.setAspect(static_cast<double>(kW) / kH);
    cam.set(ui.bookmark("Overview")->view);
    const core::Json before = ui.saveViewState();

    tour.seek(3); // a focus step with a view change: seeking from idle starts the tour there
    REQUIRE(tour.playing());
    CHECK(tour.step() == 3);
    for (int i = 0; i < 20; ++i)
        tour.update(kDt, cam);
    CHECK(tour.phase() == Tour::Phase::Flying);
    CHECK(ui.saveViewState() != before); // the step's view was applied

    // The viewport orbits the camera under the tour (spec 18 §6): the next update pauses it.
    cam.orbit(12.0, 0.0);
    tour.update(kDt, cam);
    CHECK(tour.paused());
    CHECK_FALSE(tour.playing());
    CHECK(tour.active());
    const gfx::Bookmark held = cam.bookmark();
    tour.update(kDt, cam); // paused: the camera is left alone
    CHECK(cam.position() == held.position);
    const double frozen = tour.stepProgress();
    CHECK(frozen > 0.0);
    CHECK(tour.stepProgress() == frozen);

    // Play re-flies to the stop and dwells there.
    tour.play();
    CHECK(tour.playing());
    CHECK(tour.step() == 3);
    for (int i = 0; i < 60; ++i)
        tour.update(kDt, cam);
    CHECK(tour.phase() == Tour::Phase::Dwelling);
    const auto box = focusBox(scene, tour);
    REQUIRE(box.has_value());
    CHECK(glm::length(cam.target() - box->center()) < 1e-6);

    // A cancelled flight (the viewport's click) is an interruption too.
    tour.next();
    CHECK(tour.step() == 4);
    tour.update(kDt, cam);
    CHECK(tour.phase() == Tour::Phase::Flying);
    cam.cancelTransition();
    tour.update(kDt, cam);
    CHECK(tour.paused());

    // The explicit call, and the transport from a paused state.
    tour.play();
    tour.update(kDt, cam);
    tour.userInterrupted();
    CHECK(tour.paused());
    tour.prev();
    CHECK(tour.step() == 3);
    CHECK(tour.playing());
    tour.seek(tour.size() + 10); // clamped
    CHECK(tour.step() == tour.size() - 1);
    tour.next(); // past the end: over
    CHECK_FALSE(tour.active());
    CHECK(tour.finished());
    CHECK(ui.saveViewState() == before);
    tour.stop(); // idempotent
    CHECK(ui.saveViewState() == before);

    // Physical-lab mode (spec 19 §2): exactly the stops whose focus descriptor is Simulator-only
    // are skipped, and a play-through never dwells on one of them.
    tour.setPhysicalLab(true);
    std::size_t skipped = 0;
    for (std::size_t k = 0; k < tour.size(); ++k) {
        const Node* n = scene.node(tour.focusId(k));
        const ComponentDescriptor* d = n != nullptr ? scene.descriptor(*n) : nullptr;
        INFO("step " << k << " '" << tour.steps()[k].id << "'");
        CHECK(tour.stepSkipped(k) == (d != nullptr && d->simulatorOnly));
        if (tour.stepSkipped(k))
            ++skipped;
    }
    CHECK(skipped >= 1); // the transmon stop: its Bloch row is Simulator-only (spec 00 §6)
    CHECK(skipped < tour.size() / 2);
    tour.play();
    std::set<std::size_t> dwelt;
    for (int i = 0; i < 60 * 3600 && tour.playing(); ++i) {
        tour.update(kDt, cam);
        if (tour.phase() == Tour::Phase::Dwelling)
            dwelt.insert(tour.step());
    }
    CHECK(tour.finished());
    CHECK(dwelt.size() == tour.size() - skipped);
    for (std::size_t k : dwelt)
        CHECK_FALSE(tour.stepSkipped(k));
    tour.setPhysicalLab(false);
    CHECK(ui.saveViewState() == before);
}

TEST_CASE(
    "tour: the live rows come from the focus descriptor's spec sheet and the hover is untouched") {
    Scene scene = build("sc_lab_standard");
    Interaction ui(scene);
    Tour tour = loadTour("sc_lab_standard", scene, ui);
    gfx::Camera cam;
    cam.setAspect(static_cast<double>(kW) / kH);
    // Hover something in the viewport; the tour's rows must not disturb it (spec 17 §7.1 timer).
    const ComponentId hovered = scene.findByInstance("ovc");
    REQUIRE(hovered.value != 0);
    ui.hover(hovered, 0.0);
    std::size_t stepsWithRows = 0;
    for (std::size_t k = 0; k < tour.size(); ++k) {
        tour.seek(k);
        tour.update(kDt, cam);
        const std::vector<Tooltip::LiveRow> rows = tour.liveRows();
        CHECK(rows.size() <= 3);
        const Node* n = scene.node(tour.focusId(k));
        const ComponentDescriptor* d = n != nullptr ? scene.descriptor(*n) : nullptr;
        std::size_t bound = 0;
        if (d != nullptr)
            for (const SpecRow& r : d->specSheet)
                if (r.binding)
                    ++bound;
        CHECK(rows.size() == std::min<std::size_t>(bound, 3));
        for (const Tooltip::LiveRow& r : rows) {
            CHECK(r.text.find(':') != std::string::npos);
            CHECK(r.text.find("—") != std::string::npos); // no registry: every value reads "—"
        }
        if (!rows.empty())
            ++stepsWithRows;
    }
    CHECK(stepsWithRows >= 20);
    CHECK(ui.hovered() == hovered);
    CHECK(ui.tooltip(1.0).has_value()); // the hover timer still started at t = 0
    tour.stop();
}

TEST_CASE("tour: the ion-trap script loads against the ion scene and plays") {
    Scene scene = build("ion_lab_11");
    Interaction ui(scene);
    Tour tour = loadTour("ion_lab_11", scene, ui);
    CHECK(tour.size() >= 14);
    for (std::size_t k = 0; k < tour.size(); ++k) {
        const TourStep& s = tour.steps()[k];
        INFO("step " << k << " '" << s.id << "'");
        if (!s.focusInstance.empty() || !s.focusDescriptor.empty())
            CHECK(tour.focusId(k).value != 0);
        CHECK(std::count(s.narration.begin(), s.narration.end(), '.') >= 3);
    }
    gfx::Camera cam;
    cam.setAspect(static_cast<double>(kW) / kH);
    cam.set(ui.bookmark("Overview")->view);
    tour.play();
    std::set<std::size_t> visited;
    int frames = 0;
    while (tour.playing() && frames < 60 * 1200) {
        tour.update(kDt, cam);
        ++frames;
        if (tour.phase() == Tour::Phase::Dwelling) {
            visited.insert(tour.step());
            if (const auto box = focusBox(scene, tour)) {
                glm::dvec2 px;
                INFO("step " << tour.step());
                CHECK(cam.project(box->center(), kW, kH, px));
                CHECK((px.x >= 0.0 && px.x <= kW && px.y >= 0.0 && px.y <= kH));
            }
        }
    }
    CHECK(tour.finished());
    CHECK(visited.size() == tour.size());
}

TEST_CASE(
    "tour: a script with an unresolved focus, bookmark or anchor is refused naming each one") {
    Scene scene = build("ion_lab_11");
    Interaction ui(scene);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "qxl_tour_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "broken.json";
    {
        std::ofstream out(file);
        out << R"({"qxl": {"kind": "lab.tour", "schema": 1, "app": "test", "created": "2026-09-18T00:00:00Z"},
                  "data": {"id": "broken", "steps": [
          {"id": "a", "title": "A", "focus_instance": "trap_chip", "theory": "T06#1.1-rf-confinement-and-the-pseudopotential",
           "narration": "One sentence of enough length to pass the loader. Two sentences. Three sentences with 11 ions."},
          {"id": "b", "title": "B", "focus_instance": "no_such_part", "theory": "T06#1-the-linear-paul-trap",
           "narration": "One sentence of enough length to pass the loader. Two sentences. Three sentences with 11 ions."},
          {"id": "c", "title": "C", "bookmark": "Nowhere", "theory": "T06#99-not-a-heading",
           "narration": "One sentence of enough length to pass the loader. Two sentences. Three sentences with 11 ions."}
        ]}})";
    }
    const auto t = Tour::load(file, scene, ui, nullptr, theoryDir());
    REQUIRE_FALSE(t.has_value());
    CHECK(t.error().code == kErrTour);
    const std::string& msg = t.error().message;
    CHECK(msg.find("Nowhere") != std::string::npos);
    CHECK(msg.find("T06#99-not-a-heading") != std::string::npos);
    // A focus that this scene lacks is NOT a refusal: the same script serves every device of the
    // layout, so the step is skipped and named in `warnings()` (spec 17 §7.10).
    CHECK(msg.find("no_such_part") == std::string::npos);
    const std::filesystem::path partial = dir / "partial.json";
    {
        std::ofstream out(partial);
        out << R"({"qxl": {"kind": "lab.tour", "schema": 1, "app": "test", "created": "2026-09-18T00:00:00Z"},
                  "data": {"id": "partial", "steps": [
          {"id": "a", "title": "A", "focus_instance": "trap_chip", "theory": "T06#1.1-rf-confinement-and-the-pseudopotential",
           "narration": "One sentence of enough length to pass the loader. Two sentences. Three sentences with 11 ions."},
          {"id": "b", "title": "B", "focus_instance": "no_such_part", "theory": "T06#1-the-linear-paul-trap",
           "narration": "One sentence of enough length to pass the loader. Two sentences. Three sentences with 11 ions."}
        ]}})";
    }
    auto p = Tour::load(partial, scene, ui, nullptr, theoryDir());
    REQUIRE(p.has_value());
    REQUIRE(p->warnings().size() == 1);
    CHECK(p->warnings().front().find("no_such_part") != std::string::npos);
    CHECK_FALSE(p->stepSkipped(0));
    CHECK(p->stepSkipped(1));
    p->play();
    p->next(); // the only other step is skipped, so the tour is over
    CHECK(p->finished());
    // A script none of whose steps resolve is refused.
    const std::filesystem::path none = dir / "none.json";
    {
        std::ofstream out(none);
        out << R"({"qxl": {"kind": "lab.tour", "schema": 1, "app": "test", "created": "2026-09-18T00:00:00Z"},
                  "data": {"id": "none", "steps": [
          {"id": "b", "title": "B", "focus_instance": "no_such_part", "theory": "T06#1-the-linear-paul-trap",
           "narration": "One sentence of enough length to pass the loader. Two sentences. Three sentences with 11 ions."}
        ]}})";
    }
    CHECK_FALSE(Tour::load(none, scene, ui, nullptr, theoryDir()).has_value());
    CHECK(msg.find("'a'") == std::string::npos); // the good step is not reported
    // A wrong envelope kind is refused too.
    CHECK_FALSE(
        Tour::load(core::assetDir() / "Lab" / "Layouts" / "ion_lab_11" / "layout.json", scene, ui)
            .has_value());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("tour: seeking to a stop applies the view state of every stop before it") {
    Scene scene = build("sc_lab_standard");
    Interaction ui(scene);
    auto t = Tour::load(core::assetDir() / "Lab" / "Tours" / "sc_lab_standard.json", scene, ui,
                        nullptr, theoryDir());
    REQUIRE(t.has_value());
    // The script opens the fridge at an early stop (cans off or a cutaway) and the mixing-chamber
    // stop itself changes nothing, so a seek straight to it must arrive with the interior visible:
    // the folded state is what a play-through would have left, not the stop's own (empty) view.
    std::size_t mxc = t->size();
    for (std::size_t k = 0; k < t->size(); ++k)
        if (t->steps()[k].focusInstance == "stage_mxc" ||
            t->steps()[k].focusDescriptor == "stage_mxc") {
            mxc = k;
            break;
        }
    REQUIRE(mxc < t->size());
    CHECK_FALSE(t->steps()[mxc].view.cans.has_value());
    CHECK_FALSE(t->steps()[mxc].view.cutaway.has_value());
    const TourStep::View folded = t->foldedView(mxc);
    const bool opened = (folded.cans && !*folded.cans) || (folded.cutaway && *folded.cutaway);
    CHECK(opened);
    CHECK(ui.cansVisible());
    CHECK_FALSE(ui.cutaway());
    gfx::Camera cam;
    cam.lookAt({5.0, 3.0, 6.0}, {0.0, 1.2, 0.0});
    t->play();
    t->seek(mxc);
    for (int i = 0; i < 90; ++i)
        t->update(1.0 / 60.0, cam); // the 0.9 s flight and a moment of dwell
    CHECK((!ui.cansVisible() || ui.cutaway()));
    if (ui.cutaway()) {
        // The half of every can facing the camera is the one removed: the eye lies in the
        // discarded half-space dot(n, p) + w < 0 (Interaction::cutawayPlane).
        const glm::dvec4 plane = ui.cutawayPlane();
        const glm::dvec3 eye = cam.position();
        CHECK(glm::dot(glm::dvec3(plane), eye) + plane.w < 0.0);
    }
    t->stop();
    CHECK(ui.cansVisible()); // restored
    CHECK_FALSE(ui.cutaway());
}
