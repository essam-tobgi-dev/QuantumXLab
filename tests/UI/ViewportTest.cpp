// Spec 19 §3 "Viewport" / 18 §6 — the picture is an input surface: a drag orbits the camera, a
// right-drag pans, the wheel zooms, and none of it moves the panel. Headless, with the mouse
// driven through ImGui's input queue; no renderer, so there is no picture and no pick — the
// camera must still respond.
#include "UiHarness.hpp"
#include "UI/Panels/Panels.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h>
#include <cmath>

using namespace qlab;
using namespace qlab::ui;
using Catch::Approx;

namespace {

struct Rig {
    test::UiHarness ui;
    PanelPtr panel = makeViewportPanel();
    gfx::Camera camera;
    Rig() {
        camera.lookAt({0.0, 1.5, 6.0}, {0.0, 1.0, 0.0});
        ui.context().camera = &camera;
    }
    // One frame with the panel in a fixed window; `pre` queues input BEFORE the frame.
    template <class F> void frame(F&& pre) {
        pre(ImGui::GetIO());
        ui.frame([&] {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f));
            ImGui::Begin("viewport_rig");
            panel->draw(ui.context());
            ImGui::End();
        });
    }
    void frame() { frame([](ImGuiIO&) {}); }
    ImVec2 windowPos() const { return ImGui::FindWindowByName("viewport_rig")->Pos; }
};

} // namespace

TEST_CASE("Viewport: a left drag on the picture orbits the camera and does not move the panel") {
    Rig rig;
    if (!rig.ui.ready()) SKIP("the pinned fonts are not available");
    rig.frame();
    rig.frame();
    const glm::dvec3 pos0 = rig.camera.position();
    const glm::dvec3 target0 = rig.camera.target();
    const ImVec2 win0 = rig.windowPos();
    rig.frame([](ImGuiIO& io) { io.AddMousePosEvent(400.0f, 350.0f); });
    rig.frame([](ImGuiIO& io) { io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); });
    rig.frame([](ImGuiIO& io) { io.AddMousePosEvent(460.0f, 350.0f); });
    rig.frame([](ImGuiIO& io) { io.AddMousePosEvent(520.0f, 330.0f); });
    rig.frame([](ImGuiIO& io) { io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); });
    rig.frame();
    // Orbit: the eye moved on its sphere about the unchanged target — and the scene followed the
    // mouse: a drag to the RIGHT (+x on screen) turns the eye toward +x so what was under the
    // cursor slides right (the grab convention; "Invert orbit" gives the camera-turn one).
    CHECK(glm::length(rig.camera.position() - pos0) > 0.1);
    CHECK(rig.camera.position().x > pos0.x + 0.1);
    CHECK(glm::length(rig.camera.target() - target0) < 1e-9);
    CHECK(glm::length(rig.camera.position() - target0) == Approx(glm::length(pos0 - target0)).epsilon(1e-9));
    // …and the window stayed where it was (an `Image` let the drag grab the window instead).
    const ImVec2 win1 = rig.windowPos();
    CHECK(win1.x == Approx(win0.x));
    CHECK(win1.y == Approx(win0.y));
}

TEST_CASE("Viewport: a right drag pans, Shift-drag pans, and the wheel zooms") {
    Rig rig;
    if (!rig.ui.ready()) SKIP("the pinned fonts are not available");
    rig.frame();
    rig.frame();
    const glm::dvec3 pos0 = rig.camera.position();
    const glm::dvec3 target0 = rig.camera.target();
    rig.frame([](ImGuiIO& io) { io.AddMousePosEvent(400.0f, 350.0f); });
    rig.frame([](ImGuiIO& io) { io.AddMouseButtonEvent(ImGuiMouseButton_Right, true); });
    rig.frame([](ImGuiIO& io) { io.AddMousePosEvent(440.0f, 380.0f); });
    rig.frame([](ImGuiIO& io) { io.AddMouseButtonEvent(ImGuiMouseButton_Right, false); });
    rig.frame();
    // Pan: eye and target translate together, the distance is unchanged.
    const glm::dvec3 dEye = rig.camera.position() - pos0, dTarget = rig.camera.target() - target0;
    CHECK(glm::length(dEye) > 1e-3);
    CHECK(glm::length(dEye - dTarget) < 1e-9);
    CHECK(rig.camera.distance() == Approx(glm::length(pos0 - target0)).epsilon(1e-9));

    const double d1 = rig.camera.distance();
    rig.frame([](ImGuiIO& io) { io.AddMouseWheelEvent(0.0f, 2.0f); });
    rig.frame();
    // Wheel: without a pick the zoom goes toward the target, 0.9 per notch.
    CHECK(rig.camera.distance() == Approx(d1 * 0.81).epsilon(1e-6));

    // Shift + left drag also pans.
    const glm::dvec3 pos2 = rig.camera.position(), target2 = rig.camera.target();
    rig.frame([](ImGuiIO& io) { io.AddKeyEvent(ImGuiMod_Shift, true); io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); });
    rig.frame([](ImGuiIO& io) { io.AddMousePosEvent(400.0f, 300.0f); });
    rig.frame([](ImGuiIO& io) { io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); io.AddKeyEvent(ImGuiMod_Shift, false); });
    rig.frame();
    CHECK(glm::length((rig.camera.position() - pos2) - (rig.camera.target() - target2)) < 1e-9);
    CHECK(glm::length(rig.camera.position() - pos2) > 1e-3);
}

TEST_CASE("Viewport: the panel state round-trips the help toggle") {
    PanelPtr panel = makeViewportPanel();
    core::Json j = panel->serialize();
    REQUIRE(j.is_object());
    CHECK(j.contains("show_help"));
    j["show_help"] = true;
    panel->deserialize(j);
    CHECK(panel->serialize()["show_help"].get<bool>());
}
