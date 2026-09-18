// Spec 18 §3 — path-based generators: tubes (coax), extrusions (CPW), meanders, grid.
#include "Graphics/Mesh.hpp"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace qlab::gfx::shapes {
namespace {
glm::vec3 catmull(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}
std::vector<glm::vec3> smoothPath(const std::vector<glm::vec3>& p, int sub) {
    if (p.size() < 3) return p;
    std::vector<glm::vec3> out;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        glm::vec3 p0 = i == 0 ? p[0] : p[i - 1], p3 = i + 2 < p.size() ? p[i + 2] : p.back();
        for (int s = 0; s < sub; ++s) out.push_back(catmull(p0, p[i], p[i + 1], p3, static_cast<float>(s) / sub));
    }
    out.push_back(p.back());
    return out;
}
} // namespace

MeshData tube(const std::vector<glm::vec3>& inPath, float r, int seg, bool smooth) {
    MeshData m;
    if (inPath.size() < 2) return m;
    std::vector<glm::vec3> path = smooth ? smoothPath(inPath, 6) : inPath;
    // parallel-transport frames
    glm::vec3 prevN{0, 1, 0};
    float arc = 0.f;
    for (std::size_t i = 0; i < path.size(); ++i) {
        glm::vec3 t = i + 1 < path.size() ? path[i + 1] - path[i] : path[i] - path[i - 1];
        float tl = glm::length(t);
        t = tl > 1e-9f ? t / tl : glm::vec3(0, 0, 1);
        if (i > 0) arc += glm::length(path[i] - path[i - 1]);
        glm::vec3 n = prevN - t * glm::dot(prevN, t);
        if (glm::length(n) < 1e-4f) n = std::abs(t.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), n -= t * glm::dot(n, t);
        n = glm::normalize(n);
        glm::vec3 b = glm::cross(t, n);
        prevN = n;
        for (int j = 0; j <= seg; ++j) {
            float a = static_cast<float>(j) / seg * glm::two_pi<float>();
            glm::vec3 nn = n * std::cos(a) + b * std::sin(a);
            m.vertices.push_back({path[i] + nn * r, nn, {static_cast<float>(j) / seg, arc}});
        }
    }
    for (std::size_t i = 0; i + 1 < path.size(); ++i)
        for (int j = 0; j < seg; ++j) {
            std::uint32_t a = static_cast<std::uint32_t>(i * (seg + 1) + j), bb = a + seg + 1;
            m.indices.insert(m.indices.end(), {a, a + 1, bb, a + 1, bb + 1, bb});
        }
    // end caps
    for (int end = 0; end < 2; ++end) {
        std::size_t i = end == 0 ? 0 : path.size() - 1;
        glm::vec3 t = end == 0 ? glm::normalize(path[0] - path[1]) : glm::normalize(path.back() - path[path.size() - 2]);
        auto c = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({path[i], t, {0.5f, 0.5f}});
        auto ring = static_cast<std::uint32_t>(i * (seg + 1));
        for (int j = 0; j <= seg; ++j) m.vertices.push_back({m.vertices[ring + j].position, t, {0, 0}});
        for (int j = 0; j < seg; ++j) {
            if (end == 0) m.indices.insert(m.indices.end(), {c, c + 1 + j, c + 2 + j});
            else m.indices.insert(m.indices.end(), {c, c + 2 + j, c + 1 + j});
        }
    }
    m.orientToNormals();
    return m;
}

