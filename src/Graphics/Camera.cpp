#include "Graphics/Camera.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace qlab::gfx {

void Aabb::expand(const glm::dvec3& p) {
    min = glm::min(min, p);
    max = glm::max(max, p);
}
Aabb Aabb::transformed(const glm::dmat4& m) const {
    Aabb r{glm::dvec3(1e300), glm::dvec3(-1e300)};
    for (int i = 0; i < 8; ++i) {
        glm::dvec3 c{(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z};
        r.expand(glm::dvec3(m * glm::dvec4(c, 1.0)));
    }
    return r;
}
bool Frustum::intersects(const Aabb& box) const {
    for (const auto& p : planes) {
        glm::dvec3 v{p.x >= 0 ? box.max.x : box.min.x, p.y >= 0 ? box.max.y : box.min.y,
                     p.z >= 0 ? box.max.z : box.min.z};
        if (glm::dot(glm::dvec3(p), v) + p.w < 0)
            return false;
    }
    return true;
}

Camera::Camera() = default;

void Camera::lookAt(const glm::dvec3& pos, const glm::dvec3& tgt, const glm::dvec3& up) {
    pos_ = pos;
    target_ = tgt;
    up_ = glm::normalize(up);
}
void Camera::set(const Bookmark& b) {
    lookAt(b.position, b.target, b.up);
    fovDeg_ = b.fovDeg;
    ortho_ = b.ortho;
    orthoHeight_ = b.orthoHeight;
    animT_ = animDur_ = 0.0;
    animT_ = 1.0;
}
Bookmark Camera::bookmark(const std::string& name) const {
    return Bookmark{name, pos_, target_, up_, fovDeg_, ortho_, orthoHeight_};
}
glm::dvec3 Camera::right() const {
    glm::dvec3 f = forward();
    glm::dvec3 r = glm::cross(f, up_);
    double l = glm::length(r);
    if (l < 1e-12)
        r = glm::cross(f, glm::dvec3(1, 0, 0)), l = glm::length(r);
    return r / l;
}

void Camera::orbit(double dxPx, double dyPx) {
    const double kRadPerPx = 0.005;
    glm::dvec3 offset = pos_ - target_;
    double r = glm::length(offset);
    if (r < 1e-12)
        return;
    // spherical coords around up_ (assume up_ = +y for the orbit pole; general up handled by basis)
    glm::dvec3 u = up_;
    glm::dvec3 f = -offset / r;
    glm::dvec3 rt = glm::normalize(glm::cross(f, u));
    // yaw about up
    glm::dmat4 yaw = glm::rotate(glm::dmat4(1.0), -dxPx * kRadPerPx, u);
    offset = glm::dvec3(yaw * glm::dvec4(offset, 0.0));
    // pitch about right, clamped near the poles
    double elev = std::asin(std::clamp(glm::dot(glm::normalize(offset), u), -1.0, 1.0));
    double dElev = dyPx * kRadPerPx;
    double maxElev = glm::radians(89.0);
    dElev = std::clamp(elev + dElev, -maxElev, maxElev) - elev;
    glm::dmat4 pitch = glm::rotate(glm::dmat4(1.0), dElev, rt);
    offset = glm::dvec3(pitch * glm::dvec4(offset, 0.0));
    pos_ = target_ + offset;
}
void Camera::pan(double dxPx, double dyPx, int viewportH) {
    double h = viewportH > 0 ? viewportH : 1;
    double worldPerPx;
    if (ortho_)
        worldPerPx = orthoHeight_ / h;
    else
        worldPerPx = 2.0 * distance() * std::tan(glm::radians(fovDeg_) * 0.5) / h;
    glm::dvec3 r = right();
    glm::dvec3 u = glm::normalize(glm::cross(r, forward()));
    glm::dvec3 d = (-dxPx * r + dyPx * u) * worldPerPx;
    pos_ += d;
    target_ += d;
}
void Camera::dolly(double scrollSteps) {
    double f = std::pow(0.9, scrollSteps);
    if (ortho_) {
        orthoHeight_ = std::max(1e-9, orthoHeight_ * f);
        return;
    }
    double d = std::max(1e-6, distance() * f);
    pos_ = target_ - forward() * d;
}
void Camera::dollyToward(const glm::dvec3& anchor, double scrollSteps) {
    const double f = std::pow(0.9, scrollSteps);
    if (ortho_) {
        // Keep `anchor` fixed on screen: the view-plane offset from the target scales with the
        // height.
        glm::dvec3 d = target_ - anchor;
        glm::dvec3 fwd = forward();
        d -= fwd * glm::dot(d, fwd); // only the in-plane part moves the picture
        const glm::dvec3 shift = d * (f - 1.0);
        target_ += shift;
        pos_ += shift;
        orthoHeight_ = std::max(1e-9, orthoHeight_ * f);
        return;
    }
    // Scaling both the eye and the target about the anchor keeps the anchor's direction from the
    // eye unchanged, i.e. under the same pixel, and shortens every distance by `f`.
    const glm::dvec3 toEye = pos_ - anchor;
    if (glm::length(toEye) * f < 1e-7)
        return; // never fold the camera onto the anchor
    pos_ = anchor + toEye * f;
    target_ = anchor + (target_ - anchor) * f;
}
void Camera::setPivot(const glm::dvec3& point) {
    const glm::dvec3 fwd = forward();
    const double depth = glm::dot(point - pos_, fwd);
    if (depth <= 1e-9)
        return; // behind the eye: keep the current pivot
    target_ = pos_ + fwd * depth;
}
void Camera::cancelTransition() {
    animDur_ = 0.0;
    animT_ = 1.0;
}
void Camera::fly(const glm::dvec3& local, double dt, double speed) {
    glm::dvec3 f = forward(), r = right(), u = glm::normalize(glm::cross(r, f));
    glm::dvec3 d = (r * local.x + u * local.y + f * local.z) * (speed * dt);
    pos_ += d;
    target_ += d;
}
void Camera::rotateInPlace(double dxPx, double dyPx) {
    const double k = 0.003;
    glm::dvec3 f = forward();
    double dist = distance();
    glm::dmat4 yaw = glm::rotate(glm::dmat4(1.0), -dxPx * k, up_);
    f = glm::dvec3(yaw * glm::dvec4(f, 0.0));
    glm::dvec3 r = glm::normalize(glm::cross(f, up_));
    glm::dmat4 pitch = glm::rotate(glm::dmat4(1.0), -dyPx * k, r);
    glm::dvec3 f2 = glm::dvec3(pitch * glm::dvec4(f, 0.0));
    if (std::abs(glm::dot(glm::normalize(f2), up_)) < 0.995)
        f = f2;
    target_ = pos_ + f * dist;
}

void Camera::transitionTo(const Bookmark& b, double durationS) {
    from_ = bookmark();
    to_ = b;
    animDur_ = std::max(0.0, durationS);
    animT_ = 0.0;
    if (animDur_ == 0.0)
        set(b);
}
void Camera::update(double dt) {
    if (!transitioning())
        return;
    animT_ = std::min(animDur_, animT_ + dt);
    double s = animDur_ > 0 ? animT_ / animDur_ : 1.0;
    double e = s < 0.5 ? 4 * s * s * s : 1 - std::pow(-2 * s + 2, 3) / 2; // ease in-out cubic
    pos_ = glm::mix(from_.position, to_.position, e);
    target_ = glm::mix(from_.target, to_.target, e);
    up_ = glm::normalize(glm::mix(from_.up, to_.up, e));
    fovDeg_ = glm::mix(from_.fovDeg, to_.fovDeg, e);
    orthoHeight_ = std::exp(glm::mix(std::log(from_.orthoHeight), std::log(to_.orthoHeight), e));
    if (animT_ >= animDur_) {
        ortho_ = to_.ortho;
        animT_ = animDur_ = 0.0;
        animT_ = 1.0;
    }
}

void Camera::frame(const Aabb& box, double margin) {
    if (!box.valid())
        return;
    glm::dvec3 c = box.center();
    double r = std::max(box.radius(), 1e-9) * margin;
    glm::dvec3 f = forward();
    if (ortho_) {
        orthoHeight_ = 2.0 * r;
        target_ = c;
        pos_ = c - f * (r * 4.0);
    } else {
        double halfFov = glm::radians(fovDeg_) * 0.5;
        double fitFov = std::min(halfFov, std::atan(std::tan(halfFov) * aspect_));
        double d = r / std::sin(fitFov);
        target_ = c;
        pos_ = c - f * d;
    }
    near_ = std::max(1e-4 * r, distance() * 1e-4);
    far_ = std::max(far_, distance() + 4.0 * r);
}

glm::dmat4 Camera::viewMatrix() const {
    return glm::lookAt(pos_, target_, up_);
}
glm::mat4 Camera::viewRel() const {
    // Same rotation as viewMatrix but translation from the origin: subtract the camera position
    // in double before converting, so metre-scale scenes with micrometre features stay exact.
    return glm::mat4(glm::lookAt(glm::dvec3(0.0), target_ - pos_, up_));
}
glm::mat4 Camera::projMatrix() const {
    if (ortho_) {
        double hh = orthoHeight_ * 0.5, hw = hh * aspect_;
        return glm::mat4(glm::ortho(-hw, hw, -hh, hh, near_, far_));
    }
    return glm::mat4(glm::perspective(glm::radians(fovDeg_), aspect_, near_, far_));
}
Frustum Camera::frustum() const {
    glm::dmat4 m = viewProj();
    Frustum fr;
    // Gribb–Hartmann plane extraction (row-major access via m[col][row])
    auto row = [&](int r) { return glm::dvec4(m[0][r], m[1][r], m[2][r], m[3][r]); };
    glm::dvec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    glm::dvec4 p[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
    for (int i = 0; i < 6; ++i) {
        double l = glm::length(glm::dvec3(p[i]));
        fr.planes[i] = l > 0 ? p[i] / l : p[i];
    }
    return fr;
}
Ray Camera::ray(double px, double py, int w, int h) const {
    double nx = (2.0 * px / std::max(1, w)) - 1.0;
    double ny = 1.0 - (2.0 * py / std::max(1, h));
    glm::dmat4 inv = glm::inverse(viewProj());
    glm::dvec4 a = inv * glm::dvec4(nx, ny, -1.0, 1.0);
    glm::dvec4 b = inv * glm::dvec4(nx, ny, 1.0, 1.0);
    glm::dvec3 pa = glm::dvec3(a) / a.w, pb = glm::dvec3(b) / b.w;
    if (ortho_)
        return {pa, glm::normalize(pb - pa)};
    return {pos_, glm::normalize(pb - pa)};
}
glm::dvec3 Camera::unproject(double px, double py, double depth01, int w, int h) const {
    const double nx = (2.0 * px / std::max(1, w)) - 1.0;
    const double ny = 1.0 - (2.0 * py / std::max(1, h));
    const double nz = 2.0 * std::clamp(depth01, 0.0, 1.0) - 1.0;
    const glm::dvec4 v = glm::inverse(viewProj()) * glm::dvec4(nx, ny, nz, 1.0);
    return glm::dvec3(v) / v.w;
}
bool Camera::project(const glm::dvec3& p, int w, int h, glm::dvec2& out) const {
    glm::dvec4 c = viewProj() * glm::dvec4(p, 1.0);
    if (c.w <= 0)
        return false;
    out = {(c.x / c.w * 0.5 + 0.5) * w, (1.0 - (c.y / c.w * 0.5 + 0.5)) * h};
    return true;
}

} // namespace qlab::gfx
