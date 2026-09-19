// Spec 17 §6/§7.5 — polyline utilities, plane clipping and mesh validation.
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace qlab::lab::mesh {

std::vector<glm::vec3> dedupe(const std::vector<glm::vec3>& pts, float eps) {
    std::vector<glm::vec3> out;
    out.reserve(pts.size());
    for (const auto& p : pts)
        if (out.empty() || glm::length(p - out.back()) > eps)
            out.push_back(p);
    return out;
}

std::vector<glm::vec2> dedupe2(const std::vector<glm::vec2>& pts, float eps) {
    std::vector<glm::vec2> out;
    out.reserve(pts.size());
    for (const auto& p : pts)
        if (out.empty() || glm::length(p - out.back()) > eps)
            out.push_back(p);
    return out;
}

std::vector<glm::vec2> offsetPolyline(const std::vector<glm::vec2>& in, float d) {
    std::vector<glm::vec2> p = dedupe2(in, 1e-9f), out;
    if (p.size() < 2)
        return p;
    out.reserve(p.size());
    for (std::size_t i = 0; i < p.size(); ++i) {
        glm::vec2 d0 = i > 0 ? glm::normalize(p[i] - p[i - 1]) : glm::normalize(p[1] - p[0]);
        glm::vec2 d1 = i + 1 < p.size() ? glm::normalize(p[i + 1] - p[i]) : d0;
        glm::vec2 n0{-d0.y, d0.x}, n1{-d1.y, d1.x};
        glm::vec2 nm = n0 + n1;
        float len = glm::length(nm);
        nm = len > 1e-6f ? nm / len : n0;
        float c = glm::dot(nm, n0);
        out.push_back(p[i] + nm * (c > 0.2f ? d / c : d)); // mitre, limited at sharp turns
    }
    return out;
}

MeshData ribbon(const std::vector<glm::vec2>& path, float width, float y0, float y1) {
    MeshData m;
    std::vector<glm::vec2> p = dedupe2(path, 1e-7f);
    if (p.size() < 2 || width <= 0.0f)
        return m;
    std::vector<glm::vec2> L = offsetPolyline(p, 0.5f * width),
                           R = offsetPolyline(p, -0.5f * width);
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
        auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({a, n, {0, 0}});
        m.vertices.push_back({b, n, {1, 0}});
        m.vertices.push_back({c, n, {1, 1}});
        m.vertices.push_back({d, n, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    auto at = [](glm::vec2 q, float y) { return glm::vec3(q.x, y, q.y); };
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        glm::vec2 dir = glm::normalize(p[i + 1] - p[i]);
        glm::vec3 left{-dir.y, 0.0f, dir.x};
        quad(at(L[i], y1), at(L[i + 1], y1), at(R[i + 1], y1), at(R[i], y1), {0, 1, 0});
        quad(at(L[i], y0), at(R[i], y0), at(R[i + 1], y0), at(L[i + 1], y0), {0, -1, 0});
        quad(at(L[i], y0), at(L[i + 1], y0), at(L[i + 1], y1), at(L[i], y1), left);
        quad(at(R[i], y0), at(R[i], y1), at(R[i + 1], y1), at(R[i + 1], y0), -left);
    }
    glm::vec2 d0 = glm::normalize(p[1] - p[0]), d1 = glm::normalize(p.back() - p[p.size() - 2]);
    quad(at(L.front(), y0), at(R.front(), y0), at(R.front(), y1), at(L.front(), y1),
         {-d0.x, 0, -d0.y});
    quad(at(L.back(), y0), at(L.back(), y1), at(R.back(), y1), at(R.back(), y0), {d1.x, 0, d1.y});
    orientToNormals(m);
    return m;
}

double polylineLength(const std::vector<glm::dvec3>& pts) {
    double s = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i)
        s += glm::length(pts[i] - pts[i - 1]);
    return s;
}

std::vector<glm::dvec3> catmullRom(const std::vector<glm::dvec3>& p, int sub) {
    if (p.size() < 3)
        return p;
    std::vector<glm::dvec3> out;
    auto cr = [](glm::dvec3 p0, glm::dvec3 p1, glm::dvec3 p2, glm::dvec3 p3, double t) {
        double t2 = t * t, t3 = t2 * t;
        return 0.5 * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                      (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
    };
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        glm::dvec3 p0 = i == 0 ? p[0] : p[i - 1];
        glm::dvec3 p3 = i + 2 < p.size() ? p[i + 2] : p.back();
        for (int s = 0; s < sub; ++s)
            out.push_back(cr(p0, p[i], p[i + 1], p3, static_cast<double>(s) / sub));
    }
    out.push_back(p.back());
    return out;
}

glm::dvec3 pointAt(const std::vector<glm::dvec3>& pts, double s, glm::dvec3* tangent) {
    if (pts.empty())
        return glm::dvec3(0.0);
    if (pts.size() == 1) {
        if (tangent)
            *tangent = glm::dvec3(0, -1, 0);
        return pts[0];
    }
    s = std::max(0.0, s);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        glm::dvec3 seg = pts[i] - pts[i - 1];
        double len = glm::length(seg);
        if (len <= 0.0)
            continue;
        if (s <= len || i + 1 == pts.size()) {
            if (tangent)
                *tangent = seg / len;
            return pts[i - 1] + seg * std::min(1.0, s / len);
        }
        s -= len;
    }
    return pts.back();
}

