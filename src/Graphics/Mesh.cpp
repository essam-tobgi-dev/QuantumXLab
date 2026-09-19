#include "Graphics/Mesh.hpp"
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace qlab::gfx {

Aabb MeshData::bounds() const {
    Aabb b{glm::dvec3(1e300), glm::dvec3(-1e300)};
    if (vertices.empty())
        return Aabb{};
    for (auto& v : vertices)
        b.expand(glm::dvec3(v.position));
    return b;
}
void MeshData::append(const MeshData& o, const glm::mat4& xf) {
    auto base = static_cast<std::uint32_t>(vertices.size());
    glm::mat3 n = glm::transpose(glm::inverse(glm::mat3(xf)));
    for (auto v : o.vertices) {
        v.position = glm::vec3(xf * glm::vec4(v.position, 1.0f));
        v.normal = glm::normalize(n * v.normal);
        vertices.push_back(v);
    }
    for (auto i : o.indices)
        indices.push_back(base + i);
}
void MeshData::transform(const glm::mat4& xf) {
    glm::mat3 n = glm::transpose(glm::inverse(glm::mat3(xf)));
    for (auto& v : vertices) {
        v.position = glm::vec3(xf * glm::vec4(v.position, 1.0f));
        v.normal = glm::normalize(n * v.normal);
    }
}
void MeshData::recomputeNormals() {
    for (auto& v : vertices)
        v.normal = glm::vec3(0);
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        auto &a = vertices[indices[i]], &b = vertices[indices[i + 1]],
             &c = vertices[indices[i + 2]];
        glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (auto& v : vertices) {
        float l = glm::length(v.normal);
        v.normal = l > 1e-12f ? v.normal / l : glm::vec3(0, 1, 0);
    }
}
std::size_t MeshData::orientToNormals() {
    std::size_t flipped = 0;
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        const Vertex& a = vertices[indices[t]];
        const Vertex& b = vertices[indices[t + 1]];
        const Vertex& c = vertices[indices[t + 2]];
        const glm::vec3 g = glm::cross(b.position - a.position, c.position - a.position);
        if (glm::dot(g, a.normal + b.normal + c.normal) < 0.0f) {
            std::swap(indices[t + 1], indices[t + 2]);
            ++flipped;
        }
    }
    return flipped;
}

void MeshData::setColor(const glm::vec4& c) {
    for (auto& v : vertices)
        v.color = c;
}

void Mesh::upload(const MeshData& d) {
    // The element-buffer binding is VAO state: uploading while another mesh's VAO is bound (the
    // post quad after endFrame, or whatever the caller last drew) would re-point THAT VAO's index
    // buffer at this mesh. Detach first.
    VertexArray::unbind();
    vbo_ = Buffer(GL_ARRAY_BUFFER);
    ibo_ = Buffer(GL_ELEMENT_ARRAY_BUFFER);
    vao_ = VertexArray();
    vbo_.setData(d.vertices);
    ibo_.setData(d.indices);
    const GLsizei stride = sizeof(Vertex);
    VertexAttrib attribs[] = {
        {0, 3, GL_FLOAT, false, stride, offsetof(Vertex, position)},
        {1, 3, GL_FLOAT, false, stride, offsetof(Vertex, normal)},
        {2, 2, GL_FLOAT, false, stride, offsetof(Vertex, uv)},
        {3, 4, GL_FLOAT, false, stride, offsetof(Vertex, color)},
    };
    vao_.setAttribs(vbo_, attribs);
    vao_.setIndexBuffer(ibo_);
    VertexArray::unbind();
    indexCount_ = static_cast<std::uint32_t>(d.indices.size());
    bounds_ = d.bounds();
}
void Mesh::draw() const {
    if (!indexCount_)
        return;
    vao_.bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT, nullptr);
}
void Mesh::drawInstanced(int count) const {
    if (!indexCount_ || count <= 0)
        return;
    vao_.bind();
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT,
                            nullptr, count);
}
void Mesh::bindInstanceBuffer(const Buffer& instances) {
    // Instance layout: mat4 (4 × vec4 at 4..7), uvec2 id/flags (8), vec4 color (9). Stride 96
    // bytes.
    const GLsizei stride = 96;
    std::vector<VertexAttrib> a;
    for (GLuint c = 0; c < 4; ++c)
        a.push_back({4 + c, 4, GL_FLOAT, false, stride, c * 16, 1});
    a.push_back({8, 2, GL_UNSIGNED_INT, false, stride, 64, 1});
    a.push_back({9, 4, GL_FLOAT, false, stride, 80, 1});
    vao_.setAttribs(instances, a);
    VertexArray::unbind();
}

