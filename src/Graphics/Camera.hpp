#pragma once
// Spec 18 §5 — camera: orbit/pan/zoom/fly, perspective + orthographic, double-precision
// position on the CPU (chip-scale micrometres next to metre-scale fridge), float `viewRel`
// upload with the camera position subtracted, eased transitions to bookmarks, AABB framing.
#include <glm/glm.hpp>
#include <string>

namespace qlab::gfx {

struct Aabb {
    glm::dvec3 min{0}, max{0};
    glm::dvec3 center() const { return 0.5 * (min + max); }
    double radius() const { return 0.5 * glm::length(max - min); }
    bool valid() const { return max.x >= min.x && max.y >= min.y && max.z >= min.z; }
    void expand(const glm::dvec3& p);
    Aabb transformed(const glm::dmat4& m) const;
};

struct Bookmark {
    std::string name;
    glm::dvec3 position{0, 0, 5};
    glm::dvec3 target{0, 0, 0};
    glm::dvec3 up{0, 1, 0};
    double fovDeg = 45.0;
    bool ortho = false;
    double orthoHeight = 2.0; // world units visible vertically in ortho mode
};

struct Ray { glm::dvec3 origin; glm::dvec3 dir; };

struct Frustum {
    glm::dvec4 planes[6]; // normalized (a,b,c,d): dot(n,p)+d >= 0 inside
    bool intersects(const Aabb& box) const;
};

class Camera {
public:
    Camera();
    // --- state
    const glm::dvec3& position() const { return pos_; }
    const glm::dvec3& target() const { return target_; }
    const glm::dvec3& up() const { return up_; }
    double fovDeg() const { return fovDeg_; }
    bool ortho() const { return ortho_; }
    double orthoHeight() const { return orthoHeight_; }
    double nearPlane() const { return near_; }
    double farPlane() const { return far_; }
    void setAspect(double a) { aspect_ = a > 0 ? a : 1.0; }
    double aspect() const { return aspect_; }
    void setClip(double n, double f) { near_ = n; far_ = f; }
    void setOrtho(bool o) { ortho_ = o; }
    void setFov(double deg) { fovDeg_ = deg; }
    void lookAt(const glm::dvec3& pos, const glm::dvec3& tgt, const glm::dvec3& up = {0, 1, 0});
    void set(const Bookmark& b);
    Bookmark bookmark(const std::string& name = "") const;

    // --- interaction (deltas in pixels unless noted)
    void orbit(double dxPx, double dyPx);          // rotate around target
    void pan(double dxPx, double dyPx, int viewportH); // translate target in view plane
    void dolly(double scrollSteps);                 // exponential distance change / ortho zoom
    // Zoom toward a world point: the point stays under the same pixel while the distance to it
    // shrinks by 0.9 per step (grows for negative steps). Ortho: zooms the height about it.
    void dollyToward(const glm::dvec3& anchor, double scrollSteps);
    // Orbit about a world point WITHOUT moving the view: the target slides along the current
    // viewing line to the depth of `point`, so a drag rotates around what was under the cursor.
    void setPivot(const glm::dvec3& point);
    void fly(const glm::dvec3& localMove, double dt, double speed); // fly mode translate
    void rotateInPlace(double dxPx, double dyPx);   // fly mode look

    // --- animation
    void transitionTo(const Bookmark& b, double durationS);
    void update(double dt);                         // advances the transition (ease in-out)
    bool transitioning() const { return animT_ < animDur_; }
    void cancelTransition();                        // spec 18 §6: any user input stops a flight where it is

    // --- framing
    void frame(const Aabb& box, double marginFactor = 1.15);

    // --- matrices
    glm::dmat4 viewMatrix() const;                  // absolute (double)
    glm::mat4 viewRel() const;                      // view with camera position at the origin (float upload)
    glm::mat4 projMatrix() const;
    glm::dmat4 viewProj() const { return glm::dmat4(projMatrix()) * viewMatrix(); }
    Frustum frustum() const;
    double distance() const { return glm::length(pos_ - target_); }
    glm::dvec3 forward() const { return glm::normalize(target_ - pos_); }
    glm::dvec3 right() const;

    // --- picking helpers: pixel (x right, y down) in a viewport of w×h
    Ray ray(double px, double py, int w, int h) const;
    // World-space → pixel; returns false if behind the camera.
    bool project(const glm::dvec3& p, int w, int h, glm::dvec2& out) const;
    // Pixel plus GL window depth (0 near … 1 far, as the depth buffer stores it) → world point.
    glm::dvec3 unproject(double px, double py, double depth01, int w, int h) const;

private:
    glm::dvec3 pos_{0, 1.5, 5}, target_{0, 0, 0}, up_{0, 1, 0};
    double fovDeg_ = 45.0, aspect_ = 16.0 / 9.0, near_ = 0.01, far_ = 200.0;
    bool ortho_ = false;
    double orthoHeight_ = 2.0;
    // transition
    Bookmark from_, to_;
    double animT_ = 1.0, animDur_ = 0.0;
};

} // namespace qlab::gfx
