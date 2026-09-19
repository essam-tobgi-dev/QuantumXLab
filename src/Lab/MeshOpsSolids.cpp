// Spec 17 §6 — solids of the fridge detail pass: dished can heads, plate faces with a real
// through-hole and chamfered rims, T-slot frame profiles, hex bolts.
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace qlab::lab::mesh {

MeshData dishedHead(float r, float depth, float yTop, int seg, int stacks, bool outward) {
    // Profile: x = r sin φ, y = yTop − depth (1 − cos φ) … using the ellipse (x/r)² + (Δy/depth)² =
    // 1 parametrised by φ ∈ [0, π/2]: x = r cos φ, Δy = depth sin φ. Normal of the ellipse at φ is
    // (depth cos φ, −r sin φ) in the (radial, y) plane, pointing outward/downward.
    MeshData m;
    float s = outward ? 1.0f : -1.0f;
    for (int i = 0; i <= stacks; ++i) {
        float phi = glm::half_pi<float>() * static_cast<float>(i) / static_cast<float>(stacks);
        float rad = r * std::cos(phi), dy = depth * std::sin(phi);
        glm::vec2 n2 = glm::normalize(glm::vec2(depth * std::cos(phi), -r * std::sin(phi)));
        for (int j = 0; j <= seg; ++j) {
            float th = glm::two_pi<float>() * static_cast<float>(j) / static_cast<float>(seg);
            glm::vec3 d{std::cos(th), 0.0f, std::sin(th)};
            glm::vec3 n = glm::normalize(glm::vec3(d.x * n2.x, n2.y, d.z * n2.x));
            if (i == stacks)
                n = {0.0f, -1.0f, 0.0f}; // crown
            m.vertices.push_back({d * rad + glm::vec3(0.0f, yTop - dy, 0.0f),
                                  n * s,
                                  {static_cast<float>(j) / seg, static_cast<float>(i) / stacks}});
        }
    }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < seg; ++j) {
            auto a = static_cast<std::uint32_t>(i * (seg + 1) + j);
            auto b = a + static_cast<std::uint32_t>(seg + 1);
            m.indices.insert(m.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    orientToNormals(m);
    return m;
}

MeshData plateFace(float r, float y, float hx, float hz, float hr, int seg, bool up,
                   std::vector<float>& angles) {
    MeshData m;
    angles.clear();
    glm::vec3 n{0.0f, up ? 1.0f : -1.0f, 0.0f};
    const float rc2 = hx * hx + hz * hz;
    if (hr <= 0.0f || rc2 >= r * r) { // plain fan
        for (int i = 0; i < seg; ++i)
            angles.push_back(glm::two_pi<float>() * static_cast<float>(i) /
                             static_cast<float>(seg));
        m.vertices.push_back({{0.0f, y, 0.0f}, n, {0.5f, 0.5f}});
        for (int i = 0; i <= seg; ++i) {
            float t = angles[static_cast<std::size_t>(i % seg)];
            m.vertices.push_back({{r * std::cos(t), y, r * std::sin(t)},
                                  n,
                                  {0.5f + 0.5f * std::cos(t), 0.5f + 0.5f * std::sin(t)}});
        }
        for (int i = 0; i < seg; ++i)
            m.indices.insert(m.indices.end(), {0u, static_cast<std::uint32_t>(i + 1),
                                               static_cast<std::uint32_t>(i + 2)});
        orientToNormals(m);
        return m;
    }
    // Generalised annulus around the hole: along direction d from the hole centre c, the outer
    // circle is at distance R = −c·d + sqrt((c·d)² + r² − |c|²).
    const glm::vec2 c{hx, hz};
    for (int i = 0; i <= seg; ++i) {
        float t = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(seg);
        glm::vec2 d{std::cos(t), std::sin(t)};
        float cd = glm::dot(c, d);
        float R = -cd + std::sqrt(std::max(0.0f, cd * cd + r * r - rc2));
        glm::vec2 inner = c + d * hr, outer = c + d * R;
        if (i < seg)
            angles.push_back(std::atan2(outer.y, outer.x));
        m.vertices.push_back({{inner.x, y, inner.y}, n, {static_cast<float>(i) / seg, 0.0f}});
        m.vertices.push_back({{outer.x, y, outer.y}, n, {static_cast<float>(i) / seg, 1.0f}});
    }
    for (int i = 0; i < seg; ++i) {
        auto a = static_cast<std::uint32_t>(2 * i);
        m.indices.insert(m.indices.end(), {a, a + 1, a + 2, a + 1, a + 3, a + 2});
    }
    orientToNormals(m);
    return m;
}

MeshData plateRim(float r, float yTop, float yBot, float c, const std::vector<float>& angles) {
    MeshData m;
    const std::size_t n = angles.size();
    if (n < 3)
        return m;
    c = std::min(c, 0.45f * (yTop - yBot));
    const float k = glm::one_over_root_two<float>();
    // Four rings per azimuth: chamfer start on the top face, top edge, bottom edge, chamfer end.
    for (std::size_t i = 0; i <= n; ++i) {
        float t = angles[i % n];
        glm::vec3 d{std::cos(t), 0.0f, std::sin(t)};
        float u = static_cast<float>(i) / static_cast<float>(n);
        glm::vec3 nTop = glm::normalize(glm::vec3(d.x * k, k, d.z * k)),
                  nBot = glm::normalize(glm::vec3(d.x * k, -k, d.z * k));
        m.vertices.push_back({d * (r - c) + glm::vec3(0, yTop, 0), nTop, {u, 0.0f}});
        m.vertices.push_back({d * r + glm::vec3(0, yTop - c, 0), nTop, {u, 0.2f}});
        m.vertices.push_back({d * r + glm::vec3(0, yTop - c, 0), d, {u, 0.2f}});
        m.vertices.push_back({d * r + glm::vec3(0, yBot + c, 0), d, {u, 0.8f}});
        m.vertices.push_back({d * r + glm::vec3(0, yBot + c, 0), nBot, {u, 0.8f}});
        m.vertices.push_back({d * (r - c) + glm::vec3(0, yBot, 0), nBot, {u, 1.0f}});
    }
    for (std::size_t i = 0; i < n; ++i) {
        auto a = static_cast<std::uint32_t>(6 * i);
        for (std::uint32_t band : {0u, 2u, 4u}) {
            std::uint32_t p = a + band, q = a + 6 + band;
            m.indices.insert(m.indices.end(), {p, q, p + 1, p + 1, q, q + 1});
        }
    }
    orientToNormals(m);
    return m;
}

std::vector<glm::vec2> tSlotProfile(float s, float slot, float slotDepth, float lip) {
    // One side (+X face, traversed from +Z to −Z): flat, groove mouth (width `slot`, depth `lip`),
    // widening to the T cavity (width 2·slot) down to `slotDepth`, then back.
    const float h = 0.5f * s, hs = 0.5f * slot, ws = slot;
    std::vector<glm::vec2> side{{h, h},
                                {h, hs},
                                {h - lip, hs},
                                {h - lip, ws},
                                {h - slotDepth, ws},
                                {h - slotDepth, -ws},
                                {h - lip, -ws},
                                {h - lip, -hs},
                                {h, -hs},
                                {h, -h}};
    std::vector<glm::vec2> out;
    for (int face = 0; face < 4; ++face) { // rotate the +X side by −90° per face: (x, z) → (z, −x)
        for (std::size_t i = 0; i + 1 < side.size(); ++i) {
            glm::vec2 p = side[i];
            for (int k = 0; k < face; ++k)
                p = {p.y, -p.x};
            out.push_back(p);
        }
    }
    // The +X side runs +Z → −Z, which is clockwise seen from +Y in an XZ plane; prism() accepts
    // either orientation.
    return out;
}

MeshData extrudeBetween(const std::vector<glm::vec2>& profileXZ, glm::vec3 a, glm::vec3 b) {
    float len = glm::length(b - a);
    if (len <= 0.0f)
        return {};
    MeshData m = prism(profileXZ, 0.0f, len);
    m.transform(translate(a) * alignY((b - a) / len));
    orientToNormals(m);
    return m;
}

MeshData hexBolt(float af, float hh, float rs, float ls) {
    MeshData m = ngonPrism(6, af / std::sqrt(3.0f), 0.0f, hh);
    if (ls > 0.0f)
        append(m, gfx::shapes::cylinder(rs, ls, 12, true), translate({0.0f, -0.5f * ls, 0.0f}));
    return m;
}

} // namespace qlab::lab::mesh
