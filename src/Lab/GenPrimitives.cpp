// Spec 17 §6 — Box, Cylinder, Sphere, Torus, Octagon. Features (ports, fins, attachments) are
// fitted inside the requested envelope so the part's bounding box is the size the descriptor
// states. Detail::Simple is the bare envelope primitive.
#include "Lab/Generators.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace qlab::lab::gen {

using gfx::MeshData;
namespace {
const glm::vec4 kDark{0.08f, 0.08f, 0.09f, 1.0f};
const glm::vec4 kGold{1.0f, 0.82f, 0.45f, 1.0f};
const glm::vec4 kPanel{0.78f, 0.80f, 0.82f, 1.0f};

// Visible-spectrum approximation for beam colours (laser_path `colour_from: wavelength`);
// UV and IR lines are shown at the violet / deep-red ends (Illustrative).
glm::vec4 wavelengthColor(double nm) {
    double t = std::clamp((nm - 380.0) / (780.0 - 380.0), 0.0, 1.0);
    if (nm < 380.0) return {0.55f, 0.2f, 1.0f, 1.0f};
    if (nm > 780.0) return {0.6f, 0.05f, 0.05f, 1.0f};
    auto f = static_cast<float>(t);
    return {std::clamp(2.0f * f - 0.6f, 0.0f, 1.0f), std::clamp(1.0f - std::abs(2.0f * f - 1.0f) * 1.3f, 0.0f, 1.0f),
            std::clamp(1.2f - 2.0f * f, 0.0f, 1.0f), 1.0f};
}
} // namespace

Result<MeshData> box(const GenParams& p, const GenContext& c) {
    glm::vec3 size{c.F(p.length("w", 0.1)), c.F(p.length("h", 0.1)), c.F(p.length("d", 0.1))};
    if (c.detail == Detail::Simple) return gfx::shapes::box(size);
    MeshData m;
    if (p.boolean("wireframe", false)) { // package cavity outline: 12 edge bars inside the envelope
        float e = 0.04f * std::min({size.x, size.y, size.z});
        glm::vec3 h = size * 0.5f - glm::vec3(e * 0.5f);
        for (int axis = 0; axis < 3; ++axis)
            for (int s1 = -1; s1 <= 1; s1 += 2)
                for (int s2 = -1; s2 <= 1; s2 += 2) {
                    glm::vec3 center(0.0f), bar(e);
                    int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
                    center[a1] = static_cast<float>(s1) * h[a1];
                    center[a2] = static_cast<float>(s2) * h[a2];
                    bar[axis] = size[axis];
                    mesh::append(m, mesh::box(center, bar));
                }
        return m;
    }
    int ports = p.integer("ports", 0);
    std::string marking = p.string("marking");
    float stub = ports > 0 ? std::min(0.2f * size.x, c.F(0.004)) : 0.0f;
    float fin = marking == "heatsink" ? 0.25f * size.y : (marking == "arrow" ? std::min(0.05f * size.y, c.F(0.001)) : 0.0f);
    float panel = p.string("front") == "valve_mimic" ? std::min(0.02f * size.z, c.F(0.012)) : 0.0f;
    glm::vec3 body{size.x - 2.0f * stub, size.y - fin, size.z - panel};
    mesh::append(m, mesh::box({0.0f, -fin * 0.5f, -panel * 0.5f}, body));
    for (int i = 0; i < ports; ++i) { // SMA ports alternating on the −x / +x faces
        float side = (i % 2 == 0) ? -1.0f : 1.0f;
        int perSide = (ports + 1) / 2;
        float z = body.z * ((static_cast<float>(i / 2) + 0.5f) / static_cast<float>(perSide) - 0.5f);
        float r = std::min(0.3f * std::min(body.y, body.z / static_cast<float>(perSide)), c.F(0.0016));
        MeshData cyl = gfx::shapes::cylinder(r, stub, 12, true);
        glm::mat4 xf = mesh::translate({side * (body.x * 0.5f + stub * 0.5f), -fin * 0.5f, z - panel * 0.5f}) *
                       mesh::alignY({1.0f, 0.0f, 0.0f});
        mesh::appendColored(m, std::move(cyl), kGold, xf);
    }
    if (marking == "heatsink")
        for (int i = 0; i < 6; ++i) {
            float x = body.x * ((static_cast<float>(i) + 0.5f) / 6.0f - 0.5f);
            mesh::appendColored(m, mesh::box({x, size.y * 0.5f - fin * 0.5f, 0.0f}, {body.x / 14.0f, fin, size.z}), kDark);
        }
    if (marking == "arrow") {
        float L = 0.6f * body.z, W = 0.25f * body.x, y0 = size.y * 0.5f - fin, y1 = size.y * 0.5f;
        std::vector<glm::vec2> arrow{{-0.15f * W, -0.5f * L}, {0.15f * W, -0.5f * L}, {0.15f * W, 0.1f * L},
                                     {0.5f * W, 0.1f * L},   {0.0f, 0.5f * L},       {-0.5f * W, 0.1f * L},
                                     {-0.15f * W, 0.1f * L}};
        mesh::appendColored(m, mesh::prism(arrow, y0, y1), kPanel);
    }
    if (panel > 0.0f) { // GHS front panel with a valve mimic: light panel, dark flow lines, valve discs
        float zf = size.z * 0.5f - panel * 0.5f;
        mesh::appendColored(m, mesh::box({0.0f, 0.05f * size.y, zf}, {0.9f * size.x, 0.6f * size.y, panel * 0.5f}), kPanel);
        for (int row = 0; row < 4; ++row) {
            float y = size.y * (0.3f - 0.15f * static_cast<float>(row));
            mesh::appendColored(m, mesh::box({0.0f, y, zf + panel * 0.3f}, {0.8f * size.x, 0.004f * size.y, panel * 0.4f}), kDark);
            for (int k = 0; k < 5; ++k) {
                float x = size.x * (0.16f * static_cast<float>(k) - 0.32f);
                MeshData v = gfx::shapes::cylinder(0.025f * size.x, panel * 0.5f, 12, true);
                mesh::appendColored(m, std::move(v), kDark, mesh::translate({x, y, zf + panel * 0.25f}) * mesh::alignY({0, 0, 1}));
            }
        }
    }
    return m;
}

