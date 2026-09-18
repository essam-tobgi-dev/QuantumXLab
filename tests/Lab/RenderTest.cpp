// Spec 17 §9/§10, 18 §9 — headless render of the standard laboratory: culling and LOD run on the
// CPU, the scene renders to a PNG in the build directory and picking returns the component under
// the cursor. Skipped when no GL context is available.
#include "Graphics/Window.hpp"
#include "Lab/Lab.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using namespace qlab;
using namespace qlab::lab;

namespace {
constexpr int kWidth = 800, kHeight = 600;

std::unique_ptr<gfx::Window> hiddenWindow() {
    gfx::WindowDesc d;
    d.visible = false;
    d.width = kWidth;
    d.height = kHeight;
    d.vsync = false;
    auto w = gfx::Window::create(d);
    return w ? std::move(*w) : nullptr;
}

Scene build() {
    auto s = buildScene("sc_lab_standard");
    if (!s) FAIL(s.error().format());
    return std::move(*s);
}

std::filesystem::path buildDir() {
    std::filesystem::path dir = std::filesystem::path(QXL_SOURCE_DIR) / "build";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}
} // namespace

TEST_CASE("culling and level of detail run without a GL context") {
    Scene scene = build();
    Interaction ui(scene);
    SceneRenderer renderer(scene);
    gfx::Camera camera;
    camera.setAspect(static_cast<double>(kWidth) / kHeight);
    camera.lookAt({5.0, 3.0, 6.0}, {0.0, 1.2, 0.0});
    fitClipPlanes(camera);
    renderer.prepare(camera, ui);
    const RenderStats overview = renderer.stats();
    CHECK(overview.considered > 100);
    CHECK(overview.triangles > 0);
    CHECK(overview.triangles < 3'000'000); // spec 24 §6 budget at a bookmark
    CHECK(overview.occluded > 0);          // the interior (chip included) is inside the closed vacuum can

    // Hiding the cans exposes the interior; the occlusion test then keeps nothing out, and the
    // chip, 8 m away, is hidden by its micrometre parts' LOD rule instead.
    ui.setCansVisible(false);
    renderer.prepare(camera, ui);
    CHECK(renderer.stats().occluded == 0);
    CHECK(renderer.stats().lodHidden > 0);
    CHECK(renderer.stats().layerHidden > 0); // the cans themselves

    // Beyond 6 m the wiring LOD hides the lines; X-ray brings them back (spec 17 §7.6/§10) and
    // fades the vacuum can and the shields to 12 % opacity.
    ui.setCansVisible(true);
    gfx::Camera far;
    far.setAspect(static_cast<double>(kWidth) / kHeight);
    far.lookAt({14.0, 7.0, 16.0}, {0.0, 1.2, 0.0});
    fitClipPlanes(far);
    ui.setLayerVisible(Group::FridgeExterior, false); // let the interior through the occlusion test
    renderer.prepare(far, ui);
    std::size_t plainWiring = 0;
    for (const auto& item : renderer.items())
        if (scene.node(item.id)->group == Group::Wiring) ++plainWiring;
    ui.setXray(true);
    ui.setLayerVisible(Group::FridgeExterior, true);
    renderer.prepare(far, ui);
    std::size_t xrayWiring = 0;
    bool faded = false;
    for (const auto& item : renderer.items()) {
        if (scene.node(item.id)->group == Group::Wiring) ++xrayWiring;
        if (scene.node(item.id)->xrayFade) faded = faded || item.material.baseColor.a < 0.2f;
    }
    CHECK(plainWiring == 0); // hidden by their LOD rule at this distance
    CHECK(xrayWiring > 100);
    CHECK(faded);

    // Focused on a qubit, only the chip's micrometre parts survive the frustum
    ui.setXray(false);
    ui.setLayerVisible(Group::FridgeExterior, true);
    REQUIRE(ui.applyBookmark("Qubit q[7]", camera, 0.0));
    renderer.prepare(camera, ui);
    CHECK(renderer.stats().frustumCulled > 100);
    bool micro = false;
    for (const auto& item : renderer.items()) micro = micro || scene.node(item.id)->group == Group::ChipMicro;
    CHECK(micro);
}

