// Spec 18 §5 (amended) — post-chain oracles: SSAO in a crease, soft shadow penumbra, bloom halo,
// and the GPU cost of the whole chain at 3200×2000 (reported; the budget is 3.5 ms on the M4).
#include "TestImage.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
using namespace gfxtest;

namespace {
glm::mat4 at(glm::vec3 p) { return glm::translate(glm::mat4(1.0f), p); }
Material matte(glm::vec3 c) { Material m; m.baseColor = glm::vec4(c, 1.0f); m.metallic = 0.0f; m.roughness = 0.8f; return m; }
} // namespace

TEST_CASE("SSAO: the crease where a box meets a plane is occluded, the open plane is not") {
    auto win = makeHidden();
    if (!win) { SKIP("no GL context available"); }
    RendererDesc rd; rd.samples = 4; rd.shadowSize = 256; rd.ibl = true; rd.ssao = true; rd.bloom = false;
    auto rr = Renderer::create(rd);
    REQUIRE(rr.has_value());
    auto& r = **rr;
    Mesh plane(shapes::box({4.0f, 0.02f, 4.0f})), box(shapes::box({1, 1, 1}));
    Camera cam; cam.lookAt({2.2, 1.3, 0.0}, {0.5, 0.05, 0.0}); cam.setAspect(320.0 / 240.0); cam.setClip(0.1, 20.0);
    const int W = 320, H = 240;
    for (int i = 0; i < 2; ++i) {
        r.beginFrame(W, H, cam, 0.0);
        r.submit(plane, matte({0.7f, 0.7f, 0.7f}), at({0, -0.01f, 0}), ComponentId{1});
        r.submit(box, matte({0.7f, 0.7f, 0.7f}), at({0, 0.5f, 0}), ComponentId{2});
        r.endFrame();
    }
    const Camera& c = r.camera();
    auto ao = readTexture(r.aoTexture(), GL_RED, 1);   // row 0 at the BOTTOM (GL)
    auto aoAt = [&](glm::dvec3 world) {
        glm::dvec2 px;
        REQUIRE(c.project(world, W, H, px));
        REQUIRE(px.x >= 0); REQUIRE(px.x < W); REQUIRE(px.y >= 0); REQUIRE(px.y < H);
        const int x = static_cast<int>(px.x), y = H - 1 - static_cast<int>(px.y);
        return ao[static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x)];
    };
    std::vector<float> crease, open;
    for (int i = -4; i <= 4; ++i) {
        const double z = 0.08 * i;
        crease.push_back(aoAt({0.505, 0.0, z}));   // the crease pixel: on the plane, 5 mm from the box
        crease.push_back(aoAt({0.5, 0.005, z}));   // the crease pixel: on the box face, 5 mm up
        open.push_back(aoAt({1.2, 0.0, z}));       // open plane, 0.7 m from the box (radius ≈ 0.1 m)
    }
    auto sc = stats(crease), so = stats(open);
    INFO("crease " << sc.mean << " open " << so.mean);
    CHECK(sc.mean < 0.85);
    CHECK(so.mean > 0.97);
}

TEST_CASE("soft shadow: the penumbra of a box on a plane spans at least 3 pixels on each edge") {
    auto win = makeHidden();
    if (!win) { SKIP("no GL context available"); }
    RendererDesc rd; rd.samples = 4; rd.shadowSize = 256; rd.ibl = false; rd.ssao = false; rd.bloom = false;
    rd.clearColor = {0, 0, 0};
    auto rr = Renderer::create(rd);
    REQUIRE(rr.has_value());
    auto& r = **rr;
    r.setAmbient({0, 0, 0});
    r.setSun({-0.4f, -1.0f, -0.3f}, {1, 1, 1}, 3.0f);
    Mesh plane(shapes::box({4.0f, 0.02f, 4.0f})), box(shapes::box({1, 1, 1}));
    // Top-down orthographic view, 3 m tall over 240 px: 80 px per metre.
    Camera cam; cam.lookAt({0, 5, 0}, {0, 0, 0}, {0, 0, -1}); cam.setOrtho(true);
    Bookmark b = cam.bookmark(); b.ortho = true; b.orthoHeight = 3.0; cam.set(b);
    cam.setAspect(320.0 / 240.0); cam.setClip(0.1, 20.0);
    const int W = 320, H = 240;
    r.beginFrame(W, H, cam, 0.0);
    r.submit(plane, matte({0.5f, 0.5f, 0.5f}), at({0, -0.01f, 0}), ComponentId{1});
    r.submit(box, matte({0.5f, 0.5f, 0.5f}), at({0, 0.5f, 0}), ComponentId{2});
    r.endFrame();
    LumImage img = grab(r, "qxl_soft_shadow.png");
    REQUIRE(img.w == W);
    // The sun travels along (−0.4, −1, −0.3), so the shadow falls toward −x, −z. A row at z = −0.65
    // lies outside the box footprint (|z| ≤ 0.5) but inside its shadow, so it crosses
    // plane → shadow → plane with no box pixels.
    glm::dvec2 p0;
    REQUIRE(r.camera().project({0.0, 0.0, -0.65}, W, H, p0));
    const int row = static_cast<int>(p0.y);
    std::vector<float> line;
    for (int x = 0; x < W; ++x) line.push_back(img.at(x, row));
    float lo = 1.0f, hi = 0.0f; int imin = 0;
    for (int x = 0; x < W; ++x) { if (line[static_cast<std::size_t>(x)] < lo) { lo = line[static_cast<std::size_t>(x)]; imin = x; } hi = std::max(hi, line[static_cast<std::size_t>(x)]); }
    REQUIRE(hi - lo > 0.2f);   // there is a shadow in the row
    const float a = lo + 0.1f * (hi - lo), bnd = lo + 0.9f * (hi - lo);
    int left = 0, right = 0;
    for (int x = 0; x < W; ++x) {
        const float v = line[static_cast<std::size_t>(x)];
        if (v > a && v < bnd) (x < imin ? left : right)++;
    }
    INFO("penumbra pixels left " << left << " right " << right << " (lit " << hi << ", shadow " << lo << ")");
    CHECK(left >= 3);
    CHECK(right >= 3);
}