Result<MeshData> cylinder(const GenParams& p, const GenContext& c) {
    float r = c.F(p.length("r", 0.05)), h = c.F(p.length("h", 0.1));
    bool caps = p.boolean("caps", true);
    if (c.detail == Detail::Simple) return gfx::shapes::cylinder(r, h, 12, caps);
    MeshData m;
    glm::vec4 body = mesh::kWhite;
    if (p.string("colour_from") == "wavelength") body = wavelengthColor(p.number("wavelength_nm", 369.0));
    std::vector<std::string> att = p.strings("attachments");
    auto hasAtt = [&](std::string_view a) { return std::find(att.begin(), att.end(), a) != att.end(); };
    if (hasAtt("rotary_valve_box")) {
        mesh::appendColored(m, gfx::shapes::cylinder(r, 0.8f * h, 48, caps), body, mesh::translate({0, -0.1f * h, 0}));
        mesh::appendColored(m, mesh::box({0, 0.4f * h, 0}, {1.2f * r, 0.2f * h, 1.2f * r}), kDark);
    } else if (hasAtt("sma_bulkheads")) {
        mesh::appendColored(m, gfx::shapes::cylinder(0.85f * r, h, 48, caps), body);
        for (int k = 0; k < 8; ++k) {
            float a = glm::two_pi<float>() * static_cast<float>(k) / 8.0f;
            glm::vec3 dir{std::cos(a), 0.0f, std::sin(a)};
            MeshData s = gfx::shapes::cylinder(0.05f * r, 0.15f * r, 12, true);
            mesh::appendColored(m, std::move(s), kGold, mesh::translate(dir * (0.925f * r)) * mesh::alignY(dir));
        }
    } else if (hasAtt("flange")) { // a flange ring at the top (default) or the bottom end
        float sgn = p.string("flange_at", "top") == "bottom" ? -1.0f : 1.0f;
        float ft = std::min(0.15f * h, c.F(0.012));
        mesh::appendColored(m, gfx::shapes::cylinder(0.75f * r, h - ft, 48, caps), body, mesh::translate({0, -sgn * 0.5f * ft, 0}));
        mesh::appendColored(m, gfx::shapes::cylinder(r, ft, 48, true), body, mesh::translate({0, sgn * (0.5f * h - 0.5f * ft), 0}));
    } else if (hasAtt("coil")) {
        mesh::appendColored(m, gfx::shapes::cylinder(0.9f * r, h, 48, caps), body);
        for (int k = -1; k <= 1; ++k)
            mesh::appendColored(m, gfx::shapes::torus(0.95f * r, 0.05f * r, 48, 8), kGold, mesh::translate({0, 0.25f * h * static_cast<float>(k), 0}));
    } else if (hasAtt("handle")) {
        mesh::appendColored(m, gfx::shapes::cylinder(r, 0.6f * h, 32, caps), body, mesh::translate({0, -0.2f * h, 0}));
        mesh::appendColored(m, gfx::shapes::cylinder(0.15f * r, 0.25f * h, 12, true), kDark, mesh::translate({0, 0.225f * h, 0}));
        mesh::appendColored(m, mesh::box({0, 0.425f * h, 0}, {2.0f * r, 0.15f * h, 0.2f * r}), kDark);
    } else {
        mesh::appendColored(m, gfx::shapes::cylinder(r, h, 48, caps), body);
    }
    if (p.string("front") == "dial") { // pressure-gauge dial face and needle on the +y cap
        mesh::appendColored(m, mesh::disc(0.85f * r, 0.5f * h + 0.0f, 32, true), kPanel, mesh::translate({0, -0.01f * h, 0}));
        mesh::appendColored(m, mesh::box({0.25f * r, 0.495f * h, 0}, {0.5f * r, 0.01f * h, 0.04f * r}), kDark);
    }
    return m;
}