TEST_CASE("headless render writes a PNG and picks the mixing-chamber plate") {
    auto window = hiddenWindow();
    if (!window) {
        SKIP("no GL context available");
    }
    gfx::RendererDesc desc;
    desc.samples = 4;
    desc.shadowSize = 1024;
    auto created = gfx::Renderer::create(desc);
    REQUIRE(created.has_value());
    gfx::Renderer& renderer = **created;

    Scene scene = build();
    BindingRegistry registry;
    Interaction ui(scene, &registry);
    ui.setCansVisible(false); // open the fridge: the cans would hide the mixing chamber
    SceneRenderer sceneRenderer(scene);

    // Between the cold plate and the mixing chamber, on an azimuth free of wiring bundles.
    const Node* mxc = scene.node(scene.stageNodes()[5]);
    REQUIRE(mxc != nullptr);
    glm::dvec3 target(mxc->world[3]);
    const double azimuth = glm::radians(337.5);
    gfx::Camera camera;
    camera.setFov(40.0);
    camera.setAspect(static_cast<double>(kWidth) / kHeight);
    camera.lookAt(target + glm::dvec3(0.45 * std::cos(azimuth), 0.10, 0.45 * std::sin(azimuth)), target);
    fitClipPlanes(camera);

    for (int frame = 0; frame < 2; ++frame) { // the first frame settles the LOD hysteresis
        sceneRenderer.prepare(camera, ui);
        renderer.beginFrame(kWidth, kHeight, camera, 0.0);
        sceneRenderer.submit(renderer, ui);
        renderer.endFrame();
    }
    glm::dvec2 pixel;
    REQUIRE(camera.project(target, kWidth, kHeight, pixel));
    ComponentId picked = renderer.pick(static_cast<int>(pixel.x), static_cast<int>(pixel.y));
    INFO("picked " << picked.value << " = " << (scene.node(picked) ? scene.node(picked)->instanceName : "background"));
    CHECK(picked == scene.stageNodes()[5]);
    CHECK(scene.inspect(picked).descriptorId == "stage_mxc");
    // repeated parts are drawn instanced with contiguous ids (spec 17 §9)
    CHECK(sceneRenderer.stats().instancedBatches > 0);
    CHECK(sceneRenderer.stats().instances > sceneRenderer.stats().instancedBatches);
    CHECK(sceneRenderer.stats().triangles < 3'000'000);
    CHECK(renderer.stats().drawCalls > 0);

    auto mxcPng = buildDir() / "lab_mxc.png";
    REQUIRE(renderer.screenshot(mxcPng).has_value());
    CHECK(std::filesystem::file_size(mxcPng) > 1000);

    // an overview frame with the selection outline, its label and the overlays
    LabOverlays overlays(scene, registry);
    ui.setCansVisible(true);
    ui.select(scene.findByInstance("drive_q3.att_MXC"));
    ui.hover(scene.stageNodes()[5], 0.0);
    REQUIRE(ui.applyBookmark("Overview", camera, 0.0));
    overlays.update(0.0);
    sceneRenderer.prepare(camera, ui, &overlays.visuals());
    renderer.beginFrame(kWidth, kHeight, camera, 0.0);
    sceneRenderer.submit(renderer, ui);
    sceneRenderer.submitOverlays(renderer, overlays);
    renderer.endFrame();
    auto overviewPng = buildDir() / "lab_overview.png";
    REQUIRE(renderer.screenshot(overviewPng).has_value());
    CHECK(std::filesystem::file_size(overviewPng) > 1000);
    // the room shell is scenery: a corner pixel of the overview is background, never a component
    CHECK(renderer.pick(2, 2).value == 0u);

    // the cutaway clips the cans without touching the interior
    ui.setCutaway(true, 90.0);
    sceneRenderer.prepare(camera, ui);
    std::size_t clipped = 0;
    for (const auto& item : sceneRenderer.items()) clipped += item.clipped ? 1 : 0;
    CHECK(clipped > 0);
    renderer.beginFrame(kWidth, kHeight, camera, 0.0);
    sceneRenderer.submit(renderer, ui);
    renderer.endFrame();
    auto cutPng = buildDir() / "lab_cutaway.png";
    REQUIRE(renderer.screenshot(cutPng).has_value());
    sceneRenderer.releaseGpu();
}

