#include "Core/Paths.hpp"
#include "Graphics/Graphics.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <stb/stb_image.h>
using namespace qlab;
using namespace qlab::gfx;

namespace {
std::unique_ptr<Window> makeHidden() {
    WindowDesc d;
    d.visible = false;
    d.width = 320;
    d.height = 240;
    d.vsync = false;
    auto w = Window::create(d);
    return w ? std::move(*w) : nullptr;
}
} // namespace

TEST_CASE("all shaders compile and link") {
    auto win = makeHidden();
    if (!win) {
        SKIP("no GL context available");
    }
    for (auto& e : std::filesystem::directory_iterator(shaderRoot())) {
        auto p = e.path();
        if (p.extension() != ".frag")
            continue;
        std::string stem = p.stem().string();
        ShaderDesc d;
        d.name = stem;
        d.vertex = std::filesystem::exists(shaderRoot() / (stem + ".vert")) ? stem + ".vert"
                   : stem == "id"                                           ? "pbr.vert"
                   : stem.starts_with("env_")
                       ? "cube_face.vert" // IBL generation (spec 18 §5 EnvPrefilter)
                       : "fullscreen.vert";
        d.fragment = stem + ".frag";
        auto r = ShaderProgram::fromFiles(d);
        INFO(stem);
        REQUIRE(r.has_value());
        if (stem == "pbr" || stem == "shadow" || stem == "id") {
            d.defines = {"INSTANCED"};
            REQUIRE(ShaderProgram::fromFiles(d).has_value());
        }
        if (stem == "pbr") { // spec 18 §4 textures: the TEXTURED permutations
            d.defines = {"TEXTURED"};
            REQUIRE(ShaderProgram::fromFiles(d).has_value());
            d.defines = {"INSTANCED", "TEXTURED"};
            REQUIRE(ShaderProgram::fromFiles(d).has_value());
        }
        if (stem == "ssao_blur") {
            d.defines = {"UPSAMPLE"};
            REQUIRE(ShaderProgram::fromFiles(d).has_value());
        }
        if (stem == "bloom_blur") {
            d.defines = {"BRIGHT"};
            REQUIRE(ShaderProgram::fromFiles(d).has_value());
        }
    }
}

TEST_CASE("render a box: pick returns its id, screenshot written") {
    auto win = makeHidden();
    if (!win) {
        SKIP("no GL context available");
    }
    RendererDesc rd;
    rd.samples = 4;
    rd.shadowSize = 512;
    auto rr = Renderer::create(rd);
    REQUIRE(rr.has_value());
    auto& r = **rr;
    Mesh box(shapes::box({1, 1, 1}));
    Camera cam;
    cam.lookAt({0, 0, 4}, {0, 0, 0});
    cam.setAspect(320.0 / 240.0);
    Material m = Material::preset("copper");
    for (int frame = 0; frame < 2; ++frame) {
        r.beginFrame(320, 240, cam, 0.0);
        r.submit(box, m, glm::mat4(1.0f), ComponentId{7});
        r.lines().axes({0, 0, 0}, 1.0f);
        r.text().label3D({0, 0.8f, 0}, "q[0] 4.812 GHz", 14.f, {1, 1, 1, 1});
        r.text().label2D({8, 8}, "QuantumXLab", 16.f, {1, 1, 1, 1});
        r.setSelection(ComponentId{7}, ComponentId{0});
        r.endFrame();
    }
    REQUIRE(r.pick(160, 120).value == 7u);
    REQUIRE(r.pick(3, 3).value == 0u);
    REQUIRE(r.stats().drawCalls >= 2);
    auto out = std::filesystem::temp_directory_path() / "qxl_render_test.png";
    REQUIRE(r.screenshot(out).has_value());
    REQUIRE(std::filesystem::file_size(out) > 100);
    // instanced path
    std::vector<InstanceData> inst;
    for (int i = 0; i < 5; ++i)
        inst.push_back({glm::translate(glm::mat4(1.0f), {i * 0.3f - 0.6f, 0, 0}) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(0.1f)),
                        {100u + static_cast<unsigned>(i), 0u},
                        {},
                        {1, 1, 1, 1}});
    r.beginFrame(320, 240, cam, 0.0);
    r.submitInstanced(box, m, inst);
    r.endFrame();
    REQUIRE(r.pick(160, 120).value == 102u);
}

TEST_CASE("SDF atlas builds from the shipped font without GL") {
    auto ttf = core::assetDir() / "Fonts" / "Inter-Regular.ttf";
    auto a = buildSdfAtlas(ttf, 32, 6);
    REQUIRE(a.has_value());
    REQUIRE(a->glyphs.count('A') == 1);
    REQUIRE(a->glyphs.at('A').advance > 0);
    REQUIRE(a->width == 1024);
    REQUIRE(decodeUtf8("Ωμ").size() == 2);
    REQUIRE(decodeUtf8("Ωμ")[0] == 0x3A9);
}

