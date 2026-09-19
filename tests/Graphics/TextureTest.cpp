// Spec 18 §4 (textures, amended 2026-09-18) — texture oracles: albedo × baseColor and sRGB decode
// on a textured quad, normal mapping through the derivative tangent frame, seam-free triplanar
// blending on a sphere, neutral fallbacks for missing maps, one bind per texture-set batch, and
// the shipped sets of Assets/Textures.
#include "Core/Paths.hpp"
#include "TestImage.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>
using namespace gfxtest;
using Catch::Approx;

namespace {
constexpr int W = 320, H = 240;
constexpr float kTol = 2.0f / 255.0f;

// A 2 × 2 quad in the xy plane facing +z; u grows with +x, v with +y (the tangent frame oracle).
MeshData quadXY() {
    MeshData m;
    m.vertices = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}},
                  {{1, -1, 0}, {0, 0, 1}, {1, 0}},
                  {{1, 1, 0}, {0, 0, 1}, {1, 1}},
                  {{-1, 1, 0}, {0, 0, 1}, {0, 1}}};
    m.indices = {0, 1, 2, 0, 2, 3};
    return m;
}
template <class F> ImageRgba8 pattern(int n, F f) { // f(x, y) → (r, g, b) bytes
    ImageRgba8 img;
    img.width = img.height = n;
    img.rgba.resize(static_cast<std::size_t>(n) * static_cast<std::size_t>(n) * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            auto c = f(x, y);
            std::uint8_t* p = &img.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(n) +
                                         static_cast<std::size_t>(x)) *
                                        4];
            p[0] = c.x;
            p[1] = c.y;
            p[2] = c.z;
            p[3] = 255;
        }
    return img;
}
// Tangent-space normal tilted by `deg` about the v axis (positive: towards +u), encoded 0–255.
glm::u8vec3 tilted(float deg) {
    const float a = glm::radians(deg);
    return {static_cast<std::uint8_t>(std::lround((std::sin(a) * 0.5f + 0.5f) * 255)), 128,
            static_cast<std::uint8_t>(std::lround((std::cos(a) * 0.5f + 0.5f) * 255))};
}
float cmax(glm::vec3 v) {
    return std::max({v.x, v.y, v.z});
}
float cmin(glm::vec3 v) {
    return std::min({v.x, v.y, v.z});
}
float srgbToLinear(int v) {
    const float c = v / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

struct Rig {
    std::unique_ptr<Window> win;
    std::unique_ptr<Renderer> r;
    Camera cam;
    bool ok() const { return win && r; }
    explicit Rig(bool ibl = false) {
        win = makeHidden(W, H);
        if (!win)
            return;
        RendererDesc rd;
        rd.samples = 4;
        rd.shadowSize = 256;
        rd.ibl = ibl;
        rd.ssao = false;
        rd.bloom = false;
        auto rr = Renderer::create(rd);
        if (rr)
            r = std::move(*rr);
        cam.lookAt({0, 0, 3}, {0, 0, 0});
        cam.setAspect(static_cast<double>(W) / H);
        cam.setClip(0.1, 20.0);
        if (r) {
            r->setAmbient(glm::vec3(0.0f));
            r->setSun({-1.0f, 0.0f, -0.6f}, {1, 1, 1}, 3.0f);
        }
    }
    // Renders one item and returns the resolved HDR image (RGBA float, GL rows: 0 at the bottom).
    std::vector<float> render(const Mesh& mesh, const Material& m) {
        r->beginFrame(W, H, cam, 0.0);
        r->submit(mesh, m, glm::mat4(1.0f), ComponentId{1});
        r->endFrame();
        return readTexture(r->hdrTexture(), GL_RGBA, 4);
    }
};
glm::vec3 hdrAt(const std::vector<float>& px, int x, int y) { // y as pick(): 0 at the top
    const std::size_t i =
        (static_cast<std::size_t>(H - 1 - y) * W + static_cast<std::size_t>(x)) * 4;
    return {px[i], px[i + 1], px[i + 2]};
}
float maxDiff(const std::vector<float>& a, const std::vector<float>& b, int x0, int y0, int x1,
              int y1) {
    float d = 0.0f;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            d = std::max(d, cmax(glm::abs(hdrAt(a, x, y) - hdrAt(b, x, y))));
    return d;
}
} // namespace

