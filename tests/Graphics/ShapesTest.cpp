#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Graphics/Camera.hpp"
#include "Graphics/Colormap.hpp"
#include "Graphics/Mesh.hpp"
#include <cmath>
using namespace qlab::gfx;

static void checkMesh(const MeshData& m) {
    REQUIRE(!m.vertices.empty());
    REQUIRE(m.indices.size() % 3 == 0);
    for (auto i : m.indices) REQUIRE(i < m.vertices.size());
    for (auto& v : m.vertices) REQUIRE(glm::length(v.normal) == Catch::Approx(1.0).margin(1e-4));
}
TEST_CASE("procedural shapes are well-formed with unit normals") {
    checkMesh(shapes::box({1, 2, 3}));
    checkMesh(shapes::sphere(0.5f));
    checkMesh(shapes::cylinder(0.2f, 1.0f));
    checkMesh(shapes::cone(0.2f, 1.0f));
    checkMesh(shapes::torus(1.0f, 0.2f));
    checkMesh(shapes::tube({{0, 0, 0}, {0, 1, 0}, {1, 2, 0}, {1, 3, 1}}, 0.05f));
    checkMesh(shapes::extrudePolygon({{0, 0}, {1, 0}, {1, 1}, {0, 1}}, 0.1f));
    checkMesh(shapes::extrudePath(shapes::meanderPath(4, 0.2f, 0.5f, 0.3f), 0.02f, 0.001f));
    checkMesh(shapes::plateWithHoles({1, 1}, 0.05f, {{0.2f, 0.2f, 0.05f}}));
    auto b = shapes::box({1, 2, 3}).bounds();
    REQUIRE(b.min.x == Catch::Approx(-0.5)); REQUIRE(b.max.y == Catch::Approx(1.0)); REQUIRE(b.max.z == Catch::Approx(1.5));
    REQUIRE(shapes::sphere(1.0f, 16, 8).triangleCount() == 16 * 8 * 2);
}
TEST_CASE("meander path has expected extent") {
    auto p = shapes::meanderPath(3, 0.1f, 0.2f);
    REQUIRE(p.back().x == Catch::Approx(0.3f));
    REQUIRE(p.front().x == Catch::Approx(0.0f));
}
TEST_CASE("camera framing puts the AABB inside the frustum") {
    Camera cam; cam.setAspect(1.5);
    Aabb box{{-1, -2, -3}, {1, 2, 3}};
    cam.frame(box);
    REQUIRE(cam.frustum().intersects(box));
    for (int i = 0; i < 8; ++i) {
        glm::dvec3 c{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z};
        glm::dvec2 px;
        REQUIRE(cam.project(c, 1500, 1000, px));
        REQUIRE(px.x >= 0); REQUIRE(px.x <= 1500); REQUIRE(px.y >= 0); REQUIRE(px.y <= 1000);
    }
    // far-away box is culled
    REQUIRE_FALSE(cam.frustum().intersects(Aabb{{1000, 1000, 1000}, {1001, 1001, 1001}}));
}
TEST_CASE("camera ray through the viewport centre hits the target") {
    Camera cam; cam.setAspect(1.0); cam.lookAt({0, 0, 5}, {0, 0, 0});
    Ray r = cam.ray(500, 500, 1000, 1000);
    REQUIRE(glm::length(glm::cross(r.dir, glm::dvec3(0, 0, -1))) < 1e-6);
    // transition
    cam.transitionTo(Bookmark{"b", {5, 0, 0}, {0, 0, 0}}, 1.0);
    REQUIRE(cam.transitioning());
    for (int i = 0; i < 100; ++i) cam.update(0.011);
    REQUIRE_FALSE(cam.transitioning());
    REQUIRE(cam.position().x == Catch::Approx(5.0).margin(1e-9));
    // viewRel has zero translation
    glm::mat4 v = cam.viewRel();
    REQUIRE(std::abs(v[3][0]) < 1e-6); REQUIRE(std::abs(v[3][1]) < 1e-6); REQUIRE(std::abs(v[3][2]) < 1e-6);
}
TEST_CASE("colormap endpoints and cyclic twilight") {
    auto& v = Colormap::get(ColormapId::Viridis);
    auto c0 = v.sample(0.f), c1 = v.sample(1.f);
    REQUIRE(c0.r == Catch::Approx(0.267004f)); REQUIRE(c1.g == Catch::Approx(0.906157f));
    auto& t = Colormap::get(ColormapId::Twilight);
    REQUIRE(t.cyclic());
    REQUIRE(glm::length(t.sample(0.f) - t.sample(1.f)) < 1e-6f);
    REQUIRE(glm::length(t.sample(0.25f) - t.sample(1.25f)) < 1e-6f);
    REQUIRE(t.lut(256).size() == 256);
    REQUIRE(Colormap::byName("twilight") == ColormapId::Twilight);
}

// Counter-clockwise winding seen from outside: the geometric normal (p1-p0)x(p2-p0) of every
// triangle must point the same way as its vertex normals, otherwise back-face culling draws the
// shape inside-out. Reported by the Lab module, which had to re-orient every generated mesh.
static int misWound(const MeshData& m) {
    int bad = 0;
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const auto& a = m.vertices[m.indices[t]];
        const auto& b = m.vertices[m.indices[t + 1]];
        const auto& c = m.vertices[m.indices[t + 2]];
        const glm::vec3 g = glm::cross(b.position - a.position, c.position - a.position);
        if (glm::length(g) < 1e-12f) continue; // degenerate (sphere poles)
        if (glm::dot(g, a.normal + b.normal + c.normal) < 0.0f) ++bad;
    }
    return bad;
}
TEST_CASE("procedural shapes wind counter-clockwise against their normals") {
    CHECK(misWound(shapes::box({1, 2, 3})) == 0);
    CHECK(misWound(shapes::sphere(0.5f)) == 0);
    CHECK(misWound(shapes::cylinder(0.2f, 1.0f)) == 0);
    CHECK(misWound(shapes::cylinder(0.2f, 1.0f, 24, false)) == 0);
    CHECK(misWound(shapes::cone(0.2f, 1.0f)) == 0);
    CHECK(misWound(shapes::torus(1.0f, 0.2f)) == 0);
    CHECK(misWound(shapes::tube({{0, 0, 0}, {0, 1, 0}, {1, 2, 0}, {1, 3, 1}}, 0.05f)) == 0);
    CHECK(misWound(shapes::tube({{0, 0, 0}, {0, 1, 0}}, 0.05f, 8, false)) == 0);
    CHECK(misWound(shapes::extrudePolygon({{0, 0}, {1, 0}, {1, 1}, {0, 1}}, 0.1f)) == 0);
    CHECK(misWound(shapes::extrudePolygon({{0, 0}, {0, 1}, {1, 1}, {1, 0}}, 0.1f)) == 0); // clockwise input
    CHECK(misWound(shapes::extrudePath(shapes::meanderPath(4, 0.2f, 0.5f, 0.3f), 0.02f, 0.001f)) == 0);
    CHECK(misWound(shapes::plateWithHoles({1, 1}, 0.05f, {{0.2f, 0.2f, 0.05f}})) == 0);
    CHECK(misWound(shapes::grid(1.0f, 4, 0.01f)) == 0);
}
