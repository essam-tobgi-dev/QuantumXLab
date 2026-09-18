#pragma once
// Spec 18 §3 — anti-aliased lines by geometry expansion (core profile has no wide lines).
// Segments are expanded to screen-space quads in the vertex shader (line.vert) with pixel width.
#include "Graphics/GlObjects.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace qlab::gfx {

struct LineVertex {
    glm::vec3 position;   // relative to the batch origin (the camera), subtracted in double on the CPU
    glm::vec3 other;      // the other endpoint of the segment
    glm::vec4 color;
    float side;           // -1 / +1 expansion side
    float end;            // 0 = at 'position', 1 = at 'other'
    float widthPx;
    float dash;           // dash length in px, 0 = solid
};

class LineBatch {
public:
    void clear() { verts_.clear(); }
    // Every position handed to this batch is in WORLD space. The renderer sets the origin to the
    // camera position each frame; it is subtracted in double precision before the float upload,
    // so micrometre-scale overlays on the chip stay exact next to the metre-scale room (spec 18 §6).
    void setOrigin(const glm::dvec3& worldOrigin) { origin_ = worldOrigin; }
    const glm::dvec3& origin() const { return origin_; }
    // Double-precision endpoints; glm::vec3 arguments convert implicitly.
    void segment(const glm::dvec3& a, const glm::dvec3& b, const glm::vec4& color, float widthPx = 1.5f, float dashPx = 0);
    void polyline(const std::vector<glm::vec3>& pts, const glm::vec4& color, float widthPx = 1.5f, bool closed = false);
    void circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, int seg = 64, float widthPx = 1.5f);
    void arrow(const glm::dvec3& from, const glm::dvec3& to, const glm::vec4& color, float widthPx = 2.0f, float headFrac = 0.15f);
    void axes(const glm::vec3& origin, float length, float widthPx = 2.0f);
    void gridXZ(float halfSize, int lines, const glm::vec4& color, float widthPx = 1.0f);
    bool empty() const { return verts_.empty(); }
    std::size_t segmentCount() const { return verts_.size() / 6; }
    // Upload and draw (requires bound program `line`). Depth test controlled by caller.
    void upload();
    void draw() const;
private:
    glm::dvec3 origin_{0.0};
    std::vector<LineVertex> verts_;
    Buffer vbo_;
    VertexArray vao_;
    bool init_ = false;
    std::size_t uploaded_ = 0;
};

} // namespace qlab::gfx