MeshData extrudePolygon(const std::vector<glm::vec2>& poly, float h) {
    MeshData m;
    std::size_t n = poly.size();
    if (n < 3) return m;
    // side walls
    for (std::size_t i = 0; i < n; ++i) {
        glm::vec2 a = poly[i], b = poly[(i + 1) % n];
        glm::vec2 e = b - a;
        glm::vec3 nrm = glm::normalize(glm::vec3(e.y, 0, -e.x));
        auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({{a.x, 0, a.y}, nrm, {0, 0}});
        m.vertices.push_back({{b.x, 0, b.y}, nrm, {1, 0}});
        m.vertices.push_back({{b.x, h, b.y}, nrm, {1, 1}});
        m.vertices.push_back({{a.x, h, a.y}, nrm, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    // caps by ear clipping (polygon assumed simple, CCW in xz looking from +y)
    std::vector<std::uint32_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = static_cast<std::uint32_t>(i);
    auto cross2 = [](glm::vec2 a, glm::vec2 b, glm::vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
    auto inside = [&](glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
        return cross2(a, b, p) >= 0 && cross2(b, c, p) >= 0 && cross2(c, a, p) >= 0;
    };
    std::vector<std::uint32_t> tris;
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            std::uint32_t ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i], ic = idx[(i + 1) % idx.size()];
            if (cross2(poly[ia], poly[ib], poly[ic]) <= 0) continue;
            bool ok = true;
            for (auto j : idx)
                if (j != ia && j != ib && j != ic && inside(poly[j], poly[ia], poly[ib], poly[ic])) { ok = false; break; }
            if (!ok) continue;
            tris.insert(tris.end(), {ia, ib, ic});
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) tris.insert(tris.end(), {idx[0], idx[1], idx[2]});
    for (int side = 0; side < 2; ++side) {
        float y = side ? h : 0.f;
        glm::vec3 nrm{0, side ? 1.f : -1.f, 0};
        auto base = static_cast<std::uint32_t>(m.vertices.size());
        for (auto& p : poly) m.vertices.push_back({{p.x, y, p.y}, nrm, {p.x, p.y}});
        for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
            if (side) m.indices.insert(m.indices.end(), {base + tris[t], base + tris[t + 2], base + tris[t + 1]});
            else m.indices.insert(m.indices.end(), {base + tris[t], base + tris[t + 1], base + tris[t + 2]});
        }
    }
    m.orientToNormals();
    return m;
}

MeshData extrudePath(const std::vector<glm::vec2>& path, float w, float h) {
    MeshData m;
    if (path.size() < 2) return m;
    // Build the offset outline (left side forward, right side backward) with mitred joints.
    std::vector<glm::vec2> left, right;
    float hw = w * 0.5f;
    for (std::size_t i = 0; i < path.size(); ++i) {
        glm::vec2 d0 = i > 0 ? glm::normalize(path[i] - path[i - 1]) : glm::normalize(path[1] - path[0]);
        glm::vec2 d1 = i + 1 < path.size() ? glm::normalize(path[i + 1] - path[i]) : d0;
        glm::vec2 n0{-d0.y, d0.x}, n1{-d1.y, d1.x};
        glm::vec2 nm = glm::normalize(n0 + n1);
        float cosHalf = glm::dot(nm, n0);
        float len = cosHalf > 0.2f ? hw / cosHalf : hw;
        left.push_back(path[i] + nm * len);
        right.push_back(path[i] - nm * len);
    }
    std::vector<glm::vec2> outline(left.begin(), left.end());
    outline.insert(outline.end(), right.rbegin(), right.rend());
    // ensure CCW (positive area)
    float area = 0;
    for (std::size_t i = 0; i < outline.size(); ++i) {
        auto& a = outline[i]; auto& b = outline[(i + 1) % outline.size()];
        area += a.x * b.y - b.x * a.y;
    }
    if (area < 0) std::reverse(outline.begin(), outline.end());
    return extrudePolygon(outline, h);
}

std::vector<glm::vec2> meanderPath(int turns, float pitch, float amp, float lead) {
    std::vector<glm::vec2> p;
    float x = 0;
    p.push_back({x, 0});
    if (lead > 0) { x += lead; p.push_back({x, 0}); }
    for (int i = 0; i < turns; ++i) {
        float s = (i % 2 == 0) ? 1.f : -1.f;
        p.push_back({x, s * amp});
        x += pitch;
        p.push_back({x, s * amp});
    }
    p.push_back({x, 0});
    if (lead > 0) { x += lead; p.push_back({x, 0}); }
    return p;
}

MeshData plateWithHoles(glm::vec2 size, float t, const std::vector<glm::vec3>& holes) {
    MeshData m = box({size.x, t, size.y});
    for (auto& hcyl : holes) {
        MeshData c = cylinder(hcyl.z, t * 1.02f, 24, true);
        // flip normals inward to read as a recess
        for (auto& v : c.vertices) v.normal = -v.normal;
        for (std::size_t i = 0; i + 2 < c.indices.size(); i += 3) std::swap(c.indices[i + 1], c.indices[i + 2]);
        m.append(c, glm::translate(glm::mat4(1.0f), {hcyl.x, 0, hcyl.y}));
    }
    m.orientToNormals();
    return m;
}

MeshData grid(float half, int lines, float lw) {
    MeshData m;
    for (int i = 0; i <= lines; ++i) {
        float t = -half + 2 * half * static_cast<float>(i) / lines;
        MeshData a = box({2 * half, lw * 0.25f, lw});
        m.append(a, glm::translate(glm::mat4(1.0f), {0, 0, t}));
        MeshData b = box({lw, lw * 0.25f, 2 * half});
        m.append(b, glm::translate(glm::mat4(1.0f), {t, 0, 0}));
    }
    m.orientToNormals();
    return m;
}
} // namespace qlab::gfx::shapes
