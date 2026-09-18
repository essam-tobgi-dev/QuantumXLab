// Spec 17 §6 — Frame(w, d, h, profile) — T-slot extrusion with corner brackets, feet and the
// top-plate damper beams — the gantry variant, and ExtrudedU (cable tray with its bundle).
#include "Lab/Generators.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::lab::gen {

using gfx::MeshData;
namespace {
const glm::vec4 kKnob{0.30f, 0.30f, 0.32f, 1.0f};
const glm::vec4 kSteel{0.55f, 0.56f, 0.58f, 1.0f};
const glm::vec4 kRubber{0.08f, 0.08f, 0.08f, 1.0f};
const glm::vec4 kBracket{0.70f, 0.71f, 0.72f, 1.0f};
const glm::vec4 kCable{0.12f, 0.14f, 0.30f, 1.0f};

// Box-tube bar between two points, square profile `s` (axis-aligned bars only).
MeshData bar(glm::vec3 a, glm::vec3 b, float s) {
    glm::vec3 center = 0.5f * (a + b), size = glm::abs(b - a) + glm::vec3(s);
    return mesh::box(center, size);
}
} // namespace

Result<MeshData> frame(const GenParams& p, const GenContext& c) {
    MeshData m;
    float s = c.F(p.length("profile", 0.06));
    if (p.length("span")) { // gantry: two rails along z, cross beam, trolley, hoist cable and hook
        float span = c.F(p.length("span", 1.4)), travel = c.F(p.length("travel", 1.6));
        float H = c.F(kGantryHeight_m);
        float rail = std::min(s, 0.1f * H);
        if (c.detail == Detail::Simple) return mesh::box({0, 0, 0}, {span, H, travel});
        float yTop = 0.5f * H - 0.5f * rail;
        for (float x : {-0.5f * span + 0.5f * rail, 0.5f * span - 0.5f * rail})
            mesh::append(m, mesh::box({x, yTop, 0}, {rail, rail, travel}));
        mesh::append(m, mesh::box({0, yTop - rail, 0}, {span, rail, rail}));
        mesh::appendColored(m, mesh::box({0, yTop - 2.0f * rail, 0}, {3.0f * rail, rail, 2.0f * rail}), kKnob);
        float cableTop = yTop - 2.5f * rail, hook = -0.5f * H + 0.5f * rail;
        mesh::appendColored(m, gfx::shapes::cylinder(0.1f * rail, cableTop - hook, 8, true), kSteel,
                            mesh::translate({0, 0.5f * (cableTop + hook), 0}));
        mesh::appendColored(m, mesh::box({0, hook, 0}, {rail, rail, 0.4f * rail}), kKnob);
        return m;
    }
    float w = c.F(p.length("width", 1.4)), d = c.F(p.length("depth", 1.4)), h = c.F(p.length("height", 2.6));
    if (c.detail == Detail::Simple) {
        for (float x : {-1.0f, 1.0f})
            for (float z : {-1.0f, 1.0f})
                mesh::append(m, mesh::box({x * 0.5f * (w - s), 0, z * 0.5f * (d - s)}, {s, h, s}));
        return m;
    }
    // Aluminium T-slot extrusion (40 × 40, four grooves) at full detail (spec 17 §3.1 amended);
    // the box-tube look stays available with profile: "box".
    const bool tslot = p.string("profile", "t_slot") != "box";
    const std::vector<glm::vec2> prof = mesh::tSlotProfile(s, 0.2f * s, 0.3f * s, 0.05f * s);
    auto member = [&](glm::vec3 a, glm::vec3 b, float size) {
        if (tslot && std::abs(size - s) < 1e-6f) mesh::append(m, mesh::extrudeBetween(prof, a, b));
        else mesh::append(m, bar(a, b, size));
    };
    float hx = 0.5f * (w - s), hz = 0.5f * (d - s);
    const float feet = c.F(0.03); // rubber levelling feet under the four uprights
    for (float x : {-hx, hx})
        for (float z : {-hz, hz}) {
            member({x, -0.5f * h + feet, z}, {x, 0.5f * h, z}, s);
            mesh::appendColored(m, mesh::box({x, -0.5f * h + 0.5f * feet, z}, {1.4f * s, feet, 1.4f * s}), kRubber);
        }
    for (float y : {0.5f * h - 0.5f * s, 0.05f * h, -0.5f * h + feet + 0.5f * s}) { // top ring, mid brace, base
        member({-hx + 0.5f * s, y, -hz}, {hx - 0.5f * s, y, -hz}, s);
        member({-hx + 0.5f * s, y, hz}, {hx - 0.5f * s, y, hz}, s);
        member({-hx, y, -hz + 0.5f * s}, {-hx, y, hz - 0.5f * s}, s);
        member({hx, y, -hz + 0.5f * s}, {hx, y, hz - 0.5f * s}, s);
        for (float x : {-hx, hx}) // corner brackets: gusset plates on the inner faces of the joints
            for (float z : {-hz, hz})
                mesh::appendColored(m, mesh::box({x - std::copysign(0.55f * s, x), y, z - std::copysign(0.55f * s, z)},
                                                 {1.1f * s, 0.9f * s, 1.1f * s}), kBracket);
    }
    // Top-plate support: two cross beams under the plate's rim with a rubber isolation block on each
    // end, so the insert hangs on the dampers and not on the frame's resonances (spec 17 §3.1).
    float ySupport = c.F(p.length("support_y", 0.5 * h / c.unitScale - 0.06));
    float span = c.F(p.length("support_span", 0.5 * w / c.unitScale)), block = c.F(0.03);
    for (float z : {-0.5f * span, 0.5f * span}) {
        member({-hx, ySupport - 0.5f * s - block, z}, {hx, ySupport - 0.5f * s - block, z}, s);
        for (float x : {-0.55f * span, 0.55f * span})
            mesh::appendColored(m, mesh::box({x, ySupport - 0.5f * block, z}, {1.6f * s, block, 1.6f * s}), kRubber);
    }
    return m;
}