// Fridge detail pass: one screenshot per layout bookmark (plus the interior views with the cans
// hidden) in the build directory, for the geometry review of spec 17 §3.1. Each frame must stay
// inside the 3 M triangle budget and pick a component at the image centre when one is there.
TEST_CASE("every bookmark renders to a PNG within the triangle budget") {
    auto window = hiddenWindow();
    if (!window) {
        SKIP("no GL context available");
    }
    gfx::RendererDesc desc;
    desc.samples = 4;
    desc.shadowSize = 2048;
    auto created = gfx::Renderer::create(desc);
    REQUIRE(created.has_value());
    gfx::Renderer& renderer = **created;
    renderer.setSun(glm::normalize(glm::vec3(-0.35f, -0.85f, -0.4f)), glm::vec3(1.0f, 0.98f, 0.94f), 3.0f);
    renderer.setAmbient(glm::vec3(0.22f));
    Scene scene = build();
    Interaction ui(scene);
    SceneRenderer sceneRenderer(scene);
    sceneRenderer.setLabels(false);
    struct Shot {
        const char* bookmark;
        bool cans;
        const char* file;
    };
    const Shot shots[]{{"Overview", true, "lab_bm_overview.png"}, {"Fridge", true, "lab_bm_fridge.png"},
                       {"Fridge", false, "lab_bm_fridge_open.png"}, {"MXC", false, "lab_bm_mxc.png"},
                       {"Chip", true, "lab_bm_chip.png"}, {"Rack", true, "lab_bm_rack.png"}, {"GHS", true, "lab_bm_ghs.png"}};
    for (const Shot& shot : shots) {
        INFO(shot.file);
        gfx::Camera camera;
        camera.setAspect(static_cast<double>(kWidth) / kHeight);
        for (int g = 0; g < kGroupCount; ++g) ui.setLayerVisible(static_cast<Group>(g), true);
        REQUIRE(ui.applyBookmark(shot.bookmark, camera, 0.0));
        ui.setCansVisible(shot.cans);
        for (int frame = 0; frame < 2; ++frame) {
            sceneRenderer.prepare(camera, ui);
            renderer.beginFrame(kWidth, kHeight, camera, 0.0);
            sceneRenderer.submit(renderer, ui);
            renderer.endFrame();
        }
        CHECK(sceneRenderer.stats().triangles < 3'000'000);
        CHECK(sceneRenderer.pointLights(camera.position()).size() == scene.layout().lights.size());
        auto png = buildDir() / shot.file;
        REQUIRE(renderer.screenshot(png).has_value());
        CHECK(std::filesystem::file_size(png) > 1000);
    }
    // two review cameras that no bookmark covers: the top plate from above (feedthrough ring, KF
    // ports, viewport, pulse-tube head) and the upper stages from the side with the cans hidden
    struct Review {
        glm::dvec3 eye, target;
        bool cans;
        const char* file;
    };
    const Review reviews[]{{{-0.9, 3.1, 0.7}, {0.0, 2.3, 0.0}, true, "lab_bm_topplate.png"},
                           {{0.75, 2.45, 0.55}, {0.15, 2.25, 0.0}, true, "lab_bm_ovc_flange.png"},
                           {{1.1, 2.2, 0.9}, {0.0, 1.9, 0.0}, false, "lab_bm_stages.png"},
                           {{0.55, 1.05, 0.5}, {0.0, 1.1, 0.0}, false, "lab_bm_sample_stage.png"},
                           // rack detail pass: the instruments up close (rack B test gear, rack A control), the
                           // gas-handling panel, the bench instruments, the workstation, the dewars and the door
                           {{2.9, 1.65, -0.10}, {2.9, 1.50, -1.05}, true, "lab_bm_rack_b_close.png"},
                           {{2.2, 1.55, -0.10}, {2.2, 1.45, -1.05}, true, "lab_bm_rack_a_close.png"},
                           {{-2.5, 1.35, 0.05}, {-2.5, 1.20, -1.15}, true, "lab_bm_ghs_panel.png"},
                           {{-1.5, 1.45, 3.2}, {-1.5, 0.95, 2.0}, true, "lab_bm_bench.png"},
                           {{2.5, 1.55, 3.3}, {2.5, 0.85, 1.8}, true, "lab_bm_workstation.png"},
                           {{-0.6, 1.9, 0.0}, {-3.4, 1.0, 1.7}, true, "lab_bm_door_dewars.png"}};
    for (const Review& view : reviews) {
        INFO(view.file);
        gfx::Camera cam;
        cam.setFov(40.0);
        cam.setAspect(static_cast<double>(kWidth) / kHeight);
        cam.lookAt(view.eye, view.target);
        fitClipPlanes(cam);
        ui.setCansVisible(view.cans);
        for (int frame = 0; frame < 2; ++frame) {
            sceneRenderer.prepare(cam, ui);
            renderer.beginFrame(kWidth, kHeight, cam, 0.0);
            sceneRenderer.submit(renderer, ui);
            renderer.endFrame();
        }
        REQUIRE(renderer.screenshot(buildDir() / view.file).has_value());
    }
    // the open fridge shows the chandelier: the mixing-chamber plate is under the MXC bookmark's centre
    gfx::Camera camera;
    camera.setAspect(static_cast<double>(kWidth) / kHeight);
    REQUIRE(ui.applyBookmark("MXC", camera, 0.0));
    ui.setCansVisible(false);
    sceneRenderer.prepare(camera, ui);
    renderer.beginFrame(kWidth, kHeight, camera, 0.0);
    sceneRenderer.submit(renderer, ui);
    renderer.endFrame();
    ComponentId picked = renderer.pick(kWidth / 2, kHeight / 2);
    INFO("picked " << (scene.node(picked) ? scene.node(picked)->instanceName : "background"));
    CHECK(picked.value != 0u);
    sceneRenderer.releaseGpu();
}
