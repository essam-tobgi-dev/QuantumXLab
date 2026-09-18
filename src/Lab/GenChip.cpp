// Spec 17 §6 — chip-scale generators: Cpw(path, w, s), Meander(L, pitch, w, s, turns),
// Xmon(arm_L, arm_w, gap). Geometry lies in the chip plane mapped to local (x, −y) → (X, Z); film
// heights follow the exaggerated stack of Generators.hpp.
#include "Lab/Generators.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::lab {

std::vector<glm::dvec2> meanderCenterline(double L, double pitch, double amp, double lead) {
    std::vector<glm::dvec2> p;
    double run = L - 2.0 * lead;
    int turns = std::max(1, static_cast<int>(std::floor(run / (pitch + 2.0 * amp))));
    // the remainder widens the excursions instead of lengthening a lead, so the footprint stays compact
    double ampEff = 0.5 * (run / turns - pitch);
    if (ampEff <= 0.0) return p;
    double x = 0.0;
    p.emplace_back(0.0, 0.0);
    if (lead > 0.0) p.emplace_back(x += lead, 0.0);
    for (int i = 0; i < turns; ++i) {
        double y = (i % 2 == 0) ? ampEff : -ampEff;
        p.emplace_back(x, y);
        x += pitch;
        p.emplace_back(x, y);
    }
    p.emplace_back(x, 0.0);
    if (lead > 0.0) p.emplace_back(x + lead, 0.0);
    return p;
}

} // namespace qlab::lab

namespace qlab::lab::gen {

using gfx::MeshData;
namespace {
const glm::vec4 kMetal{0.86f, 0.87f, 0.90f, 1.0f};
const glm::vec4 kGapColor{0.10f, 0.10f, 0.13f, 1.0f};

std::vector<glm::vec2> toXZ(const std::vector<glm::dvec2>& chip, double scale) {
    std::vector<glm::vec2> out;
    out.reserve(chip.size());
    for (const auto& q : chip) out.push_back(chipToXZ(q, scale));
    return out;
}

// Centre conductor [0, metalTop] plus the two gap cut-outs of width s on either side, drawn as
// dark films between the ground-plane top and the gap top.
MeshData cpwStrips(const std::vector<glm::vec2>& P, float w, float s, const GenContext& c, bool gaps) {
    MeshData m;
    mesh::appendColored(m, mesh::ribbon(P, w, 0.0f, c.F(kFilmMetalTop_m)), kMetal);
    if (!gaps) return m;
    for (float side : {-1.0f, 1.0f}) {
        std::vector<glm::vec2> off = mesh::offsetPolyline(P, side * 0.5f * (w + s));
        mesh::appendColored(m, mesh::ribbon(off, s, c.F(kFilmGround_m), c.F(kFilmGapTop_m)), kGapColor);
    }
    return m;
}
} // namespace

Result<MeshData> cpw(const GenParams& p, const GenContext& c) {
    std::vector<glm::dvec2> path = p.points2("path");
    if (path.size() < 2) path = {glm::dvec2(0.0), glm::dvec2(1e-3, 0.0)};
    std::vector<glm::vec2> P = mesh::dedupe2(toXZ(path, c.unitScale), 0.0f);
    if (P.size() < 2) return fail(kErrGeometry, "Cpw: path collapses to a point");
    float w = c.F(p.length("w", 10e-6)), s = c.F(p.length("s", 6e-6));
    return cpwStrips(P, w, s, c, c.detail == Detail::Full);
}

Result<MeshData> meander(const GenParams& p, const GenContext& c) {
    double L = p.length("length").value_or(quarterWaveLength_m(p.number("f_r_hz", 7e9)));
    double pitch = p.length("pitch", 50e-6), amp = p.length("amplitude", 150e-6), lead = p.length("lead", 50e-6);
    std::vector<glm::dvec2> centre = meanderCenterline(L, pitch, amp, lead);
    if (centre.size() < 2) return fail(kErrGeometry, "Meander: length shorter than one turn plus leads");
    std::vector<glm::vec2> P = toXZ(centre, c.unitScale);
    float w = c.F(p.length("w", 10e-6)), s = c.F(p.length("s", 6e-6));
    return cpwStrips(P, w, s, c, c.detail == Detail::Full);
}

// Xmon cross with arms of length arm_L (centre to tip) and width arm_w, surrounded by an etched
// gap `gap`; `scale` shrinks a coupler (spec 17 §3.5 coupler_tunable, scale 0.6).
Result<MeshData> xmon(const GenParams& p, const GenContext& c) {
    double scale = p.number("scale", 1.0);
    float L = c.F(p.length("arm_length", 150e-6) * scale), a = 0.5f * c.F(p.length("arm_width", 24e-6) * scale);
    float g = c.F(p.length("gap", 24e-6) * scale);
    auto cross = [](float len, float half) {
        return std::vector<glm::vec2>{{len, -half},  {len, half},   {half, half},   {half, len},
                                      {-half, len},  {-half, half}, {-len, half},   {-len, -half},
                                      {-half, -half}, {-half, -len}, {half, -len},  {half, -half}};
    };
    MeshData m;
    mesh::appendColored(m, mesh::prism(cross(L, a), 0.0f, c.F(kFilmMetalTop_m)), kMetal);
    if (c.detail == Detail::Full)
        mesh::appendColored(m, mesh::prism(cross(L + g, a + g), c.F(kFilmGround_m), c.F(kFilmGapTop_m)), kGapColor);
    return m;
}

} // namespace qlab::lab::gen