TEST_CASE("textured quad: albedo x baseColor at the centre, sRGB decode, mean normalisation, "
          "neutral fallback, one bind") {
    Rig rig;
    if (!rig.ok()) {
        SKIP("no GL context available");
    }
    Mesh quad(quadXY());
    TextureLibrary& lib = rig.r->textures();
    CHECK(lib.maxAnisotropy() >= 1.0f);
    TextureSetSource solid;
    solid.albedo = ImageRgba8::solid(200, 90, 40, 8, 8);
    lib.add("solid", solid);
    Material m;
    m.unlit = true;
    m.baseColor = {0.5f, 1.0f, 0.25f, 1.0f};
    m.textureSet = "solid";
    m.normalizeMaps = false;
    auto px = rig.render(quad, m);
    const glm::vec3 expect = glm::vec3(srgbToLinear(200), srgbToLinear(90), srgbToLinear(40)) *
                             glm::vec3(0.5f, 1.0f, 0.25f);
    const glm::vec3 got = hdrAt(px, W / 2, H / 2);
    CHECK(got.r == Approx(expect.r).margin(kTol));
    CHECK(got.g == Approx(expect.g).margin(kTol));
    CHECK(got.b == Approx(expect.b).margin(kTol));
    CHECK(rig.r->stats().textureBinds == 1);
    // sRGB: a 128-grey albedo decodes to linear 0.216 (not 0.502); normalised it is the mean → 1.
    TextureSetSource grey;
    grey.albedo = ImageRgba8::solid(128, 128, 128, 8, 8);
    lib.add("grey", grey);
    m.baseColor = glm::vec4(1.0f);
    m.textureSet = "grey";
    CHECK(hdrAt(rig.render(quad, m), W / 2, H / 2).g == Approx(0.2159f).margin(0.004f));
    CHECK(lib.get("grey").albedoMean.g == Approx(0.2159f).margin(0.002f));
    m.normalizeMaps = true;
    CHECK(hdrAt(rig.render(quad, m), W / 2, H / 2).g == Approx(1.0f).margin(0.01f));
    // Missing maps fall back to neutral texels: a lit material with an albedo-only set (white) and
    // one with an unknown set both render exactly as the untextured material.
    TextureSetSource albedoOnly;
    albedoOnly.albedo = ImageRgba8::solid(255, 255, 255, 4, 4);
    lib.add("albedo_only", albedoOnly);
    CHECK_FALSE(lib.get("albedo_only").hasNormal);
    CHECK_FALSE(lib.get("albedo_only").hasRoughness);
    CHECK_FALSE(lib.get("no_such_set").hasAlbedo);
    Material lit;
    lit.baseColor = {0.7f, 0.6f, 0.5f, 1.0f};
    lit.metallic = 0.3f;
    lit.roughness = 0.5f;
    auto plain = rig.render(quad, lit);
    lit.textureSet = "albedo_only";
    lit.normalizeMaps = false;
    CHECK(maxDiff(plain, rig.render(quad, lit), W / 2 - 30, H / 2 - 30, W / 2 + 30, H / 2 + 30) <
          kTol);
    lit.textureSet = "no_such_set";
    CHECK(maxDiff(plain, rig.render(quad, lit), W / 2 - 30, H / 2 - 30, W / 2 + 30, H / 2 + 30) <
          kTol);
    CHECK(hdrAt(plain, W / 2, H / 2).r > 0.05f); // the quad is lit, not black
}