namespace shapes {
namespace {
void quad(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
    auto base = static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({a, n, {0, 0}});
    m.vertices.push_back({b, n, {1, 0}});
    m.vertices.push_back({c, n, {1, 1}});
    m.vertices.push_back({d, n, {0, 1}});
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}
} // namespace

MeshData box(glm::vec3 s) {
    MeshData m;
    glm::vec3 h = s * 0.5f;
    quad(m, {-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z}, {0, 0, 1});
    quad(m, {h.x, -h.y, -h.z}, {-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z}, {h.x, h.y, -h.z}, {0, 0, -1});
    quad(m, {h.x, -h.y, h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {h.x, h.y, h.z}, {1, 0, 0});
    quad(m, {-h.x, -h.y, -h.z}, {-h.x, -h.y, h.z}, {-h.x, h.y, h.z}, {-h.x, h.y, -h.z}, {-1, 0, 0});
    quad(m, {-h.x, h.y, h.z}, {h.x, h.y, h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z}, {0, 1, 0});
    quad(m, {-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, -h.y, h.z}, {-h.x, -h.y, h.z}, {0, -1, 0});
    m.orientToNormals();
    return m;
}

MeshData sphere(float r, int slices, int stacks) {
    MeshData m;
    for (int i = 0; i <= stacks; ++i) {
        float v = static_cast<float>(i) / stacks, phi = v * glm::pi<float>();
        for (int j = 0; j <= slices; ++j) {
            float u = static_cast<float>(j) / slices, th = u * glm::two_pi<float>();
            glm::vec3 n{std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th)};
            m.vertices.push_back({n * r, n, {u, 1 - v}});
        }
    }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            std::uint32_t a = i * (slices + 1) + j, b = a + slices + 1;
            m.indices.insert(m.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    m.orientToNormals();
    return m;
}

MeshData cylinder(float r, float h, int seg, bool caps) {
    MeshData m;
    float hh = h * 0.5f;
    for (int i = 0; i <= seg; ++i) {
        float t = static_cast<float>(i) / seg * glm::two_pi<float>();
        glm::vec3 n{std::cos(t), 0, std::sin(t)};
        m.vertices.push_back({n * r + glm::vec3(0, -hh, 0), n, {static_cast<float>(i) / seg, 0}});
        m.vertices.push_back({n * r + glm::vec3(0, hh, 0), n, {static_cast<float>(i) / seg, 1}});
    }
    for (int i = 0; i < seg; ++i) {
        std::uint32_t a = 2 * i;
        m.indices.insert(m.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    }
    if (caps) {
        for (int side = -1; side <= 1; side += 2) {
            float y = hh * side;
            glm::vec3 n{0, static_cast<float>(side), 0};
            auto c = static_cast<std::uint32_t>(m.vertices.size());
            m.vertices.push_back({{0, y, 0}, n, {0.5f, 0.5f}});
            for (int i = 0; i <= seg; ++i) {
                float t = static_cast<float>(i) / seg * glm::two_pi<float>();
                m.vertices.push_back({{r * std::cos(t), y, r * std::sin(t)},
                                      n,
                                      {0.5f + 0.5f * std::cos(t), 0.5f + 0.5f * std::sin(t)}});
            }
            for (int i = 0; i < seg; ++i) {
                if (side > 0)
                    m.indices.insert(m.indices.end(), {c, c + 2 + i, c + 1 + i});
                else
                    m.indices.insert(m.indices.end(), {c, c + 1 + i, c + 2 + i});
            }
        }
    }
    m.orientToNormals();
    return m;
}

MeshData cone(float r, float h, int seg) {
    MeshData m;
    float hh = h * 0.5f;
    float slope = r / h;
    for (int i = 0; i <= seg; ++i) {
        float t = static_cast<float>(i) / seg * glm::two_pi<float>();
        glm::vec3 d{std::cos(t), 0, std::sin(t)};
        glm::vec3 n = glm::normalize(glm::vec3(d.x, slope, d.z));
        m.vertices.push_back({d * r + glm::vec3(0, -hh, 0), n, {static_cast<float>(i) / seg, 0}});
        m.vertices.push_back({{0, hh, 0}, n, {static_cast<float>(i) / seg, 1}});
    }
    for (int i = 0; i < seg; ++i) {
        std::uint32_t a = 2 * i;
        m.indices.insert(m.indices.end(), {a, a + 2, a + 1});
    }
    auto c = static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({{0, -hh, 0}, {0, -1, 0}, {0.5f, 0.5f}});
    for (int i = 0; i <= seg; ++i) {
        float t = static_cast<float>(i) / seg * glm::two_pi<float>();
        m.vertices.push_back({{r * std::cos(t), -hh, r * std::sin(t)}, {0, -1, 0}, {0, 0}});
    }
    for (int i = 0; i < seg; ++i)
        m.indices.insert(m.indices.end(), {c, c + 1 + i, c + 2 + i});
    m.orientToNormals();
    return m;
}

MeshData torus(float R, float r, int majorSeg, int minorSeg) {
    MeshData m;
    for (int i = 0; i <= majorSeg; ++i) {
        float u = static_cast<float>(i) / majorSeg * glm::two_pi<float>();
        glm::vec3 c{R * std::cos(u), 0, R * std::sin(u)};
        for (int j = 0; j <= minorSeg; ++j) {
            float v = static_cast<float>(j) / minorSeg * glm::two_pi<float>();
            glm::vec3 n{std::cos(u) * std::cos(v), std::sin(v), std::sin(u) * std::cos(v)};
            m.vertices.push_back(
                {c + n * r,
                 n,
                 {static_cast<float>(i) / majorSeg, static_cast<float>(j) / minorSeg}});
        }
    }
    for (int i = 0; i < majorSeg; ++i)
        for (int j = 0; j < minorSeg; ++j) {
            std::uint32_t a = i * (minorSeg + 1) + j, b = a + minorSeg + 1;
            m.indices.insert(m.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    m.orientToNormals();
    return m;
}
} // namespace shapes
} // namespace qlab::gfx
