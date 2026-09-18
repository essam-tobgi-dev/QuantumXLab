// Spec 17 §7.10 / 19 §3 — the Guided tour panel and the viewport narration card in a headless
// frame with a running tour (harness: UiHarness.hpp; fixture style of FrameTest.cpp).
#include "UiHarness.hpp"
#include "Core/Paths.hpp"
#include "Lab/Tour.hpp"
#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h>

using namespace qlab;
using namespace qlab::ui;

namespace {

struct TourFixture {
    lab::Scene scene;
    lab::Interaction ui;
    gfx::Camera cam;
    std::optional<lab::Tour> tour;

    static lab::Scene build() {
        auto s = lab::buildScene("sc_lab_standard");
        if (!s) FAIL(s.error().format());
        return std::move(*s);
    }
    TourFixture() : scene(build()), ui(scene) {
        cam.setAspect(16.0 / 9.0);
        cam.set(ui.bookmark("Overview")->view);
        auto t = lab::Tour::load(core::assetDir() / "Lab" / "Tours" / "sc_lab_standard.json", scene, ui, nullptr,
                                 std::filesystem::path(QXL_SOURCE_DIR) / "docs" / "theory");
        if (!t) FAIL(t.error().format());
        tour.emplace(std::move(*t));
    }
    void bind(UiContext& ctx) {
        ctx.scene = &scene;
        ctx.interaction = &ui;
        ctx.camera = &cam;
        ctx.tour = &*tour;
    }
};

// One frame: the panel in its own window plus the overlay over a fake viewport image.
template <class Harness>
void frameWith(Harness& h, Panel& panel, UiContext& ctx, bool overlay) {
    // Two frames: child windows and scrollbars settle on the second one, so vertex counts of
    // identical content are comparable.
    for (int pass = 0; pass < 2; ++pass) h.frame([&] {
        ImGui::SetNextWindowSize(ImVec2(900.0f, 420.0f), ImGuiCond_Always);
        ImGui::Begin(panel.windowTitle().c_str());
        panel.draw(ctx);
        ImGui::End();
        // The viewport stand-in is always there; `overlay` only decides whether the card is drawn,
        // so vertex counts with and without it compare the card alone.
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1200.0f, 700.0f), ImGuiCond_Always);
        ImGui::Begin("##viewport_stand_in", nullptr, ImGuiWindowFlags_NoDecoration);
        if (overlay) drawTourOverlay(ctx, ImVec2(0.0f, 0.0f), ImVec2(1200.0f, 700.0f));
        ImGui::End();
    });
}

} // namespace

TEST_CASE("Tour panel: catalog identity, the Lab preset and a headless frame with a running tour") {
    test::UiHarness h;
    if (!h.ready()) SKIP("pinned fonts not on disk");
    PanelPtr panel = makePanel(PanelId::Tour);
    REQUIRE(panel != nullptr);
    CHECK(panel->key() == "tour");
    CHECK(panel->icon() == "▶");
    CHECK(panel->defaultWorkspace() == Workspace::Lab);
    CHECK(panel->title() == "Guided Tour");
    CHECK(preset(Workspace::Lab).contains(PanelId::Tour));
    CHECK_FALSE(panel->simulatorOnly());

    UiContext& ctx = h.context();
    std::string opened;
    ctx.cmd.openTheory = [&](std::string_view anchor) { opened = std::string(anchor); };

    // No tour: the placeholder, and no overlay at all.
    frameWith(h, *panel, ctx, true);
    const int placeholderVertices = h.vertices();
    CHECK(placeholderVertices > 0);

    TourFixture f;
    f.bind(ctx);
    // Idle tour: the panel lists the steps; the overlay still draws nothing.
    frameWith(h, *panel, ctx, false);
    const int idlePanel = h.vertices();
    CHECK(idlePanel > placeholderVertices);
    frameWith(h, *panel, ctx, true);
    const int idleWithOverlay = h.vertices();
    CHECK(idleWithOverlay == idlePanel); // the card is absent while the tour is idle

    // Running tour: the model advances (as LabModel::tickLab does), the panel and the card draw it.
    f.tour->play();
    for (int i = 0; i < 70; ++i) f.tour->update(1.0 / 60.0, f.cam);
    REQUIRE(f.tour->playing());
    CHECK(f.tour->phase() == lab::Tour::Phase::Dwelling);
    frameWith(h, *panel, ctx, true);
    const int running = h.vertices();
    CHECK(running > idleWithOverlay);
    frameWith(h, *panel, ctx, false);
    CHECK(h.vertices() < running); // the card accounted for part of it
    CHECK(ImGui::FindWindowByName(panel->windowTitle().c_str()) != nullptr);

    // Paused, later stops (chip-scale, bookmark stops) and the finished state all draw.
    f.tour->userInterrupted();
    CHECK(f.tour->paused());
    frameWith(h, *panel, ctx, true);
    CHECK(h.vertices() > idleWithOverlay);
    for (std::size_t k = 0; k < f.tour->size(); k += 5) {
        f.tour->seek(k);
        for (int i = 0; i < 60; ++i) f.tour->update(1.0 / 60.0, f.cam);
        INFO("step " << k);
        frameWith(h, *panel, ctx, true);
        CHECK(h.vertices() > idleWithOverlay);
    }
    f.tour->seek(f.tour->size() - 1);
    f.tour->next();
    CHECK(f.tour->finished());
    frameWith(h, *panel, ctx, true);
    CHECK(h.vertices() > 0);

    // Physical-lab mode hides Simulator-only rows only; the frame still draws.
    ctx.physicalLab = true;
    f.tour->setPhysicalLab(true);
    f.tour->play();
    for (int i = 0; i < 70; ++i) f.tour->update(1.0 / 60.0, f.cam);
    frameWith(h, *panel, ctx, true);
    CHECK(h.vertices() > idleWithOverlay);
    ctx.physicalLab = false;
    f.tour->stop();

    // Panel state round-trips and tolerates rubbish (spec 19 §9).
    const core::Json state = panel->serialize();
    CHECK(state.is_object());
    panel->deserialize(state);
    panel->deserialize(core::Json::array());
    panel->deserialize(core::Json{});
    ctx.tour = nullptr;
    ctx.scene = nullptr;
    ctx.interaction = nullptr;
    ctx.camera = nullptr;
}
