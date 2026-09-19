#pragma once
// Spec 17 §6 — mesh building blocks shared by the procedural generators (all CPU, no GL).
#include "Graphics/Mesh.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace qlab::lab::mesh {

using gfx::MeshData;

inline const glm::vec4 kWhite{1.0f, 1.0f, 1.0f, 1.0f};

glm::mat4 toMat4(const glm::dmat4& m);
glm::mat4 translate(glm::vec3 t);
// Rotation taking +Y onto `dir` (unit), used to orient axis-+Y primitives along a path segment.
glm::mat4 alignY(glm::vec3 dir);

// Appends `part` transformed by `xf`; if `color` is given, the appended vertices take it.
void append(MeshData& into, const MeshData& part, const glm::mat4& xf = glm::mat4(1.0f));
void appendColored(MeshData& into, MeshData part, const glm::vec4& color,
                   const glm::mat4& xf = glm::mat4(1.0f));

// Axis-aligned box with centre and full size.
MeshData box(glm::vec3 center, glm::vec3 size);
// Flat disc / annulus in the XZ plane at height y, facing +Y (up) or −Y.
MeshData disc(float r, float y, int segments, bool up);
MeshData annulus(float rInner, float rOuter, float y, int segments, bool up);
// Open cylindrical wall of radius r between y0 < y1, normals outward or inward.
MeshData wall(float r, float y0, float y1, int segments, bool outward);
// Lower hemisphere of radius r centred at (0, yCenter, 0), normals outward or inward.
MeshData dome(float r, float yCenter, int segments, int stacks, bool outward);
// Right prism of a simple polygon given in the XZ plane (either orientation), from y0 to y1,
// with outward side normals and caps.
MeshData prism(std::vector<glm::vec2> polygonXZ, float y0, float y1);
// Regular n-gon prism (vertex 0 on +X) of circumradius r between y0 and y1.
MeshData ngonPrism(int n, float r, float y0, float y1);
// Strip of `width` along a polyline in the XZ plane between y0 and y1, built from mitred quads
// (top, bottom, both side walls, end caps); used for CPW centre conductors and gap cut-outs.
MeshData ribbon(const std::vector<glm::vec2>& pathXZ, float width, float y0, float y1);

// Rewinds every non-degenerate triangle whose geometric normal points against the average of its
// vertex normals, so back-face culling agrees with the shading normals.
void orientToNormals(MeshData& m);

// Polyline helpers.
std::vector<glm::vec3> dedupe(const std::vector<glm::vec3>& pts, float eps);
std::vector<glm::vec2> dedupe2(const std::vector<glm::vec2>& pts, float eps);
std::vector<glm::vec2> offsetPolyline(const std::vector<glm::vec2>& pts, float distance);
double polylineLength(const std::vector<glm::dvec3>& pts);
// Catmull–Rom resampling identical to gfx::shapes::tube's smoothing (6 samples per span).
std::vector<glm::dvec3> catmullRom(const std::vector<glm::dvec3>& pts, int samplesPerSpan = 6);
// Point at arc length s along a polyline (clamped), and the unit tangent there.
glm::dvec3 pointAt(const std::vector<glm::dvec3>& pts, double s, glm::dvec3* tangent = nullptr);

// ---- solids of the fridge detail pass (MeshOpsSolids.cpp)
// 2:1 ellipsoidal dished head (pressure-vessel end) of rim radius r hanging below y = yTop, depth
// `depth` at the crown; normals outward or inward. A can's "dished" base.
MeshData dishedHead(float r, float depth, float yTop, int seg, int stacks, bool outward);
// Plate face of radius r at height y around one optional through-hole (centre hx, hz, radius hr,
// all in the plate's XZ plane): a generalised annulus swept around the hole so the hole is a real
// opening. `angles` receives the plate-centred azimuth of each outer boundary vertex, so the rim
// built by plateRim shares those vertices. Without a hole (hr <= 0) it is a fan of `seg` steps.
MeshData plateFace(float r, float y, float hx, float hz, float hr, int seg, bool up,
                   std::vector<float>& angles);
// Chamfered rim of a plate between yTop and yBot: 45° chamfer bands of size `c` on both edges and
// the vertical wall between them, on the boundary azimuths of plateFace.
MeshData plateRim(float r, float yTop, float yBot, float c, const std::vector<float>& angles);
// 40×40-class aluminium T-slot extrusion profile (four grooves) of side `s` centred on the origin,
// counter-clockwise in the XZ plane.
std::vector<glm::vec2> tSlotProfile(float s, float slot, float slotDepth, float lip);
// Extrudes an XZ profile along the segment a → b (any direction).
MeshData extrudeBetween(const std::vector<glm::vec2>& profileXZ, glm::vec3 a, glm::vec3 b);
// Hexagon-head bolt: hex head of across-flats `af` and height `hh` above y = 0, shank of radius
// `rs` and length `ls` below it (axis +Y).
MeshData hexBolt(float af, float hh, float rs, float ls);

// Keeps the part of a mesh with dot(plane.xyz, p) + plane.w >= 0 (object space); triangles that
// straddle the plane are split. Used for the cutaway clip of cans (spec 17 §7.5).
MeshData clipByPlane(const MeshData& m, const glm::dvec4& plane);

// Structural check used by tests and debug builds: index range, triangle list, finite positions,
// unit normals (|n| within `normalTol` of 1). Returns an empty string when valid.
std::string validate(const MeshData& m, double normalTol = 1e-3);

} // namespace qlab::lab::mesh
