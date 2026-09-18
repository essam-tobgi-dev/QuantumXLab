// Spec 17 §6 — Can(r, h, t, base, open, flange, flange_bolts) and Post(d, L, wall, collar): the
// vacuum can and radiation shields with their top flange and bolt circle, and the stage support
// posts of the chandelier (fridge detail pass).
#include "Lab/Generators.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace qlab::lab::gen {

using gfx::MeshData;
namespace {
const glm::vec4 kBolt{0.62f, 0.62f, 0.60f, 1.0f};
const glm::vec4 kCollar{0.72f, 0.72f, 0.70f, 1.0f};
} // namespace

// Thin-walled can hanging from its mounting (open) end at y = 0: open "top" extends toward −y,
// open "bottom" toward +y. `h` is the cylindrical wall height; the base adds r ("hemi") or r/2
// (2:1 ellipsoidal "dished" head). `flange: true` adds the mounting flange ring at the open end
// (thickness `flange_t`, radial width `flange_w`) with `flange_bolts` hex-head bolts on its bolt
// circle at full detail. Simple level: the plain cylinder.
Result<MeshData> can(const GenParams& p, const GenContext& c) {
    float r = c.F(p.length("r", 0.2)), h = c.F(p.length("h", 1.0));
    float t = std::min(c.F(p.length("t", 0.002)), 0.5f * r);
    std::string base = p.string("base", "flat");
    bool openBottom = p.string("open", "top") == "bottom";
    bool full = c.detail == Detail::Full;
    int seg = full ? 96 : 24, stacks = full ? 12 : 4;
    MeshData m;
    mesh::append(m, mesh::wall(r, -h, 0.0f, seg, true));
    mesh::append(m, mesh::wall(r - t, -h, 0.0f, seg, false));
    mesh::append(m, mesh::annulus(r - t, r, 0.0f, seg, true));
    if (base == "hemi") {
        mesh::append(m, mesh::dome(r, -h, seg, stacks, true));
        mesh::append(m, mesh::dome(r - t, -h, seg, stacks, false));
    } else if (base == "dished") {
        mesh::append(m, mesh::dishedHead(r, 0.5f * r, -h, seg, stacks, true));
        mesh::append(m, mesh::dishedHead(r - t, 0.5f * r - t, -h, seg, stacks, false));
    } else {
        mesh::append(m, mesh::disc(r, -h, seg, false));
        mesh::append(m, mesh::disc(r - t, -h + t, seg, true));
    }
    if (p.boolean("flange", false)) {
        float ft = c.F(p.length("flange_t", 0.012)), fw = c.F(p.length("flange_w", 0.015));
        mesh::append(m, mesh::wall(r + fw, -ft, 0.0f, seg, true));
        mesh::append(m, mesh::annulus(r, r + fw, 0.0f, seg, true));
        mesh::append(m, mesh::annulus(r, r + fw, -ft, seg, false));
        int bolts = full ? p.integer("flange_bolts", 0) : 0;
        if (bolts > 0) { // hex-head bolts standing proud of the flange, M8 heads (13 mm across flats)
            float af = std::min(0.8f * fw, c.F(0.013)), hh = c.F(0.006);
            MeshData bolt = mesh::hexBolt(af, hh, 0.0f, 0.0f);
            bolt.setColor(kBolt);
            for (int k = 0; k < bolts; ++k) {
                float a = glm::two_pi<float>() * (static_cast<float>(k) + 0.5f) / static_cast<float>(bolts);
                glm::vec3 at{(r + 0.5f * fw) * std::cos(a), -ft, (r + 0.5f * fw) * std::sin(a)};
                mesh::append(m, bolt, glm::rotate(mesh::translate(at), -a, {0.0f, 1.0f, 0.0f}) * glm::mat4(glm::mat3(1.0f, 0, 0, 0, -1.0f, 0, 0, 0, 1.0f)));
            }
        }
    }
    if (openBottom) { // mirror about y = 0 and restore winding against the mirrored normals
        for (auto& v : m.vertices) {
            v.position.y = -v.position.y;
            v.normal.y = -v.normal.y;
        }
    }
    mesh::orientToNormals(m);
    return m;
}

// Stage support post: a hollow tube (outer diameter d, wall `wall`) of length L along +Y centred on
// the origin, with a hex collar (across flats 1.6 d, height `collar`) at both ends. Stainless above
// 4 K, G10 / Vespel below it; the material is the node's. Simple level: a 6-gon prism.
Result<MeshData> post(const GenParams& p, const GenContext& c) {
    float d = c.F(p.length("d", 0.020)), L = c.F(p.length("L", 0.2));
    float wall = std::min(c.F(p.length("wall", 0.0005)), 0.45f * d);
    float collar = std::min(c.F(p.length("collar", 0.008)), 0.25f * L);
    float r = 0.5f * d;
    if (c.detail == Detail::Simple) return mesh::ngonPrism(6, r, -0.5f * L, 0.5f * L);
    MeshData m;
    int seg = 24;
    float y0 = -0.5f * L + collar, y1 = 0.5f * L - collar;
    mesh::append(m, mesh::wall(r, y0, y1, seg, true));
    if (wall > 0.0f) mesh::append(m, mesh::wall(r - wall, y0, y1, seg, false));
    MeshData top = mesh::ngonPrism(6, 0.8f * d / std::sqrt(3.0f), y1, 0.5f * L);
    top.setColor(kCollar);
    mesh::append(m, top);
    MeshData bottom = mesh::ngonPrism(6, 0.8f * d / std::sqrt(3.0f), -0.5f * L, y0);
    bottom.setColor(kCollar);
    mesh::append(m, bottom);
    return m;
}

} // namespace qlab::lab::gen
