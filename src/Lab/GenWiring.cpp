// Spec 17 §6 — Tube(spline, r), Spiral(r, pitch, turns, tube_r), CylinderSma, SmaConnector,
// WirebondArc(p0, p1, h).
#include "Lab/Generators.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace qlab::lab::gen {

using gfx::MeshData;
namespace {
const glm::vec4 kNut{0.85f, 0.72f, 0.40f, 1.0f};
const glm::vec4 kBarrel{0.92f, 0.80f, 0.50f, 1.0f};
const glm::vec4 kPtfe{0.95f, 0.95f, 0.92f, 1.0f};
const glm::vec4 kBand{0.15f, 0.35f, 0.85f, 1.0f};
const glm::vec4 kPad{0.85f, 0.85f, 0.88f, 1.0f};

std::vector<glm::vec3> toFloat(const std::vector<glm::dvec3>& pts, double scale) {
    std::vector<glm::vec3> out;
    out.reserve(pts.size());
    for (const auto& q : pts) out.emplace_back(q * scale);
    return mesh::dedupe(out, 0.0f);
}

// "straight_0.1m" → 0.1
double presetLength(const std::string& path) {
    if (path.rfind("straight_", 0) != 0) return 0.1;
    std::string num = path.substr(9);
    if (!num.empty() && num.back() == 'm') num.pop_back();
    double v = 0.1;
    std::from_chars(num.data(), num.data() + num.size(), v);
    return v > 0.0 ? v : 0.1;
}
} // namespace

// Coax runs, capillaries, flex lines: a tube swept along `points_m` (Catmull–Rom smoothed at full
// detail); without points, a vertical straight run whose length comes from the `path` preset.
Result<MeshData> tube(const GenParams& p, const GenContext& c) {
    float r = c.F(p.length("r", 0.00043));
    std::vector<glm::dvec3> pts = p.points3("points");
    if (pts.size() < 2) {
        double L = presetLength(p.string("path"));
        pts = {glm::dvec3(0, 0.5 * L, 0), glm::dvec3(0, -0.5 * L, 0)};
    }
    std::vector<glm::vec3> f = toFloat(pts, c.unitScale);
    if (f.size() < 2) return fail(kErrGeometry, "Tube: path collapses to a point");
    bool full = c.detail == Detail::Full;
    // `bundle`: n parallel runs side by side (a loom ribbon, a copper braid, a cable bundle),
    // offset across the path in the horizontal-perpendicular direction at `bundle_pitch`. The
    // simple level keeps the ribbon's width with three coarse runs.
    const int authored = std::max(1, p.integer("bundle", 1));
    if (authored == 1) return gfx::shapes::tube(f, r, full ? 12 : 5, full && f.size() >= 3);
    const int bundle = full ? authored : std::min(authored, 3);
    const float width = c.F(p.length("bundle_pitch", 2.2 * r / c.unitScale)) * static_cast<float>(authored - 1);
    const float pitch = bundle > 1 ? width / static_cast<float>(bundle - 1) : 0.0f;
    MeshData m;
    for (int k = 0; k < bundle; ++k) {
        float off = (static_cast<float>(k) - 0.5f * static_cast<float>(bundle - 1)) * pitch;
        std::vector<glm::vec3> shifted;
        shifted.reserve(f.size());
        for (std::size_t i = 0; i < f.size(); ++i) {
            glm::vec3 tangent = f[std::min(i + 1, f.size() - 1)] - f[i > 0 ? i - 1 : 0];
            glm::vec3 side = glm::cross(tangent, glm::vec3(0, 1, 0));
            if (glm::length(side) < 1e-9f) side = glm::cross(tangent, glm::vec3(1, 0, 0));
            shifted.push_back(f[i] + glm::normalize(side) * off);
        }
        mesh::append(m, gfx::shapes::tube(shifted, r, full ? 8 : 5, full && f.size() >= 3));
    }
    return m;
}

Result<MeshData> spiral(const GenParams& p, const GenContext& c) {
    double r = p.length("r", 0.05), pitch = p.length("pitch", 0.01), tr = p.length("tube_r", 0.003);
    double turns = std::max(0.25, p.number("turns", 5.0));
    int perTurn = c.detail == Detail::Full ? 24 : 8;
    auto samples = static_cast<int>(std::ceil(turns * perTurn));
    double height = pitch * turns;
    std::vector<glm::vec3> pts;
    for (int i = 0; i <= samples; ++i) {
        double a = glm::two_pi<double>() * turns * static_cast<double>(i) / samples;
        double y = 0.5 * height - height * static_cast<double>(i) / samples;
        pts.emplace_back(glm::dvec3(r * std::cos(a), y, r * std::sin(a)) * c.unitScale);
    }
    MeshData m = gfx::shapes::tube(pts, c.F(tr), c.detail == Detail::Full ? 8 : 4, false);
    // `core_r`: the central support / return tube the coil is wound on (continuous heat exchanger).
    if (auto core = p.length("core_r"); core && *core > 0.0)
        mesh::append(m, gfx::shapes::cylinder(c.F(*core), static_cast<float>(height * c.unitScale), 24, true));
    return m;
}