TEST_CASE("bloom: an unlit emissive sphere brightens pixels 6–10 px outside its silhouette") {
    auto win = makeHidden();
    if (!win) { SKIP("no GL context available"); }
    auto render = [&](bool bloom) {
        RendererDesc rd; rd.samples = 4; rd.shadowSize = 256; rd.ibl = false; rd.ssao = false; rd.bloom = bloom;
        rd.clearColor = {0, 0, 0};
        auto rr = Renderer::create(rd);
        REQUIRE(rr.has_value());
        auto& r = **rr;
        Mesh sphere(shapes::sphere(0.5f, 48, 24));
        Material m; m.baseColor = {0.1f, 1.0f, 0.3f, 1}; m.emissive = {0.1f, 1.0f, 0.3f}; m.emissiveStrength = 6.0f; m.unlit = true;
        Camera cam; cam.lookAt({0, 0, 4}, {0, 0, 0}); cam.setAspect(320.0 / 240.0);
        r.beginFrame(320, 240, cam, 0.0);
        r.submit(sphere, m, glm::mat4(1.0f), ComponentId{1});
        r.endFrame();
        return grab(r, bloom ? "qxl_bloom_on.png" : "qxl_bloom_off.png");
    };
    LumImage on = render(true), off = render(false);
    REQUIRE(on.w == 320); REQUIRE(off.w == 320);
    // silhouette radius: 0.5 / 4 / tan(22.5°) · 120 px ≈ 36 px; measure the halo just outside it
    int radius = 0;
    for (int x = 160; x < 320; ++x) { if (off.at(x, 120) < 0.05f) { radius = x - 160; break; } }
    REQUIRE(radius > 20); REQUIRE(radius < 60);
    double halo = 0; int n = 0;
    for (int d = 6; d <= 10; ++d) {
        halo += on.at(160 + radius + d, 120) - off.at(160 + radius + d, 120); ++n;
        halo += on.at(160 - radius - d, 120) - off.at(160 - radius - d, 120); ++n;
        halo += on.at(160, 120 + radius + d) - off.at(160, 120 + radius + d); ++n;
        halo += on.at(160, 120 - radius - d) - off.at(160, 120 - radius - d); ++n;
    }
    INFO("silhouette radius " << radius << " px, mean halo gain " << halo / n);
    CHECK(halo / n > 0.02);
    // A plain lit white wall must not bloom: the rendered background is untouched far away.
    CHECK(std::abs(on.at(10, 10) - off.at(10, 10)) < 0.01f);
}

TEST_CASE("post chain GPU time at 3200x2000 (SSAO + blur + bloom + outline + tonemap)") {
    auto win = makeHidden();
    if (!win) { SKIP("no GL context available"); }
    RendererDesc rd; rd.samples = 4; rd.shadowSize = 4096;
    auto rr = Renderer::create(rd);
    REQUIRE(rr.has_value());
    auto& r = **rr;
    Mesh box(shapes::box({0.2f, 0.2f, 0.2f})), plane(shapes::box({8, 0.02f, 8}));
    std::vector<InstanceData> inst;
    for (int i = 0; i < 20; ++i) for (int j = 0; j < 20; ++j)
        inst.push_back({at({i * 0.35f - 3.5f, 0.1f, j * 0.35f - 3.5f}), {1000u + static_cast<unsigned>(i * 20 + j), 0u}, {}, {1, 1, 1, 1}});
    Camera cam; cam.lookAt({5, 4, 5}, {0, 0, 0}); cam.setAspect(1.6); cam.setClip(0.1, 50.0);
    const int W = 3200, H = 2000;
    double aoMs = 0, bloomMs = 0, postMs = 0, resMs = 0; int n = 0;
    for (int f = 0; f < 12; ++f) {
        r.beginFrame(W, H, cam, 0.0);
        r.submit(plane, Material::preset("aluminium"), at({0, -0.01f, 0}), ComponentId{1});
        r.submitInstanced(box, Material::preset("gold"), inst);
        r.setSelection(ComponentId{1005}, ComponentId{0});
        r.endFrame();
        glFinish();
        if (f >= 4) { aoMs += r.stats().aoMs; bloomMs += r.stats().bloomMs; postMs += r.stats().postMs; resMs += r.stats().resolveMs; ++n; }
    }
    aoMs /= n; bloomMs /= n; postMs /= n; resMs /= n;
    // Apple's GL does not account glBlitFramebuffer in GL_TIME_ELAPSED (resolveMs reads 0), so the
    // composite segment carries the MSAA resolve as well; the chain number below includes it.
    std::printf("[post-chain @ %dx%d, selection outline on] ssao+blur %.2f ms, bloom %.2f ms, resolve+composite+outline+tonemap %.2f ms, "
                "total %.2f ms (GPU, mean of %d frames)\n", W, H, aoMs, bloomMs, postMs - aoMs - bloomMs - resMs, postMs, n);
    CHECK(postMs > 0.0);          // the timer queries deliver
    CHECK(postMs < 8.0);          // loose bound: the 3.5 ms budget is reported, not asserted, on a shared machine
    CHECK(aoMs + bloomMs <= postMs + 0.05);
}
