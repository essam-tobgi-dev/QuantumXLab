// Spec 18 §4 (amended) — image-based lighting oracles: BRDF LUT limits, environment statistics,
// a metallic sphere lit by the environment alone, and the physically based metal presets.
#include "TestImage.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>
using namespace gfxtest;
using Catch::Approx;

TEST_CASE("BRDF LUT: A→1, B→0 at normal incidence and zero roughness; A+B bounded and monotone in roughness") {
    auto win = makeHidden();
    if (!win) { SKIP("no GL context available"); }
    RendererDesc rd; rd.shadowSize = 256;
    auto rr = Renderer::create(rd);
    REQUIRE(rr.has_value());
    const Texture2D& lut = (*rr)->brdfLut();
    REQUIRE(lut.width() == 128);
    auto px = readTexture(lut, GL_RG, 2);
    auto ab = [&](int x, int y) { const std::size_t i = (static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)) * 2; return glm::vec2(px[i], px[i + 1]); };
    // n·v = 1 (last column), roughness → 0 (first row): the Fresnel weight is all scale, no bias.
    glm::vec2 smooth = ab(127, 0);
    CHECK(smooth.x == Approx(1.0).margin(0.03));
    CHECK(smooth.y == Approx(0.0).margin(0.03));
    // n·v = 0.5, roughness 0.5: the directional albedo of a white-F0 conductor is in [0.5, 1].
    glm::vec2 mid = ab(64, 64);
    CHECK(mid.x + mid.y >= 0.5f);
    CHECK(mid.x + mid.y <= 1.0f);
    // Along n·v = 1, A + B is non-increasing in roughness (energy lost to the masking term).
    float prev = 2.0f;
    for (int y = 0; y < 128; ++y) {
        glm::vec2 v = ab(127, y);
        CHECK(v.x + v.y <= prev + 0.01f);
        prev = std::min(prev, v.x + v.y);
    }
}

TEST_CASE("environment: prefiltered mip 0 is structured, mip 5 is smooth within every face, +Y outshines -Y by 2x") {
    auto win = makeHidden();
    if (!win) { SKIP("no GL context available"); }
    RendererDesc rd; rd.shadowSize = 256;
    auto rr = Renderer::create(rd);
    REQUIRE(rr.has_value());
    const TextureCube& pre = (*rr)->prefilteredEnvironment();
    REQUIRE(pre.levels() == 6);
    REQUIRE(pre.size() == 256);
    auto m0 = stats(cubeLuminance(pre, 0));
    CHECK(m0.n == 6u * 256u * 256u);
    CHECK(m0.stddev > 0.5);                       // panels of radiance 12 against walls of 0.55
    // Mip 5 (roughness 1): the split-sum GGX lobe at α = 1 is exactly a cosine lobe (H is
    // cosine-distributed, L = 2(N·H)H − N is then uniform, weighted by N·L), so the level must
    // agree with the irradiance map (E/π) face by face, and be smooth: the second difference
    // along either axis stays below 8 % of the level mean (a gradient across a side face is
    // physical; a panel, a rack band or a seam is not — a hard step of size Δ shows up as a second
    // difference ≈ Δ, i.e. ≥ 25 % for a rack band). The lit-room environment (ceiling ≈ 1.4 with
    // its diffuser panels against a floor ≈ 0.3) convolves to a smooth curvature measured at
    // 5.0–5.3 % on the +Y and +Z faces, which is what the bound accommodates. It is NOT uniform across the cube — the
    // ceiling/floor asymmetry that the +Y/−Y oracle below demands survives any cosine
    // convolution (measured ≈ 0.3 of the mean; see SPEC_DEVIATIONS).
    const TextureCube& irr = (*rr)->irradianceMap();
    auto m5all = stats(cubeLuminance(pre, 5));
    for (int face = 0; face < 6; ++face) {
        auto m5 = stats(cubeLuminance(pre, 5, face)), mi = stats(cubeLuminance(irr, 0, face));
        INFO("face " << face << " mip5 mean " << m5.mean << " irradiance mean " << mi.mean);
        CHECK(m5.n == 8u * 8u);
        CHECK(std::abs(m5.mean - mi.mean) < 0.15 * mi.mean);
        auto l = cubeLuminance(pre, 5, face);
        auto L = [&](int x, int y) { return l[static_cast<std::size_t>(y * 8 + x)]; };
        for (int y = 1; y < 7; ++y)
            for (int x = 1; x < 7; ++x) {
                CHECK(std::abs(L(x - 1, y) - 2 * L(x, y) + L(x + 1, y)) < 0.08 * m5all.mean);
                CHECK(std::abs(L(x, y - 1) - 2 * L(x, y) + L(x, y + 1)) < 0.08 * m5all.mean);
            }
    }
    CHECK(m5all.stddev / m5all.mean < m0.stddev / m0.mean / 3.0);   // the whole cube smooths by ≥ 3×
    auto up = stats(cubeLuminance(pre, 0, 2)), down = stats(cubeLuminance(pre, 0, 3));
    CHECK(up.mean >= 2.0 * down.mean);
    // Irradiance map: E/π of a room whose radiance is O(1); the ceiling normal receives more.
    auto iu = stats(cubeLuminance(irr, 0, 2)), id = stats(cubeLuminance(irr, 0, 3));
    CHECK(iu.mean > id.mean);
    CHECK(iu.mean > 0.3); CHECK(iu.mean < 3.0);
}