Result<MeshData> sphere(const GenParams& p, const GenContext& c) {
    float r = c.F(p.length("r", 1e-5));
    bool full = c.detail == Detail::Full;
    return gfx::shapes::sphere(r, full ? 24 : 8, full ? 12 : 4);
}

Result<MeshData> torus(const GenParams& p, const GenContext& c) {
    float R = c.F(p.length("r", 0.15)), tr = c.F(p.length("tube_r", 0.01));
    int count = std::max(1, p.integer("count", 1));
    bool full = c.detail == Detail::Full;
    MeshData m;
    for (int k = 0; k < count; ++k) { // coil pairs at Helmholtz spacing (separation = R)
        float y = R * (static_cast<float>(k) - 0.5f * static_cast<float>(count - 1));
        mesh::append(m, gfx::shapes::torus(R, tr, full ? 48 : 16, full ? 12 : 6), mesh::translate({0, y, 0}));
    }
    return m;
}

Result<MeshData> octagon(const GenParams& p, const GenContext& c) {
    float r = c.F(p.length("r", 0.12)), h = c.F(p.length("h", 0.1));
    MeshData m = mesh::ngonPrism(8, r, -0.5f * h, 0.5f * h);
    if (c.detail == Detail::Simple) return m;
    float apothem = r * std::cos(glm::pi<float>() / 8.0f);
    float side = 2.0f * r * std::sin(glm::pi<float>() / 8.0f);
    for (int k = 0; k < 8; k += 2) { // viewports on alternate faces
        float a = glm::two_pi<float>() * (static_cast<float>(k) + 0.5f) / 8.0f;
        glm::vec3 dir{std::cos(a), 0.0f, std::sin(a)};
        MeshData v = gfx::shapes::cylinder(0.3f * side, 0.01f * r, 24, true);
        glm::vec4 glass{0.55f, 0.75f, 0.95f, 1.0f};
        mesh::appendColored(m, std::move(v), glass, mesh::translate(dir * (apothem - 0.004f * r)) * mesh::alignY(dir));
    }
    return m;
}

} // namespace qlab::lab::gen
