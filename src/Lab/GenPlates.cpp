// Spec 17 §6 — Plate(r, t, holes, through_hole, …) and PlateWithHoles (chip ground plane).
#include "Lab/Generators.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace qlab::lab::gen {

using gfx::MeshData;
namespace {
const glm::vec4 kHole{0.05f, 0.05f, 0.05f, 1.0f};
const glm::vec4 kTrace{1.0f, 0.82f, 0.45f, 1.0f};
const glm::vec4 kBoard{0.06f, 0.32f, 0.14f, 1.0f};
const glm::vec4 kGap{0.10f, 0.10f, 0.12f, 1.0f};
const glm::vec4 kRecess{0.62f, 0.60f, 0.56f, 1.0f};
const glm::vec4 kDimple{0.30f, 0.29f, 0.27f, 1.0f};
const glm::vec4 kGlass{0.55f, 0.75f, 0.95f, 1.0f};
} // namespace

// Stage plates (spec 17 §3.1, fridge detail pass): disc of radius r and thickness t centred on the
// origin (axis +Y) with a 1 mm chamfer on both rims, an optional real through-hole (`through_hole_m`
// [x, z, r]: the still pumping line), and at full detail the bolt circle of M6 counterbores near
// the rim, radial rows of M4 tapped holes (shallow dimples), four loom pass-through slots and, on
// the RT plate, the KF-40 ports and the viewport ring. The simple level is a plain disc.
Result<MeshData> plate(const GenParams& p, const GenContext& c) {
    float r = c.F(p.length("r", 0.2)), t = c.F(p.length("t", 0.012));
    bool square = p.string("shape") == "square";
    bool full = c.detail == Detail::Full;
    MeshData m;
    if (square) {
        if (!full) return gfx::shapes::box({2.0f * r, t, 2.0f * r});
        // PCB: board plus launcher traces on the top face, a darker pocket where the chip sits
        float tt = 0.08f * t;
        // vertex-coloured: the board is FR-4 green and the launcher traces are gold
        mesh::appendColored(m, mesh::box({0, -0.5f * tt, 0}, {2.0f * r, t - tt, 2.0f * r}), kBoard);
        for (int side = 0; side < 4; ++side)
            for (int k = 0; k < 4; ++k) {
                float along = 2.0f * r * (0.2f + 0.2f * static_cast<float>(k)) - r;
                glm::vec3 center = side % 2 == 0 ? glm::vec3(along, 0.5f * t - 0.5f * tt, (side == 0 ? 1.0f : -1.0f) * 0.7f * r)
                                                 : glm::vec3((side == 1 ? 1.0f : -1.0f) * 0.7f * r, 0.5f * t - 0.5f * tt, along);
                glm::vec3 size = side % 2 == 0 ? glm::vec3(0.04f * r, tt, 0.55f * r) : glm::vec3(0.55f * r, tt, 0.04f * r);
                mesh::appendColored(m, mesh::box(center, size), kTrace);
            }
        return m;
    }
    // KF-40 pumping ports stand on the top face (RT plate); they are part of the envelope at
    // every level, the ring flange and bore only at full detail.
    const int ports = p.integer("kf_ports", 0);
    const float portH = c.F(0.045);
    auto portAt = [&](int k) {
        float a = static_cast<float>(p.number("start_angle_deg", 0.0)) * glm::pi<float>() / 180.0f + glm::radians(55.0f + 25.0f * static_cast<float>(k));
        return glm::vec3{0.62f * r * std::cos(a), 0.5f * t, 0.62f * r * std::sin(a)};
    };
    if (!full) {
        mesh::append(m, mesh::wall(r, -0.5f * t, 0.5f * t, 24, true));
        mesh::append(m, mesh::disc(r, 0.5f * t, 24, true));
        mesh::append(m, mesh::disc(r, -0.5f * t, 24, false));
        for (int k = 0; k < ports; ++k)
            mesh::append(m, gfx::shapes::cylinder(c.F(0.0225), portH, 12, true), mesh::translate(portAt(k) + glm::vec3(0, 0.5f * portH, 0)));
        return m;
    }
    const int seg = 96;
    const float chamfer = std::min(c.F(kPlateChamfer_m), 0.3f * t);
    float hx = 0.0f, hz = 0.0f, hr = 0.0f;
    if (auto hole = p.point3("through_hole")) {
        hx = static_cast<float>(hole->x * c.unitScale);
        hz = static_cast<float>(hole->y * c.unitScale);
        hr = static_cast<float>(hole->z * c.unitScale);
    }
    std::vector<float> angles;
    mesh::append(m, mesh::plateFace(r - chamfer, 0.5f * t, hx, hz, hr, seg, true, angles));
    mesh::append(m, mesh::plateRim(r, 0.5f * t, -0.5f * t, chamfer, angles));
    std::vector<float> bottomAngles;
    mesh::append(m, mesh::plateFace(r - chamfer, -0.5f * t, hx, hz, hr, seg, false, bottomAngles));
    if (hr > 0.0f) mesh::append(m, mesh::wall(hr, -0.5f * t, 0.5f * t, 32, false), mesh::translate({hx, 0.0f, hz}));

    // ---- surface features, drawn just proud of the faces so they never z-fight with the plate
    const float lift = c.F(0.00015);
    auto marker = [&](float x, float z, float radius, const glm::vec4& color, bool both) {
        mesh::appendColored(m, mesh::disc(radius, 0.5f * t + lift, 12, true), color, mesh::translate({x, 0.0f, z}));
        if (both) mesh::appendColored(m, mesh::disc(radius, -0.5f * t - lift, 12, false), color, mesh::translate({x, 0.0f, z}));
    };
    auto inHole = [&](float x, float z, float margin) {
        return hr > 0.0f && std::hypot(x - hx, z - hz) < hr + margin;
    };
    std::string holes = p.string("holes");
    const float start = static_cast<float>(p.number("start_angle_deg", 0.0)) * glm::pi<float>() / 180.0f;
    if (holes == "bolt_circle") { // M6 counterbores on the bolt circle: Ø 6.6 clearance in an Ø 11 recess
        int count = p.integer("hole_count", plateBoltCount(r / c.unitScale));
        float ring = c.F(p.length("hole_ring", plateBoltRing_m(r / c.unitScale)));
        for (int k = 0; k < count; ++k) {
            float a = start + glm::two_pi<float>() * static_cast<float>(k) / static_cast<float>(count);
            float x = ring * std::cos(a), z = ring * std::sin(a);
            if (inHole(x, z, c.F(0.006))) continue;
            mesh::appendColored(m, mesh::disc(c.F(0.0055), 0.5f * t + lift, 16, true), kRecess, mesh::translate({x, 0.0f, z}));
            marker(x, z, c.F(0.0033), kHole, true);
        }
    } else if (holes == "feedthrough_ring") { // SMA bulkhead clearance holes (count/radius from routing.json)
        int count = p.integer("hole_count", 96);
        float ring = c.F(p.length("hole_ring", 0.8 * r / c.unitScale));
        for (int k = 0; k < count; ++k) {
            float a = start + glm::two_pi<float>() * static_cast<float>(k) / static_cast<float>(count);
            marker(ring * std::cos(a), ring * std::sin(a), c.F(0.0032), kHole, true);
        }
    }
    if (p.boolean("tapped_rows", true)) { // radial rows of M4 tapped holes (shallow dimples, top face)
        int rows = std::max(2, p.integer("tapped_row_count", 8));
        for (int row = 0; row < rows; ++row) {
            float a = start + glm::two_pi<float>() * (static_cast<float>(row) + 0.5f) / static_cast<float>(rows);
            for (int k = 0; k < 5; ++k) {
                float rad = r * (0.30f + 0.11f * static_cast<float>(k));
                float x = rad * std::cos(a), z = rad * std::sin(a);
                if (!inHole(x, z, c.F(0.004))) marker(x, z, c.F(0.0018), kDimple, false);
            }
        }
    }
    int slots = p.integer("loom_slots", 4); // rectangular pass-through slots for looms and straps
    for (int k = 0; k < slots; ++k) {
        float a = start + glm::two_pi<float>() * (static_cast<float>(k) + 0.25f) / static_cast<float>(slots);
        float rad = 0.62f * r, x = rad * std::cos(a), z = rad * std::sin(a);
        if (inHole(x, z, c.F(0.04))) continue;
        MeshData slot = mesh::box({0.0f, 0.0f, 0.0f}, {c.F(0.012), t + 2.0f * lift, c.F(0.05)});
        glm::mat4 xf = glm::rotate(mesh::translate({x, 0.0f, z}), -a, {0.0f, 1.0f, 0.0f});
        mesh::appendColored(m, std::move(slot), kHole, xf);
    }
    for (int k = 0; k < ports; ++k) { // KF-40: tube, clamp-ring flange and the dark bore
        glm::vec3 at = portAt(k);
        mesh::append(m, gfx::shapes::cylinder(c.F(0.0225), portH, 32, true), mesh::translate(at + glm::vec3(0, 0.5f * portH, 0)));
        mesh::append(m, gfx::shapes::cylinder(c.F(0.0275), c.F(0.006), 32, true), mesh::translate(at + glm::vec3(0, portH - c.F(0.003), 0)));
        mesh::appendColored(m, mesh::disc(c.F(0.020), portH + lift, 24, true), kHole, mesh::translate(at));
    }
    if (p.boolean("viewport", false)) { // viewport ring with its glass window (RT plate), in the
        // readout sector where nothing else stands on the plate
        float a = start + glm::radians(270.0f);
        glm::vec3 at{0.45f * r * std::cos(a), 0.5f * t, 0.45f * r * std::sin(a)};
        float h = c.F(0.012);
        mesh::append(m, mesh::wall(c.F(0.045), 0.0f, h, 32, true), mesh::translate(at));
        mesh::append(m, mesh::annulus(c.F(0.030), c.F(0.045), h, 32, true), mesh::translate(at));
        mesh::append(m, mesh::wall(c.F(0.030), lift, h, 32, false), mesh::translate(at));
        mesh::appendColored(m, mesh::disc(c.F(0.030), 0.5f * h, 32, true), kGlass, mesh::translate(at));
    }
    return m;
}

