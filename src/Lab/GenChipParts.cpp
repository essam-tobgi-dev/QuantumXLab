// Spec 17 §6 — Junction(w_lead, overlap), SquidLoop(area, w), Airbridge(span, w, h), and the
// surface ion trap TrapChip(electrodes) of spec 17 §3.6.
#include "Lab/Generators.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::lab::gen {

using gfx::MeshData;
namespace {
const glm::vec4 kLead{0.80f, 0.82f, 0.88f, 1.0f};
const glm::vec4 kOxide{0.35f, 0.20f, 0.55f, 1.0f};
const glm::vec4 kBridge{0.90f, 0.90f, 0.93f, 1.0f};
const glm::vec4 kElectrode{1.0f, 0.80f, 0.40f, 1.0f};
const glm::vec4 kTrapBody{0.25f, 0.27f, 0.30f, 1.0f};
} // namespace

// Two perpendicular leads of width w_lead overlapping by `overlap` with the tunnel-barrier dot
// between them; envelope (10.5 w + overlap) square, two lead thicknesses tall.
Result<MeshData> junction(const GenParams& p, const GenContext& c) {
    float w = c.F(p.length("w_lead", 1e-6)), ov = c.F(p.length("overlap", 0.15e-6)),
          t = c.F(kFilmJunction_m);
    float x0 = -10.0f * w, x1 = 0.5f * w + ov, span = x1 - x0;
    if (c.detail == Detail::Simple)
        return mesh::box({0.5f * (x0 + x1), t, 0.5f * (x0 + x1)}, {span, 2.0f * t, span});
    MeshData m;
    mesh::appendColored(m, mesh::box({0.5f * (x0 + x1), 0.5f * t, 0.0f}, {span, t, w}), kLead);
    mesh::appendColored(m, mesh::box({0.0f, 1.5f * t, 0.5f * (x0 + x1)}, {w, t, span}), kLead);
    mesh::appendColored(
        m, mesh::box({0.25f * ov, t, 0.25f * ov}, {w + 0.5f * ov, 0.2f * t, w + 0.5f * ov}),
        kOxide);
    return m;
}

// DC SQUID: square loop of centreline side sqrt(area) and track width w with one junction on each
// vertical arm (asymmetric SQUIDs are drawn symmetric; the asymmetry is a spec-sheet value).
Result<MeshData> squidLoop(const GenParams& p, const GenContext& c) {
    double area = p.number("area_um2", 60.0) * 1e-12;
    float side = c.F(std::sqrt(std::max(area, 1e-14))), w = c.F(p.length("w", 1e-6)),
          t = c.F(kFilmJunction_m);
    float outer = side + w;
    if (c.detail == Detail::Simple)
        return mesh::box({0, 0.5f * t, 0}, {outer, t, outer});
    MeshData m;
    float h = 0.5f * side;
    mesh::appendColored(m, mesh::box({0, 0.5f * t, h}, {outer, t, w}), kLead);
    mesh::appendColored(m, mesh::box({0, 0.5f * t, -h}, {outer, t, w}), kLead);
    mesh::appendColored(m, mesh::box({h, 0.5f * t, 0}, {w, t, side - w}), kLead);
    mesh::appendColored(m, mesh::box({-h, 0.5f * t, 0}, {w, t, side - w}), kLead);
    for (float x : {-h, h})
        mesh::appendColored(m, mesh::box({x, 0.6f * t, 0}, {0.9f * w, 0.8f * t, 1.5f * w}), kOxide);
    return m;
}

