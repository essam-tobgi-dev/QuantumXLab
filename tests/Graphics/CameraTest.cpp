// Spec 18 §6 — the orbit camera's navigation primitives, headless: zoom toward a point keeps that
// point under its pixel, re-pivoting keeps the view, unproject inverts project, and a flight can
// be cancelled where it is.
#include "Graphics/Camera.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace qlab::gfx;
using Catch::Approx;

namespace {
double depthOf(const Camera& cam, const glm::dvec3& p) {   // GL window depth of a world point
    const glm::dvec4 c = cam.viewProj() * glm::dvec4(p, 1.0);
    return (c.z / c.w) * 0.5 + 0.5;
}
} // namespace

TEST_CASE("Camera: dollyToward keeps the anchor under the same pixel and shortens the distance by 0.9 per step") {
    Camera cam;
    cam.lookAt({3.0, 2.0, 6.0}, {0.0, 1.0, 0.0});
    cam.setAspect(1.5);
    cam.setClip(0.01, 100.0);
    const glm::dvec3 anchor{0.7, 1.3, -0.4};
    glm::dvec2 before{}, after{};
    REQUIRE(cam.project(anchor, 1600, 1000, before));
    const double d0 = glm::length(cam.position() - anchor);
    cam.dollyToward(anchor, 3.0);
    REQUIRE(cam.project(anchor, 1600, 1000, after));
    CHECK(after.x == Approx(before.x).margin(1e-6));
    CHECK(after.y == Approx(before.y).margin(1e-6));
    CHECK(glm::length(cam.position() - anchor) == Approx(d0 * std::pow(0.9, 3.0)));
    // Zooming out is the inverse.
    cam.dollyToward(anchor, -3.0);
    CHECK(glm::length(cam.position() - anchor) == Approx(d0));
    // It never folds the eye onto the anchor.
    for (int i = 0; i < 2000; ++i) cam.dollyToward(anchor, 5.0);
    CHECK(glm::length(cam.position() - anchor) > 1e-8);
}

TEST_CASE("Camera: dollyToward in orthographic mode zooms about the anchor") {
    Camera cam;
    cam.lookAt({0.0, 10.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, -1.0});
    cam.setOrtho(true);
    cam.setAspect(1.0);
    cam.setClip(0.1, 100.0);
    const glm::dvec3 anchor{0.6, 0.0, 0.3};
    glm::dvec2 before{}, after{};
    REQUIRE(cam.project(anchor, 800, 800, before));
    const double h0 = cam.orthoHeight();
    cam.dollyToward(anchor, 2.0);
    REQUIRE(cam.project(anchor, 800, 800, after));
    CHECK(after.x == Approx(before.x).margin(1e-6));
    CHECK(after.y == Approx(before.y).margin(1e-6));
    CHECK(cam.orthoHeight() == Approx(h0 * 0.81));
}

TEST_CASE("Camera: setPivot moves the orbit centre along the viewing line without moving the view") {
    Camera cam;
    cam.lookAt({4.0, 3.0, 5.0}, {0.0, 0.0, 0.0});
    const glm::dvec3 pos = cam.position();
    const glm::dvec3 fwd = cam.forward();
    const glm::dvec3 point{1.0, 0.5, 1.0};   // off the line: only its depth is taken
    cam.setPivot(point);
    CHECK(cam.position() == pos);
    CHECK(glm::length(cam.forward() - fwd) < 1e-12);
    CHECK(cam.distance() == Approx(glm::dot(point - pos, fwd)));
    // A point behind the eye is ignored.
    const double d = cam.distance();
    cam.setPivot(pos - fwd * 2.0);
    CHECK(cam.distance() == Approx(d));
    // Orbiting now turns about the new pivot: its projection stays put.
    const glm::dvec3 pivot = cam.target();
    cam.orbit(80.0, 30.0);
    CHECK(glm::length(cam.target() - pivot) < 1e-12);
    CHECK(cam.distance() == Approx(d));
}

TEST_CASE("Camera: unproject inverts project at the stored depth") {
    Camera cam;
    cam.lookAt({2.0, 1.5, 4.0}, {0.0, 0.2, 0.0});
    cam.setAspect(1.6);
    cam.setClip(0.05, 60.0);
    for (const glm::dvec3 p : {glm::dvec3{0.3, 0.4, -0.2}, glm::dvec3{-1.0, 0.0, 1.0}, glm::dvec3{0.0, 2.0, -3.0}}) {
        glm::dvec2 px{};
        REQUIRE(cam.project(p, 1600, 1000, px));
        const glm::dvec3 back = cam.unproject(px.x, px.y, depthOf(cam, p), 1600, 1000);
        CHECK(glm::length(back - p) < 1e-7);
    }
    // Depth 1 is the far plane, depth 0 the near plane, along the pixel's ray.
    const glm::dvec3 far = cam.unproject(800, 500, 1.0, 1600, 1000);
    const glm::dvec3 near = cam.unproject(800, 500, 0.0, 1600, 1000);
    CHECK(glm::dot(far - cam.position(), cam.forward()) == Approx(60.0).epsilon(1e-4)); // float projection
    CHECK(glm::dot(near - cam.position(), cam.forward()) == Approx(0.05).epsilon(1e-6));
}

TEST_CASE("Camera: cancelTransition stops a flight where it is") {
    Camera cam;
    cam.lookAt({0.0, 0.0, 5.0}, {0.0, 0.0, 0.0});
    Bookmark b;
    b.position = {10.0, 0.0, 5.0};
    b.target = {10.0, 0.0, 0.0};
    cam.transitionTo(b, 1.0);
    cam.update(0.5);
    REQUIRE(cam.transitioning());
    const glm::dvec3 mid = cam.position();
    CHECK(mid.x > 0.5);
    CHECK(mid.x < 9.5);
    cam.cancelTransition();
    CHECK_FALSE(cam.transitioning());
    cam.update(1.0);
    CHECK(cam.position() == mid);
}