TEST_CASE("normal mapping: the derivative tangent frame tilts shading with the map, flat without") {
    Rig rig;
    if (!rig.ok()) {
        SKIP("no GL context available");
    }
    Mesh quad(quadXY());
    TextureLibrary& lib = rig.r->textures();
    // The sun travels along (-1, 0, -0.6): light arrives from +x. A normal tilted towards +u
    // (= +x on this quad) faces it more, one tilted towards -u faces it less.
    TextureSetSource flat, plus, minus, stripes;
    flat.normal = ImageRgba8::solid(128, 128, 255, 4, 4);
    plus.normal = pattern(4, [](int, int) { return tilted(30.0f); });
    minus.normal = pattern(4, [](int, int) { return tilted(-30.0f); });
    stripes.normal =
        pattern(64, [](int x, int) { return tilted((x / 8) % 2 == 0 ? 30.0f : -30.0f); });
    lib.add("flat", flat);
    lib.add("plus", plus);
    lib.add("minus", minus);
    lib.add("stripes", stripes);
    Material m;
    m.baseColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    m.metallic = 0.0f;
    m.roughness = 0.6f;
    m.normalStrength = 1.0f;
    auto lum = [&](const char* set) {
        m.textureSet = set;
        auto px = rig.render(quad, m);
        std::vector<float> v;
        for (int y = H / 2 - 40; y < H / 2 + 40; ++y)
            for (int x = W / 2 - 40; x < W / 2 + 40; ++x)
                v.push_back(hdrAt(px, x, y).g);
        return stats(v);
    };
    Material bare = m;
    auto untextured = rig.render(quad, bare);
    const Stats f = lum("flat"), p = lum("plus"), n = lum("minus"), s = lum("stripes");
    // Flat map = untextured shading; N·L = 0.51 flat, 0.88 tilted +30°, 0.02 tilted −30°.
    m.textureSet = "flat";
    CHECK(maxDiff(untextured, rig.render(quad, m), W / 2 - 40, H / 2 - 40, W / 2 + 40, H / 2 + 40) <
          kTol);
    CHECK(p.mean > f.mean * 1.4);
    CHECK(n.mean < f.mean * 0.25);
    CHECK(f.stddev < 0.003); // a flat map shades the quad uniformly
    CHECK(s.stddev > 0.05);  // the stripes modulate it (≈ ±0.3 of the mean)
    // Strength 0 disables the map whatever it holds.
    m.textureSet = "stripes";
    m.normalStrength = 0.0f;
    CHECK(maxDiff(untextured, rig.render(quad, m), W / 2 - 40, H / 2 - 40, W / 2 + 40, H / 2 + 40) <
          kTol);
}

TEST_CASE("triplanar: a sphere blends the three projections without seams and a flat map keeps the "
          "geometric normal") {
    Rig rig;
    if (!rig.ok()) {
        SKIP("no GL context available");
    }
    Mesh sphere(shapes::sphere(1.0f, 64, 32));
    TextureLibrary& lib = rig.r->textures();
    TextureSetSource smooth; // a continuous, tileable luminance pattern
    smooth.albedo = pattern(64, [](int x, int y) {
        const float v =
            0.5f + 0.4f * std::sin(x / 64.0f * 6.2831853f) * std::sin(y / 64.0f * 6.2831853f);
        const auto b = static_cast<std::uint8_t>(std::lround(v * 255));
        return glm::u8vec3(b, b, b);
    });
    lib.add("smooth", smooth);
    Material m;
    m.unlit = true;
    m.baseColor = glm::vec4(1.0f);
    m.textureSet = "smooth";
    m.triplanar = true;
    m.uvScale = 1.5f;
    m.normalizeMaps = false;
    auto px = rig.render(sphere, m);
    glm::dvec2 c, e;
    REQUIRE(rig.r->camera().project({0, 0, 0}, W, H, c));
    REQUIRE(rig.r->camera().project({0, 1, 0}, W, H, e));
    const double rIn = 0.9 * glm::distance(c, e); // inside the silhouette
    std::vector<float> inside;
    float worst = 0.0f;
    for (int y = 1; y < H - 1; ++y)
        for (int x = 1; x < W - 1; ++x) {
            if (glm::distance(glm::dvec2(x, y), c) > rIn)
                continue;
            const float v = hdrAt(px, x, y).g;
            inside.push_back(v);
            worst = std::max(
                {worst, std::abs(v - hdrAt(px, x, y + 1).g), std::abs(v - hdrAt(px, x + 1, y).g)});
        }
    REQUIRE(inside.size() > 5000);
    CHECK(stats(inside).stddev > 0.05); // the pattern is visible (not the neutral fallback)
    // The pattern's own gradient is ≤ 0.03 per pixel here; a projection seam would be a step ≥ 0.2.
    CHECK(worst < 0.08f);
    // Flat normal map + white maps through the triplanar path = the untextured lit sphere.
    Material lit;
    lit.baseColor = {0.9f, 0.75f, 0.4f, 1.0f};
    lit.metallic = 1.0f;
    lit.roughness = 0.4f;
    auto plain = rig.render(sphere, lit);
    TextureSetSource flat;
    flat.normal = ImageRgba8::solid(128, 128, 255, 4, 4);
    flat.albedo = ImageRgba8::solid(255, 255, 255, 4, 4);
    lib.add("flat_tri", flat);
    lit.textureSet = "flat_tri";
    lit.triplanar = true;
    lit.uvScale = 3.0f;
    lit.normalizeMaps = false;
    auto tex = rig.render(sphere, lit);
    float d = 0.0f; // relative inside the specular peak (HDR values reach 6), absolute elsewhere
    for (int y = 1; y < H - 1; ++y)
        for (int x = 1; x < W - 1; ++x)
            if (glm::distance(glm::dvec2(x, y), c) <= rIn)
                d = std::max(d, cmax(glm::abs(hdrAt(plain, x, y) - hdrAt(tex, x, y))) /
                                    std::max(1.0f, cmax(hdrAt(plain, x, y))));
    CHECK(d < kTol);
}