MeshData clipByPlane(const MeshData& m, const glm::dvec4& plane) {
    MeshData out;
    auto dist = [&](const gfx::Vertex& v) {
        return glm::dot(glm::dvec3(plane), glm::dvec3(v.position)) + plane.w;
    };
    auto lerpV = [](const gfx::Vertex& a, const gfx::Vertex& b, float t) {
        gfx::Vertex v;
        v.position = glm::mix(a.position, b.position, t);
        glm::vec3 n = glm::mix(a.normal, b.normal, t);
        float l = glm::length(n);
        v.normal = l > 1e-12f ? n / l : a.normal;
        v.uv = glm::mix(a.uv, b.uv, t);
        v.color = glm::mix(a.color, b.color, t);
        return v;
    };
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        std::array<gfx::Vertex, 3> tri{m.vertices[m.indices[i]], m.vertices[m.indices[i + 1]],
                                       m.vertices[m.indices[i + 2]]};
        std::array<double, 3> d{dist(tri[0]), dist(tri[1]), dist(tri[2])};
        if (d[0] < 0 && d[1] < 0 && d[2] < 0)
            continue;
        std::vector<gfx::Vertex> poly;
        for (int k = 0; k < 3; ++k) {
            const auto& a = tri[static_cast<std::size_t>(k)];
            const auto& b = tri[static_cast<std::size_t>((k + 1) % 3)];
            double da = d[static_cast<std::size_t>(k)],
                   db = d[static_cast<std::size_t>((k + 1) % 3)];
            if (da >= 0)
                poly.push_back(a);
            if ((da >= 0) != (db >= 0))
                poly.push_back(lerpV(a, b, static_cast<float>(da / (da - db))));
        }
        auto base = static_cast<std::uint32_t>(out.vertices.size());
        out.vertices.insert(out.vertices.end(), poly.begin(), poly.end());
        for (std::uint32_t k = 1; k + 1 < poly.size(); ++k)
            out.indices.insert(out.indices.end(), {base, base + k, base + k + 1});
    }
    return out;
}

std::string validate(const MeshData& m, double normalTol) {
    if (m.vertices.empty() || m.indices.empty())
        return "empty mesh";
    if (m.indices.size() % 3 != 0)
        return std::format("index count {} is not a triangle list", m.indices.size());
    for (std::size_t i = 0; i < m.indices.size(); ++i)
        if (m.indices[i] >= m.vertices.size())
            return std::format("index {} = {} out of range ({} vertices)", i, m.indices[i],
                               m.vertices.size());
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const auto& v = m.vertices[i];
        if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) ||
            !std::isfinite(v.position.z))
            return std::format("vertex {} has a non-finite position", i);
        double len = glm::length(glm::dvec3(v.normal));
        if (!std::isfinite(len) || std::abs(len - 1.0) > normalTol)
            return std::format("vertex {} normal length {} is not unit", i, len);
    }
    return {};
}

} // namespace qlab::lab::mesh
