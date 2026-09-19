#pragma once
// Spec 18 §3 — CPU mesh data + GPU upload; procedural generators in gfx::shapes.
#include "Graphics/Camera.hpp"
#include "Graphics/GlObjects.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace qlab::gfx {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color{1, 1, 1, 1};
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    Aabb bounds() const;
    void append(const MeshData& other, const glm::mat4& xf = glm::mat4(1.0f));
    void transform(const glm::mat4& xf);
    void recomputeNormals(); // flat-averaged from triangles
    // Flip every triangle whose geometric normal (p1-p0)x(p2-p0) opposes its vertex normals, so
    // the mesh is counter-clockwise seen from outside and survives back-face culling. Returns the
    // number of triangles flipped. Every shapes:: generator ends with this pass.
    std::size_t orientToNormals();
    void setColor(const glm::vec4& c);
    std::size_t triangleCount() const { return indices.size() / 3; }
};

// GPU mesh. Layout: 0 position, 1 normal, 2 uv, 3 color; per-instance attribs 4..9 (see
// Instancing).
class Mesh {
  public:
    Mesh() = default;
    explicit Mesh(const MeshData& d) { upload(d); }
    void upload(const MeshData& d);
    void draw() const;
    void drawInstanced(int count) const;
    // Attach an instance buffer (mat4 model at 4..7 [0], uvec2 id/flags at 8 [64], vec4 color at 9
    // [80]; stride 96).
    void bindInstanceBuffer(const Buffer& instances);
    std::uint32_t indexCount() const { return indexCount_; }
    const Aabb& bounds() const { return bounds_; }
    bool valid() const { return indexCount_ > 0; }

  private:
    VertexArray vao_;
    Buffer vbo_{GL_ARRAY_BUFFER}, ibo_{GL_ELEMENT_ARRAY_BUFFER};
    std::uint32_t indexCount_ = 0;
    Aabb bounds_;
};

namespace shapes {
// All generators return meshes centred at the origin unless stated, with unit outward normals.
MeshData box(glm::vec3 size);
MeshData sphere(float radius, int slices = 32, int stacks = 16);
MeshData cylinder(float radius, float height, int segments = 32, bool caps = true); // axis +y
MeshData cone(float radius, float height, int segments = 32);
MeshData torus(float majorR, float minorR, int majorSeg = 48, int minorSeg = 16);
// Tube of radius r swept along a polyline (Catmull–Rom smoothed if smooth); for coax runs.
MeshData tube(const std::vector<glm::vec3>& path, float radius, int segments = 12,
              bool smooth = true);
// Plate (box) with circular holes cut as inset rings (visual approximation: holes rendered as
// recessed cylinders).
MeshData plateWithHoles(glm::vec2 size, float thickness,
                        const std::vector<glm::vec3>& holes /* x,z,radius */);
// Extrude a closed 2D polygon (xz plane, CCW) by height along +y (for CPW traces, pads).
MeshData extrudePolygon(const std::vector<glm::vec2>& polygon, float height);
// Ribbon of width w along a 2D path in the xz plane, extruded by height (for CPW centre traces).
MeshData extrudePath(const std::vector<glm::vec2>& path, float width, float height);
// Meander path: n turns of pitch p and amplitude a starting at origin along +x (2D, xz plane).
std::vector<glm::vec2> meanderPath(int turns, float pitch, float amplitude, float lead = 0.0f);
MeshData grid(float halfSize, int lines, float lineWidth); // thin quads on the xz plane
} // namespace shapes

} // namespace qlab::gfx