TEST_CASE("world-space overlay lines follow the camera (no displacement far from the origin)") {
    // Regression: line.vert and text_sdf.vert subtracted a camera position that was uploaded as
    // zero, so every world-space line and 3D label was displaced by the camera position and only
    // looked right for a camera at the origin. Batches now take world positions and subtract the
    // camera origin in double precision on the CPU.
    auto win = makeHidden();
    if (!win) {
        SKIP("no GL context available");
    }
    RendererDesc rd;
    rd.samples = 4;
    rd.shadowSize = 256;
    rd.clearColor = {0.0f, 0.0f, 0.0f};
    auto rr = Renderer::create(rd);
    REQUIRE(rr.has_value());
    auto& r = **rr;
    const glm::dvec3 target{120.0, 45.0, -80.0}; // far from the world origin
    Camera cam;
    cam.lookAt(target + glm::dvec3(0, 0, 3), target);
    cam.setAspect(1.0);
    auto brightness = [&](bool withLine) {
        r.beginFrame(128, 128, cam, 0.0);
        if (withLine) {
            // A thick white cross through the look-at point: it must land on the screen centre.
            r.linesNoDepth().segment(target - glm::dvec3(0.5, 0, 0), target + glm::dvec3(0.5, 0, 0),
                                     {1, 1, 1, 1}, 12.0f);
            r.linesNoDepth().segment(target - glm::dvec3(0, 0.5, 0), target + glm::dvec3(0, 0.5, 0),
                                     {1, 1, 1, 1}, 12.0f);
        }
        r.endFrame();
        const auto out = std::filesystem::temp_directory_path() / "qxl_overlay_test.png";
        REQUIRE(r.screenshot(out).has_value());
        int w = 0, h = 0, n = 0;
        unsigned char* px = stbi_load(out.string().c_str(), &w, &h, &n, 4);
        REQUIRE(px != nullptr);
        REQUIRE(w == 128);
        long sum = 0; // 8x8 block at the centre
        for (int y = h / 2 - 4; y < h / 2 + 4; ++y)
            for (int x = w / 2 - 4; x < w / 2 + 4; ++x)
                sum += px[(y * w + x) * 4];
        stbi_image_free(px);
        return sum / 64;
    };
    const long dark = brightness(false);
    const long lit = brightness(true);
    REQUIRE(dark < 40);
    REQUIRE(lit > 180); // the cross covers the centre block
}

TEST_CASE(
    "asynchronous pick returns the id and the surface point two frames later without a stall") {
    auto win = makeHidden();
    if (!win) {
        SKIP("no GL context available");
    }
    RendererDesc rd;
    rd.samples = 4;
    rd.shadowSize = 256;
    auto rr = Renderer::create(rd);
    REQUIRE(rr.has_value());
    auto& r = **rr;
    Mesh box(shapes::box({1, 1, 1}));
    Camera cam;
    cam.lookAt({0, 0, 4}, {0, 0, 0});
    cam.setAspect(320.0 / 240.0);
    cam.setClip(0.1, 50.0);
    Material m = Material::preset("copper");
    auto frame = [&] {
        r.beginFrame(320, 240, cam, 0.0);
        r.submit(box, m, glm::mat4(1.0f), ComponentId{7});
        r.endFrame();
    };
    // Nothing queued: nothing to poll.
    frame();
    CHECK_FALSE(r.pollPick().has_value());
    // Queued before frame N, issued by frame N, readable after frame N+1 (spec 18 §5 pass 11).
    r.queuePick(160, 120);
    frame();
    CHECK_FALSE(r.pollPick().has_value()); // the slot the GPU may still be writing is never read
    frame();
    auto hit = r.pollPick();
    REQUIRE(hit.has_value());
    CHECK(hit->id.value == 7u);
    CHECK(hit->hit);
    CHECK(hit->x == 160);
    CHECK(hit->y == 120);
    // The centre pixel looks straight down -z at the box's front face z = +0.5.
    CHECK(std::abs(hit->world.z - 0.5) < 0.01);
    CHECK(std::abs(hit->world.x) < 0.02);
    CHECK(std::abs(hit->world.y) < 0.02);
    CHECK_FALSE(r.pollPick().has_value()); // consumed
    // Background: an id of 0 and no hit.
    r.queuePick(3, 3);
    frame();
    frame();
    auto miss = r.pollPick();
    REQUIRE(miss.has_value());
    CHECK(miss->id.value == 0u);
    CHECK_FALSE(miss->hit);
}