// U-channel cable tray swept along an axis-aligned polyline (`points_m`); straight 1 m along +x
// when no path is given. Profile width and height from `width`/`height` lengths.
Result<MeshData> extrudedU(const GenParams& p, const GenContext& c) {
    float W = c.F(p.length("width", kTrayWidth_m)), H = c.F(p.length("height", kTrayHeight_m));
    float t = std::max(0.03f * H, c.F(0.002));
    std::vector<glm::dvec3> pts = p.points3("points");
    if (pts.size() < 2) pts = {glm::dvec3(-0.5, 0, 0), glm::dvec3(0.5, 0, 0)};
    MeshData m;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        glm::vec3 a = glm::vec3(pts[i] * c.unitScale), b = glm::vec3(pts[i + 1] * c.unitScale);
        glm::vec3 dir = b - a;
        float len = glm::length(dir);
        if (len <= 0.0f) continue;
        dir /= len;
        bool alongX = std::abs(dir.x) >= std::abs(dir.z) && std::abs(dir.x) >= std::abs(dir.y);
        bool vertical = std::abs(dir.y) > std::abs(dir.x) && std::abs(dir.y) > std::abs(dir.z);
        glm::vec3 center = 0.5f * (a + b);
        // extend each run by half a profile so corners close
        float L = len + (vertical ? 0.0f : W);
        if (c.detail == Detail::Simple) { // box impostor of the whole channel profile
            mesh::append(m, mesh::box(center, alongX ? glm::vec3(L, H, W) : (vertical ? glm::vec3(W, len, H) : glm::vec3(W, H, L))));
            continue;
        }
        glm::vec3 floorSize = alongX ? glm::vec3(L, t, W) : (vertical ? glm::vec3(W, len, t) : glm::vec3(W, t, L));
        glm::vec3 floorAt = center + (vertical ? glm::vec3(0, 0, -0.5f * H + 0.5f * t) : glm::vec3(0, -0.5f * H + 0.5f * t, 0));
        mesh::append(m, mesh::box(floorAt, floorSize));
        // The cable bundle the tray carries: `cables` runs laid in the channel (spec 17 §2 amended).
        int cables = p.integer("cables", 0);
        for (int k = 0; k < cables && !vertical; ++k) {
            float rc = c.F(0.006);
            float lateral = (static_cast<float>(k % 6) - 2.5f) * 0.7f * (W - 2.0f * t) / 6.0f;
            float y = -0.5f * H + t + rc + 2.0f * rc * static_cast<float>(k / 6);
            glm::vec3 off = alongX ? glm::vec3(0, y, lateral) : glm::vec3(lateral, y, 0);
            glm::vec3 run = alongX ? glm::vec3(L - 2.0f * t, 0, 0) : glm::vec3(0, 0, L - 2.0f * t);
            mesh::appendColored(m, gfx::shapes::tube({center + off - 0.5f * run, center + off + 0.5f * run}, rc, 8, false), kCable);
        }
        for (float side : {-1.0f, 1.0f}) {
            glm::vec3 off = alongX ? glm::vec3(0, 0, side * (0.5f * W - 0.5f * t))
                                   : (vertical ? glm::vec3(side * (0.5f * W - 0.5f * t), 0, 0) : glm::vec3(side * (0.5f * W - 0.5f * t), 0, 0));
            glm::vec3 size = alongX ? glm::vec3(L, H, t) : (vertical ? glm::vec3(t, len, H) : glm::vec3(t, H, L));
            mesh::append(m, mesh::box(center + off, size));
        }
    }
    return m;
}

} // namespace qlab::lab::gen