// Chip ground plane: a metal film of size w × d and thickness t (local y from 0 to t) with square
// pockets around the listed holes ([x, z, r] in output units). The film top is decomposed into
// rectangles so the pockets expose the substrate.
Result<MeshData> plateWithHoles(const GenParams& p, const GenContext& c) {
    float w = c.F(p.length("w", 0.008)), d = c.F(p.length("d", 0.008)), t = c.F(p.length("t", kFilmGround_m));
    MeshData m;
    if (c.detail == Detail::Simple) return mesh::box({0, 0.5f * t, 0}, {w, t, d});
    struct Hole { float x0, x1, z0, z1; };
    std::vector<Hole> holes;
    std::vector<float> xs{-0.5f * w, 0.5f * w}, zs{-0.5f * d, 0.5f * d};
    for (const auto& h : p.points3("holes")) {
        auto x = static_cast<float>(h.x * c.unitScale), z = static_cast<float>(h.y * c.unitScale);
        auto r = static_cast<float>(h.z * c.unitScale);
        Hole hole{std::max(-0.5f * w, x - r), std::min(0.5f * w, x + r), std::max(-0.5f * d, z - r), std::min(0.5f * d, z + r)};
        if (hole.x1 <= hole.x0 || hole.z1 <= hole.z0) continue;
        holes.push_back(hole);
        xs.insert(xs.end(), {hole.x0, hole.x1});
        zs.insert(zs.end(), {hole.z0, hole.z1});
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(zs.begin(), zs.end());
    zs.erase(std::unique(zs.begin(), zs.end()), zs.end());
    const glm::vec3 up{0, 1, 0};
    auto quad = [&](float x0, float x1, float z0, float z1) {
        auto b = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({{x0, t, z0}, up, {0, 0}});
        m.vertices.push_back({{x1, t, z0}, up, {1, 0}});
        m.vertices.push_back({{x1, t, z1}, up, {1, 1}});
        m.vertices.push_back({{x0, t, z1}, up, {0, 1}});
        m.indices.insert(m.indices.end(), {b, b + 2, b + 1, b, b + 3, b + 2});
    };
    for (std::size_t j = 0; j + 1 < zs.size(); ++j) {
        float zc = 0.5f * (zs[j] + zs[j + 1]);
        std::size_t runStart = xs.size();
        for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
            float xc = 0.5f * (xs[i] + xs[i + 1]);
            bool solid = std::none_of(holes.begin(), holes.end(), [&](const Hole& h) {
                return xc > h.x0 && xc < h.x1 && zc > h.z0 && zc < h.z1;
            });
            if (solid && runStart == xs.size()) runStart = i;
            if (!solid && runStart != xs.size()) {
                quad(xs[runStart], xs[i], zs[j], zs[j + 1]);
                runStart = xs.size();
            }
        }
        if (runStart != xs.size()) quad(xs[runStart], xs.back(), zs[j], zs[j + 1]);
    }
    // outer edge walls give the film its thickness at the chip boundary
    mesh::append(m, mesh::box({0, 0.5f * t, 0.5f * d - 0.25f * t}, {w, t, 0.5f * t}));
    mesh::append(m, mesh::box({0, 0.5f * t, -0.5f * d + 0.25f * t}, {w, t, 0.5f * t}));
    mesh::append(m, mesh::box({0.5f * w - 0.25f * t, 0.5f * t, 0}, {0.5f * t, t, d}));
    mesh::append(m, mesh::box({-0.5f * w + 0.25f * t, 0.5f * t, 0}, {0.5f * t, t, d}));
    // pocket floors: dark substrate-coloured squares just above the substrate
    for (const auto& h : holes)
        mesh::appendColored(m, mesh::box({0.5f * (h.x0 + h.x1), 0.05f * t, 0.5f * (h.z0 + h.z1)}, {h.x1 - h.x0, 0.1f * t, h.z1 - h.z0}), kGap);
    return m;
}

} // namespace qlab::lab::gen
