// Spec 17 §6 — primitive mesh builders. Every builder finishes with orientToNormals so triangle
// winding agrees with the shading normals under back-face culling (spec 18 §5 opaque pass).
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace qlab::lab::mesh {

glm::mat4 toMat4(const glm::dmat4& m) {
    return glm::mat4(m);
}

glm::mat4 translate(glm::vec3 t) {
    return glm::translate(glm::mat4(1.0f), t);
}

glm::mat4 alignY(glm::vec3 dir) {
    float len = glm::length(dir);
    if (len < 1e-12f)
        return glm::mat4(1.0f);
    glm::vec3 y = dir / len;
    glm::vec3 helper = std::abs(y.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 x = glm::normalize(glm::cross(helper, y));
    glm::vec3 z = glm::cross(x, y);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x, 0.0f);
    m[1] = glm::vec4(y, 0.0f);
    m[2] = glm::vec4(z, 0.0f);
    return m;
}

void append(MeshData& into, const MeshData& part, const glm::mat4& xf) {
    into.append(part, xf);
}

void appendColored(MeshData& into, MeshData part, const glm::vec4& color, const glm::mat4& xf) {
    part.setColor(color);
    into.append(part, xf);
}

MeshData box(glm::vec3 center, glm::vec3 size) {
    MeshData m = gfx::shapes::box(size);
    m.transform(translate(center));
    return m;
}

MeshData disc(float r, float y, int seg, bool up) {
    MeshData m;
    glm::vec3 n{0.0f, up ? 1.0f : -1.0f, 0.0f};
    m.vertices.push_back({{0.0f, y, 0.0f}, n, {0.5f, 0.5f}});
    for (int i = 0; i <= seg; ++i) {
        float t = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(seg);
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

MeshData annulus(float rIn, float rOut, float y, int seg, bool up) {
    MeshData m;
    glm::vec3 n{0.0f, up ? 1.0f : -1.0f, 0.0f};
    for (int i = 0; i <= seg; ++i) {
        float t = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(seg);
        glm::vec3 d{std::cos(t), 0.0f, std::sin(t)};
        m.vertices.push_back(
            {d * rIn + glm::vec3(0, y, 0), n, {static_cast<float>(i) / seg, 0.0f}});
        m.vertices.push_back(
            {d * rOut + glm::vec3(0, y, 0), n, {static_cast<float>(i) / seg, 1.0f}});
    }
    for (int i = 0; i < seg; ++i) {
        auto a = static_cast<std::uint32_t>(2 * i);
        m.indices.insert(m.indices.end(), {a, a + 1, a + 2, a + 1, a + 3, a + 2});
    }
    orientToNormals(m);
    return m;
}

MeshData wall(float r, float y0, float y1, int seg, bool outward) {
    MeshData m;
    float s = outward ? 1.0f : -1.0f;
    for (int i = 0; i <= seg; ++i) {
        float t = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(seg);
        glm::vec3 d{std::cos(t), 0.0f, std::sin(t)};
        m.vertices.push_back(
            {d * r + glm::vec3(0, y0, 0), d * s, {static_cast<float>(i) / seg, 0.0f}});
        m.vertices.push_back(
            {d * r + glm::vec3(0, y1, 0), d * s, {static_cast<float>(i) / seg, 1.0f}});
    }
    for (int i = 0; i < seg; ++i) {
        auto a = static_cast<std::uint32_t>(2 * i);
        m.indices.insert(m.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    }
    orientToNormals(m);
    return m;
}

MeshData dome(float r, float yc, int seg, int stacks, bool outward) {
    MeshData m;
    float s = outward ? 1.0f : -1.0f;
    for (int i = 0; i <= stacks; ++i) {
        float phi =
            glm::half_pi<float>() * (1.0f + static_cast<float>(i) / static_cast<float>(stacks));
        for (int j = 0; j <= seg; ++j) {
            float th = glm::two_pi<float>() * static_cast<float>(j) / static_cast<float>(seg);
            glm::vec3 n{std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th)};
            m.vertices.push_back({n * r + glm::vec3(0, yc, 0),
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

namespace {

double cross2(glm::dvec2 a, glm::dvec2 b, glm::dvec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Ear clipping of a counter-clockwise simple polygon without collinear vertices.
std::vector<std::uint32_t> triangulate(const std::vector<glm::vec2>& poly) {
    std::vector<std::uint32_t> idx(poly.size()), tris;
    for (std::size_t i = 0; i < poly.size(); ++i)
        idx[i] = static_cast<std::uint32_t>(i);
    auto P = [&](std::uint32_t i) { return glm::dvec2(poly[i]); };
    std::size_t guard = 0;
    while (idx.size() > 3 && guard++ < poly.size() * poly.size() + 16) {
        bool clipped = false;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            std::uint32_t ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i],
                          ic = idx[(i + 1) % idx.size()];
            if (cross2(P(ia), P(ib), P(ic)) <= 0.0)
                continue;
            bool ear = true;
            for (std::uint32_t j : idx) {
                if (j == ia || j == ib || j == ic)
                    continue;
                glm::dvec2 p = P(j);
                if (cross2(P(ia), P(ib), p) >= 0.0 && cross2(P(ib), P(ic), p) >= 0.0 &&
                    cross2(P(ic), P(ia), p) >= 0.0) {
                    ear = false;
                    break;
                }
            }
            if (!ear)
                continue;
            tris.insert(tris.end(), {ia, ib, ic});
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped)
            break;
    }
    if (idx.size() == 3)
        tris.insert(tris.end(), {idx[0], idx[1], idx[2]});
    return tris;
}

} // namespace

MeshData prism(std::vector<glm::vec2> poly, float y0, float y1) {
    MeshData m;
    poly = dedupe2(poly, 1e-9f);
    if (poly.size() >= 2 && glm::length(poly.front() - poly.back()) < 1e-9f)
        poly.pop_back();
    // drop collinear vertices (they would stall ear clipping)
    for (std::size_t i = 0; poly.size() > 3 && i < poly.size();) {
        glm::dvec2 a = poly[(i + poly.size() - 1) % poly.size()], b = poly[i],
                   c = poly[(i + 1) % poly.size()];
        if (std::abs(cross2(a, b, c)) <= 1e-12 * std::max(1.0, glm::dot(c - a, c - a)))
            poly.erase(poly.begin() + static_cast<std::ptrdiff_t>(i));
        else
            ++i;
    }
    if (poly.size() < 3)
        return m;
    double area = 0.0;
    for (std::size_t i = 0; i < poly.size(); ++i)
        area += cross2({0, 0}, poly[i], poly[(i + 1) % poly.size()]);
    if (area < 0.0)
        std::reverse(poly.begin(), poly.end());
    std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        glm::vec2 a = poly[i], b = poly[(i + 1) % n];
        glm::vec2 e = glm::normalize(b - a);
        glm::vec3 nrm{e.y, 0.0f, -e.x}; // outward for a CCW polygon in (x, z)
        auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({{a.x, y0, a.y}, nrm, {0, 0}});
        m.vertices.push_back({{b.x, y0, b.y}, nrm, {1, 0}});
        m.vertices.push_back({{b.x, y1, b.y}, nrm, {1, 1}});
        m.vertices.push_back({{a.x, y1, a.y}, nrm, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    std::vector<std::uint32_t> tris = triangulate(poly);
    for (int side = 0; side < 2; ++side) {
        float y = side ? y1 : y0;
        glm::vec3 nrm{0.0f, side ? 1.0f : -1.0f, 0.0f};
        auto base = static_cast<std::uint32_t>(m.vertices.size());
        for (const auto& p : poly)
            m.vertices.push_back({{p.x, y, p.y}, nrm, {p.x, p.y}});
        for (auto t : tris)
            m.indices.push_back(base + t);
    }
    orientToNormals(m);
    return m;
}

MeshData ngonPrism(int n, float r, float y0, float y1) {
    std::vector<glm::vec2> poly;
    for (int i = 0; i < n; ++i) {
        float t = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(n);
        poly.push_back({r * std::cos(t), r * std::sin(t)});
    }
    return prism(std::move(poly), y0, y1);
}

void orientToNormals(MeshData& m) {
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const auto& a = m.vertices[m.indices[i]];
        const auto& b = m.vertices[m.indices[i + 1]];
        const auto& c = m.vertices[m.indices[i + 2]];
        glm::dvec3 g =
            glm::cross(glm::dvec3(b.position - a.position), glm::dvec3(c.position - a.position));
        glm::dvec3 n = glm::dvec3(a.normal) + glm::dvec3(b.normal) + glm::dvec3(c.normal);
        if (glm::dot(g, n) < 0.0)
            std::swap(m.indices[i + 1], m.indices[i + 2]);
    }
}

} // namespace qlab::lab::mesh
