#include "Graphics/LineBatch.hpp"
#include <cmath>
#include <glm/gtc/constants.hpp>
namespace qlab::gfx {
void LineBatch::segment(const glm::dvec3& wa, const glm::dvec3& wb, const glm::vec4& c, float w,
                        float dash) {
    // World → origin-relative in double, then narrow to float for the GPU.
    const glm::vec3 a(wa - origin_), b(wb - origin_);
    // two triangles: (a,-1) (a,+1) (b,+1) / (a,-1) (b,+1) (b,-1)
    verts_.push_back({a, b, c, -1, 0, w, dash});
    verts_.push_back({a, b, c, +1, 0, w, dash});
    verts_.push_back({a, b, c, +1, 1, w, dash});
    verts_.push_back({a, b, c, -1, 0, w, dash});
    verts_.push_back({a, b, c, +1, 1, w, dash});
    verts_.push_back({a, b, c, -1, 1, w, dash});
}
void LineBatch::polyline(const std::vector<glm::vec3>& p, const glm::vec4& c, float w,
                         bool closed) {
    for (std::size_t i = 0; i + 1 < p.size(); ++i)
        segment(p[i], p[i + 1], c, w);
    if (closed && p.size() > 2)
        segment(p.back(), p.front(), c, w);
}
void LineBatch::circle(const glm::vec3& ctr, const glm::vec3& n, float r, const glm::vec4& c,
                       int seg, float w) {
    glm::vec3 nn = glm::normalize(n);
    glm::vec3 u = std::abs(nn.y) < 0.9f ? glm::normalize(glm::cross(nn, {0, 1, 0}))
                                        : glm::normalize(glm::cross(nn, {1, 0, 0}));
    glm::vec3 v = glm::cross(nn, u);
    std::vector<glm::vec3> pts;
    for (int i = 0; i < seg; ++i) {
        float t = static_cast<float>(i) / seg * glm::two_pi<float>();
        pts.push_back(ctr + (u * std::cos(t) + v * std::sin(t)) * r);
    }
    polyline(pts, c, w, true);
}
void LineBatch::arrow(const glm::dvec3& a, const glm::dvec3& b, const glm::vec4& c, float w,
                      float headFrac) {
    segment(a, b, c, w);
    glm::dvec3 d = b - a;
    const double l = glm::length(d);
    if (l < 1e-15)
        return; // micrometre-scale arrows on the chip are legitimate
    d /= l;
    const glm::dvec3 u = std::abs(d.y) < 0.9 ? glm::normalize(glm::cross(d, glm::dvec3(0, 1, 0)))
                                             : glm::normalize(glm::cross(d, glm::dvec3(1, 0, 0)));
    const double h = l * static_cast<double>(headFrac);
    segment(b, b - d * h + u * (h * 0.4), c, w);
    segment(b, b - d * h - u * (h * 0.4), c, w);
}
void LineBatch::axes(const glm::vec3& o, float len, float w) {
    arrow(o, o + glm::vec3(len, 0, 0), {0.9f, 0.25f, 0.25f, 1}, w);
    arrow(o, o + glm::vec3(0, len, 0), {0.3f, 0.85f, 0.3f, 1}, w);
    arrow(o, o + glm::vec3(0, 0, len), {0.3f, 0.5f, 1.0f, 1}, w);
}
void LineBatch::gridXZ(float half, int lines, const glm::vec4& c, float w) {
    for (int i = 0; i <= lines; ++i) {
        float t = -half + 2 * half * static_cast<float>(i) / lines;
        segment({-half, 0, t}, {half, 0, t}, c, w);
        segment({t, 0, -half}, {t, 0, half}, c, w);
    }
}
void LineBatch::upload() {
    if (!init_) {
        vbo_ = Buffer(GL_ARRAY_BUFFER);
        vao_ = VertexArray();
        init_ = true;
    }
    vbo_.setData(verts_, BufferUsage::Stream);
    const GLsizei stride = sizeof(LineVertex);
    VertexAttrib a[] = {
        {0, 3, GL_FLOAT, false, stride, offsetof(LineVertex, position)},
        {1, 3, GL_FLOAT, false, stride, offsetof(LineVertex, other)},
        {2, 4, GL_FLOAT, false, stride, offsetof(LineVertex, color)},
        {3, 1, GL_FLOAT, false, stride, offsetof(LineVertex, side)},
        {4, 1, GL_FLOAT, false, stride, offsetof(LineVertex, end)},
        {5, 1, GL_FLOAT, false, stride, offsetof(LineVertex, widthPx)},
        {6, 1, GL_FLOAT, false, stride, offsetof(LineVertex, dash)},
    };
    vao_.setAttribs(vbo_, a);
    VertexArray::unbind();
    uploaded_ = verts_.size();
}
void LineBatch::draw() const {
    if (!init_ || uploaded_ == 0)
        return;
    vao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(uploaded_));
}
} // namespace qlab::gfx