// Airbridge: a strip of width w spanning `span` along local X, feet on the ground plane and a
// parabolic arch of clearance h; strip thickness 0.3 µm.
Result<MeshData> airbridge(const GenParams& p, const GenContext& c) {
    float span = c.F(p.length("span", 30e-6)), w = c.F(p.length("w", 10e-6)),
          h = c.F(p.length("h", 3e-6));
    float tb = c.F(0.3e-6);
    if (c.detail == Detail::Simple)
        return mesh::box({0, 0.5f * (h + tb), 0}, {span, h + tb, w});
    float foot = 0.15f * span, xa = 0.5f * span - foot;
    const int n = 12;
    std::vector<float> xs{-0.5f * span};
    for (int i = 0; i <= n; ++i)
        xs.push_back(-xa + 2.0f * xa * static_cast<float>(i) / n);
    xs.push_back(0.5f * span);
    auto yb = [&](float x) { return std::abs(x) >= xa ? 0.0f : h * (1.0f - (x / xa) * (x / xa)); };
    auto slope = [&](float x) { return std::abs(x) >= xa ? 0.0f : -2.0f * h * x / (xa * xa); };
    MeshData m;
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 cc, glm::vec3 d, glm::vec3 na,
                    glm::vec3 nb) {
        auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({a, na, {0, 0}, kBridge});
        m.vertices.push_back({b, nb, {1, 0}, kBridge});
        m.vertices.push_back({cc, nb, {1, 1}, kBridge});
        m.vertices.push_back({d, na, {0, 1}, kBridge});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    float hw = 0.5f * w;
    for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
        float xA = xs[i], xB = xs[i + 1];
        glm::vec3 nA = glm::normalize(glm::vec3(-slope(xA), 1.0f, 0.0f)),
                  nB = glm::normalize(glm::vec3(-slope(xB), 1.0f, 0.0f));
        float bA = yb(xA), bB = yb(xB);
        quad({xA, bA + tb, -hw}, {xB, bB + tb, -hw}, {xB, bB + tb, hw}, {xA, bA + tb, hw}, nA,
             nB);                                                                 // top
        quad({xA, bA, hw}, {xB, bB, hw}, {xB, bB, -hw}, {xA, bA, -hw}, -nA, -nB); // underside
        quad({xA, bA, hw}, {xA, bA + tb, hw}, {xB, bB + tb, hw}, {xB, bB, hw}, {0, 0, 1},
             {0, 0, 1});
        quad({xA, bA, -hw}, {xB, bB, -hw}, {xB, bB + tb, -hw}, {xA, bA + tb, -hw}, {0, 0, -1},
             {0, 0, -1});
    }
    for (float s : {-1.0f, 1.0f}) {
        float x = s * 0.5f * span;
        quad({x, 0, -hw}, {x, 0, hw}, {x, tb, hw}, {x, tb, -hw}, {s, 0, 0}, {s, 0, 0});
    }
    return m;
}

// Surface-electrode ion trap die: substrate slab, two RF rails along the trap axis (X) and
// `electrodes` segmented DC electrodes split between both sides.
Result<MeshData> trapChip(const GenParams& p, const GenContext& c) {
    glm::vec3 size{c.F(p.length("w", kTrapChipSize_m[0])), c.F(p.length("h", kTrapChipSize_m[1])),
                   c.F(p.length("d", kTrapChipSize_m[2]))};
    if (c.detail == Detail::Simple)
        return mesh::box({0, 0, 0}, size);
    MeshData m;
    float film = 0.1f * size.y;
    mesh::appendColored(m, mesh::box({0, -0.5f * film, 0}, {size.x, size.y - film, size.z}),
                        kTrapBody);
    float yTop = 0.5f * size.y - 0.5f * film;
    for (float z : {-0.12f * size.z, 0.12f * size.z})
        mesh::appendColored(m, mesh::box({0, yTop, z}, {0.96f * size.x, film, 0.06f * size.z}),
                            kElectrode);
    int perSide = std::max(1, p.integer("electrodes", 40) / 2);
    float pitch = 0.96f * size.x / static_cast<float>(perSide);
    for (int k = 0; k < perSide; ++k) {
        float x = -0.48f * size.x + pitch * (static_cast<float>(k) + 0.5f);
        for (float z : {-0.32f * size.z, 0.32f * size.z})
            mesh::appendColored(m, mesh::box({x, yTop, z}, {0.8f * pitch, film, 0.3f * size.z}),
                                kElectrode);
    }
    return m;
}

} // namespace qlab::lab::gen