TEST_CASE("IBL on a sphere: a polished metal reflects the room with the sun off; black when IBL is off") {
    auto win = makeHidden();
    if (!win) { SKIP("no GL context available"); }
    auto render = [&](bool ibl) {
        RendererDesc rd; rd.samples = 4; rd.shadowSize = 256; rd.clearColor = {0, 0, 0};
        rd.ibl = ibl; rd.ssao = false; rd.bloom = false;
        auto rr = Renderer::create(rd);
        REQUIRE(rr.has_value());
        auto& r = **rr;
        r.setSun({-0.4f, -1.0f, -0.3f}, {1, 1, 1}, 0.0f);   // sun OFF
        r.setAmbient({0, 0, 0});                              // no constant ambient either
        Mesh sphere(shapes::sphere(1.0f, 64, 32));
        Material m; m.baseColor = {0.95f, 0.95f, 0.95f, 1}; m.metallic = 1.0f; m.roughness = 0.1f;
        Camera cam; cam.lookAt({0, 0, 4}, {0, 0, 0}); cam.setAspect(1.0);
        r.beginFrame(256, 256, cam, 0.0);
        r.submit(sphere, m, glm::mat4(1.0f), ComponentId{1});
        r.endFrame();
        return grab(r, ibl ? "qxl_ibl_on.png" : "qxl_ibl_off.png");
    };
    LumImage on = render(true);
    REQUIRE(on.w == 256);
    // sphere disc: radius 1 at distance 4, fov 45° → 1/4/tan(22.5°) · 128 px ≈ 77 px
    const int cx = 128, cy = 128, rad = 70;
    std::vector<float> disc, top, bottom;
    for (int y = 0; y < on.h; ++y)
        for (int x = 0; x < on.w; ++x) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > rad * rad) continue;
            disc.push_back(on.at(x, y));
            if (dy < -rad / 2) top.push_back(on.at(x, y));
            if (dy > rad / 2) bottom.push_back(on.at(x, y));
        }
    auto sd = stats(disc);
    INFO("disc mean " << sd.mean << " stddev " << sd.stddev);
    CHECK(sd.stddev > 0.08);
    CHECK(stats(top).mean > stats(bottom).mean);
    LumImage off = render(false);
    REQUIRE(off.w == 256);
    std::vector<float> discOff;
    for (int y = 0; y < off.h; ++y)
        for (int x = 0; x < off.w; ++x)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= rad * rad) discOff.push_back(off.at(x, y));
    CHECK(stats(discOff).mean < 0.02);
}

TEST_CASE("Material presets carry measured F0 and the amended roughness table (spec 18 §4)") {
    auto gold = Material::preset("gold");
    CHECK(gold.baseColor.r == Approx(1.00f)); CHECK(gold.baseColor.g == Approx(0.71f)); CHECK(gold.baseColor.b == Approx(0.29f));
    CHECK(gold.roughness == Approx(0.28f)); CHECK(gold.metallic == Approx(1.0f));
    auto cu = Material::preset("copper");
    CHECK(cu.baseColor.r == Approx(0.95f)); CHECK(cu.baseColor.g == Approx(0.64f)); CHECK(cu.baseColor.b == Approx(0.54f));
    CHECK(cu.roughness == Approx(0.38f));
    auto al = Material::preset("aluminium");
    CHECK(al.baseColor.r == Approx(0.91f)); CHECK(al.baseColor.g == Approx(0.92f)); CHECK(al.baseColor.b == Approx(0.92f));
    CHECK(al.roughness == Approx(0.5f));
    auto ss = Material::preset("stainless");
    CHECK(ss.baseColor.r == Approx(0.56f)); CHECK(ss.baseColor.g == Approx(0.57f)); CHECK(ss.baseColor.b == Approx(0.58f));
    CHECK(ss.roughness == Approx(0.35f));
    auto nb = Material::preset("niobium");
    CHECK(nb.baseColor.r == Approx(0.66f)); CHECK(nb.baseColor.g == Approx(0.65f)); CHECK(nb.baseColor.b == Approx(0.68f));
}
