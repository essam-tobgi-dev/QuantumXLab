// Spec 21 §1.2 — GlCanvas: per-view framebuffers fed by one shared gfx::Renderer, exact display
// colours through the tone map, dirty tracking. Skipped when no GL context is available.
#include "Viz/GlCanvas.hpp"
#include "Graphics/GlLoader.hpp" // test-only pixel read-back; src/Viz never calls OpenGL itself
#include "Graphics/Window.hpp"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace qlab;
using namespace qlab::viz;

namespace {
std::unique_ptr<gfx::Window> hiddenWindow() {
    gfx::WindowDesc d;
    d.visible = false;
    d.width = 320;
    d.height = 240;
    d.vsync = false;
    auto w = gfx::Window::create(d);
    return w ? std::move(*w) : nullptr;
}

std::array<int, 3> pixel(const GlCanvas& canvas, int x, int yFromTop) {
    std::array<unsigned char, 4> px{};
    canvas.framebuffer().bind(GL_READ_FRAMEBUFFER);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, canvas.height() - 1 - yFromTop, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    gfx::Framebuffer::bindDefault(GL_READ_FRAMEBUFFER);
    return {px[0], px[1], px[2]};
}

bool near(const std::array<int, 3>& got, glm::vec3 want, int tol) {
    for (int k = 0; k < 3; ++k)
        if (std::abs(got[static_cast<std::size_t>(k)] -
                     static_cast<int>(std::lround(want[k] * 255.0f))) > tol)
            return false;
    return true;
}

// A flat-coloured sphere filling the centre of the view.
GlCanvas::SceneFn ball(glm::vec3 display) {
    return [display](gfx::Renderer& r, GlBackend& gl) {
        gfx::Material m;
        m.unlit = true;
        m.baseColor = GlBackend::exact(display);
        gfx::SubmitFlags f;
        f.castShadow = f.receiveShadow = false;
        f.noPick = true;
        r.submit(gl.sphere(), m, glm::mat4(1.0f), ComponentId{0}, f);
    };
}
} // namespace

TEST_CASE("canvases keep their own image while sharing one renderer; colours are exact") {
    auto window = hiddenWindow();
    if (!window)
        SKIP("no GL context available");
    GlBackendDesc desc;
    desc.background = glm::vec3(0.086f, 0.102f, 0.125f); // bg.panel
    auto backend = GlBackend::create(desc);
    REQUIRE(backend.has_value());
    GlBackend& gl = **backend;

    // The baked key light comes from (0.35, 0.75, 0.55): look along it so the centre pixel is fully
    // lit.
    const glm::dvec3 eye = glm::normalize(glm::dvec3(0.35, 0.75, 0.55)) * 4.0;
    const glm::vec3 phaseZero(0.186f, 0.078f, 0.223f), accent(0.310f, 0.639f, 1.0f);
    GlCanvas a, b;
    a.camera().lookAt(eye, {0, 0, 0});
    b.camera().lookAt(eye, {0, 0, 0});
    auto ra = a.render(gl, 200, 160, ball(phaseZero));
    REQUIRE(ra.has_value());
    CHECK(*ra);
    auto rb =
        b.render(gl, 120, 120, ball(accent)); // a different size: the shared targets follow it
    REQUIRE(rb.has_value());
    CHECK(a.width() == 200);
    CHECK(b.height() == 120);
    CHECK(a.textureId() != b.textureId());

    // Exact display colours through ACES + gamma (±2/255), and the panel colour behind them.
    CHECK(near(pixel(b, 60, 60), accent, 2));
    CHECK(near(pixel(b, 2, 2), desc.background, 2));
    // Canvas A was rendered first and must still hold its own picture.
    CHECK(near(pixel(a, 100, 80), phaseZero, 2));
    CHECK(near(pixel(a, 2, 2), desc.background, 2));

    // A clean canvas of unchanged size is not rendered again; invalidate() or a resize is.
    CHECK_FALSE(*a.render(gl, 200, 160, ball(accent)));
    CHECK(near(pixel(a, 100, 80), phaseZero, 2));
    a.invalidate();
    CHECK(*a.render(gl, 200, 160, ball(accent)));
    CHECK(near(pixel(a, 100, 80), accent, 2));
    CHECK(*a.render(gl, 100, 80, ball(accent)));
    CHECK(a.width() == 100);
}

TEST_CASE("alignY maps the unit y axis onto a segment") {
    const glm::dvec3 from(1.0, 2.0, 3.0), to(1.0, 2.0, 7.0);
    const glm::mat4 m = alignY(from, to, 0.25);
    const glm::vec3 base = glm::vec3(m * glm::vec4(0, 0, 0, 1)),
                    tip = glm::vec3(m * glm::vec4(0, 1, 0, 1));
    CHECK(glm::length(base - glm::vec3(from)) < 1e-6f);
    CHECK(glm::length(tip - glm::vec3(to)) < 1e-6f);
    const glm::vec3 side = glm::vec3(m * glm::vec4(1, 0, 0, 0));
    CHECK(std::abs(glm::length(side) - 0.25f) < 1e-6f);
    CHECK(std::abs(glm::dot(side, glm::vec3(to - from))) < 1e-6f);
    CHECK(glm::determinant(glm::mat3(m)) > 0.0f); // a rotation and scale, no mirror
}