// Inline SMA part (attenuator, IR filter): body of `diameter` over 60 % of `length`, a hex nut and
// a threaded barrel at each end; axis +Y, total length = `length`.
Result<MeshData> cylinderSma(const GenParams& p, const GenContext& c) {
    float L = c.F(p.length("length", 0.025)), D = c.F(p.length("diameter", 0.009));
    if (c.detail == Detail::Simple) return gfx::shapes::cylinder(0.5f * D, L, 10, true);
    MeshData m;
    mesh::append(m, gfx::shapes::cylinder(0.5f * D, 0.6f * L, 32, true));
    mesh::appendColored(m, gfx::shapes::cylinder(0.502f * D, 0.08f * L, 32, false), kBand, mesh::translate({0, 0.1f * L, 0}));
    for (float s : {-1.0f, 1.0f}) {
        mesh::appendColored(m, mesh::ngonPrism(6, 0.42f * D, -0.04f * L, 0.04f * L), kNut, mesh::translate({0, s * 0.34f * L, 0}));
        mesh::appendColored(m, gfx::shapes::cylinder(0.27f * D, 0.12f * L, 16, true), kBarrel, mesh::translate({0, s * 0.44f * L, 0}));
    }
    return m;
}

// Bulkhead SMA feedthrough through a plate: barrel 6.35 mm, clamping hex nut 8 mm across flats,
// PTFE insulator face at the top; 20 mm long, axis +Y centred.
Result<MeshData> smaConnector(const GenParams& p, const GenContext& c) {
    float L = c.F(kSmaLength_m), Rc = c.F(0.5 * kSmaHexAcrossCorners_m);
    if (c.detail == Detail::Simple) return mesh::ngonPrism(6, Rc, -0.5f * L, 0.5f * L);
    MeshData m;
    mesh::appendColored(m, gfx::shapes::cylinder(c.F(0.003175), 0.96f * L, 24, true), kBarrel, mesh::translate({0, -0.02f * L, 0}));
    mesh::appendColored(m, mesh::ngonPrism(6, Rc, -0.1f * L, 0.1f * L), kNut);
    mesh::appendColored(m, mesh::ngonPrism(6, 0.9f * Rc, 0.25f * L, 0.35f * L), kNut);
    mesh::appendColored(m, gfx::shapes::cylinder(c.F(0.002), 0.08f * L, 16, true), kPtfe, mesh::translate({0, 0.46f * L, 0}));
    return m;
}

// Bond pad plus its aluminium wire: pad (`pad` square) at p0 and a tube of radius `wire_r` along a
// parabola from p0 to p1 peaking `h` above the higher end. Defaults: p0 at the origin,
// p1 500 µm along +x.
Result<MeshData> wirebondArc(const GenParams& p, const GenContext& c) {
    glm::dvec3 p0 = p.point3("p0").value_or(glm::dvec3(0.0));
    glm::dvec3 p1 = p.point3("p1").value_or(glm::dvec3(500e-6, 0.0, 0.0));
    double h = p.length("h", 300e-6), pad = p.length("pad", 90e-6), wr = p.length("wire_r", 12.5e-6);
    MeshData m;
    glm::vec3 a = glm::vec3(p0 * c.unitScale);
    mesh::appendColored(m, mesh::box(a + glm::vec3(0, c.F(0.5 * kFilmMetalTop_m), 0), {c.F(pad), c.F(kFilmMetalTop_m), c.F(pad)}), kPad);
    int n = c.detail == Detail::Simple ? 2 : 16;
    std::vector<glm::vec3> pts;
    glm::dvec3 lift(0.0, kFilmMetalTop_m, 0.0);
    double peak = std::max(p0.y, p1.y) + h;
    for (int i = 0; i <= n; ++i) {
        double t = static_cast<double>(i) / n;
        glm::dvec3 q = glm::mix(p0 + lift, p1, t);
        // quadratic through both ends reaching `peak` at t = 0.5 (heights measured from the chord)
        double chord = glm::mix(p0.y + lift.y, p1.y, t);
        q.y = chord + 4.0 * t * (1.0 - t) * (peak - 0.5 * (p0.y + lift.y + p1.y));
        pts.emplace_back(q * c.unitScale);
    }
    mesh::appendColored(m, gfx::shapes::tube(mesh::dedupe(pts, 0.0f), c.F(wr), 6, false), kPad);
    return m;
}

} // namespace qlab::lab::gen