TEST_CASE("batching: equal texture sets bind once per pass; the shipped sets load with albedo, "
          "normal and roughness") {
    Rig rig;
    if (!rig.ok()) {
        SKIP("no GL context available");
    }
    Mesh box(shapes::box({0.5f, 0.5f, 0.5f}));
    TextureLibrary& lib = rig.r->textures();
    TextureSetSource a, b;
    a.albedo = ImageRgba8::solid(200, 200, 200, 4, 4);
    b.albedo = ImageRgba8::solid(100, 100, 100, 4, 4);
    lib.add("set_a", a);
    lib.add("set_b", b);
    Material ma, mb, plain;
    ma.textureSet = "set_a";
    mb.textureSet = "set_b";
    rig.r->beginFrame(W, H, rig.cam, 0.0);
    for (int i = 0; i < 6; ++i) // a, b, plain, a, b, plain … sorted into two runs
        rig.r->submit(box,
                      i % 3 == 0   ? ma
                      : i % 3 == 1 ? mb
                                   : plain,
                      glm::translate(glm::mat4(1.0f), {i * 0.6f - 1.5f, 0, 0}),
                      ComponentId{static_cast<unsigned>(i + 1)});
    rig.r->endFrame();
    CHECK(rig.r->stats().textureBinds == 2);
    CHECK(rig.r->stats().drawCalls >= 6);
    // Assets/Textures: ≤ 12 sets, each ≤ 1.5 MB with albedo + normal + roughness, 1K, mean in
    // range.
    const auto root = core::assetDir() / "Textures";
    int sets = 0;
    for (const auto& e : std::filesystem::directory_iterator(root)) {
        if (!e.is_directory())
            continue;
        const std::string name = e.path().filename().string();
        if (name.starts_with('_') || name.starts_with('.'))
            continue; // tool caches are not sets
        INFO(name);
        REQUIRE(lib.existsOnDisk(name));
        ++sets;
        std::uintmax_t bytes = 0;
        for (const auto& f : std::filesystem::directory_iterator(e.path()))
            if (f.path().extension() == ".jpg")
                bytes += f.file_size();
        CHECK(bytes <= 1'500'000u);
        const TextureSet& set = lib.get(name);
        CHECK(set.hasAlbedo);
        CHECK(set.hasNormal);
        CHECK(set.hasRoughness);
        CHECK(set.albedo.width() >= 512);
        CHECK(set.albedo.width() <= 1024);
        CHECK(set.normal.width() == set.albedo.width());
        CHECK(cmin(set.albedoMean) > 0.01f);
        CHECK(cmax(set.albedoMean) < 0.98f);
        CHECK(set.roughnessMean > 0.02f);
        CHECK(set.roughnessMean < 0.98f);
    }
    CHECK(sets >= 10);
    CHECK(sets <= 12);
    CHECK_FALSE(TextureLibrary::loadImage(root / "nothing.jpg").has_value());
}
