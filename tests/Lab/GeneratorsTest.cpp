// Spec 17 §6 — every generator produces a well-formed mesh whose bounding box is the size the
// parameters ask for (5 % tolerance), at every detail level, and consistently wound.
#include "Lab/Catalog.hpp"
#include "Lab/Generators.hpp"
#include "Lab/MeshLibrary.hpp"
#include "Lab/MeshOps.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <set>

using namespace qlab;
using namespace qlab::lab;

namespace {

struct Case {
    const char* generator;
    core::Json params;
    double unitScale;
    glm::dvec3 expected; // object-space bounding-box size in output units
};

std::vector<Case> cases() {
    const double c = 299792458.0;
    double meanderL = c / (4.0 * 7e9 * std::sqrt(6.45)) * 1e6; // µm
    std::vector<glm::dvec2> centre = meanderCenterline(meanderL, 50.0, 150.0, 50.0);
    double lenX = centre.back().x, amp = 0.0;
    for (const auto& p : centre) amp = std::max(amp, std::abs(p.y));
    return {
        {"Box", {{"w_m", 0.03}, {"h_m", 0.02}, {"d_m", 0.04}, {"ports", 3}, {"marking", "arrow"}}, 1.0, {0.03, 0.02, 0.04}},
        {"Box", {{"w_m", 0.03}, {"h_m", 0.01}, {"d_m", 0.03}, {"wireframe", true}}, 1.0, {0.03, 0.01, 0.03}},
        {"Box", {{"w_m", 0.8}, {"h_m", 1.9}, {"d_m", 0.7}, {"front", "valve_mimic"}}, 1.0, {0.8, 1.9, 0.7}},
        {"Box", {{"w_m", 0.03}, {"h_m", 0.02}, {"d_m", 0.05}, {"marking", "heatsink"}}, 1.0, {0.03, 0.02, 0.05}},
        {"Plate", {{"r_m", 0.19}, {"t_m", 0.012}, {"holes", "bolt_circle"}}, 1.0, {0.38, 0.012, 0.38}},
        {"Plate", {{"r_m", 0.3}, {"t_m", 0.02}, {"holes", "feedthrough_ring"}, {"hole_count", 96}, {"hole_ring_m", 0.24}}, 1.0, {0.6, 0.02, 0.6}},
        {"Plate", {{"r_m", 0.014}, {"t_m", 0.0016}, {"shape", "square"}}, 1.0, {0.028, 0.0016, 0.028}},
        {"PlateWithHoles", {{"w_um", 8000}, {"d_um", 8000}, {"t_um", 2}, {"holes_um", {{0, 0, 300}, {1000, 500, 200}}}}, 1e6, {8000, 2, 8000}},
        {"RackUnit", {{"u", 2}, {"front", "sg_mw"}}, 1.0, {kRackPanelWidth_m, 2 * kRackUnit_m, kRackUnitDepth_m}},
        {"RackUnit", {{"u", 6}, {"front", "awg"}}, 1.0, {kRackPanelWidth_m, 6 * kRackUnit_m, kRackUnitDepth_m}},
        {"Cylinder", {{"r_m", 0.06}, {"h_m", 0.45}, {"caps", true}, {"attachments", {"rotary_valve_box"}}}, 1.0, {0.12, 0.45, 0.12}},
        {"Cylinder", {{"r_m", 0.025}, {"h_m", 0.05}, {"attachments", {"sma_bulkheads"}}}, 1.0, {0.05, 0.05, 0.05}},
        {"Cylinder", {{"r_m", 0.03}, {"h_m", 0.02}, {"front", "dial"}}, 1.0, {0.06, 0.02, 0.06}},
        {"Cylinder", {{"r_m", 0.015}, {"h_m", 0.02}, {"attachments", {"handle"}}}, 1.0, {0.03, 0.02, 0.03}},
        {"Cylinder", {{"r_m", 0.12}, {"h_m", 0.4}, {"attachments", {"coil"}}}, 1.0, {0.24, 0.4, 0.24}},
        {"Cylinder", {{"r_m", 0.06}, {"h_m", 0.15}, {"attachments", {"flange"}}}, 1.0, {0.12, 0.15, 0.12}},
        {"CylinderSma", {{"length_mm", 25}, {"diameter_mm", 9}}, 1.0, {0.009, 0.025, 0.009}},
        {"SmaConnector", core::Json::object(), 1.0, {kSmaHexAcrossCorners_m, kSmaLength_m, kSmaHexAcrossCorners_m * 0.866}},
        {"Tube", {{"r_m", 0.00043}, {"points_m", {{0, 0, 0}, {0, -0.5, 0}}}}, 1.0, {0.00086, 0.5, 0.00086}},
        {"Tube", {{"r_m", 0.01}, {"path", "straight_0.1m"}}, 1.0, {0.02, 0.1, 0.02}},
        {"Can", {{"r_m", 0.28}, {"h_m", 1.35}, {"t_m", 0.006}, {"base", "hemi"}, {"open", "top"}}, 1.0, {0.56, 1.63, 0.56}},
        {"Can", {{"r_m", 0.28}, {"h_m", 1.35}, {"t_m", 0.006}, {"base", "dished"}, {"open", "top"}, {"flange", true}, {"flange_w_m", 0.015}, {"flange_t_m", 0.012}, {"flange_bolts", 48}}, 1.0, {0.59, 1.49, 0.59}},
        {"Post", {{"d_m", 0.02}, {"L_m", 0.22}, {"wall_m", 0.0005}, {"collar_m", 0.008}}, 1.0, {0.02, 0.22, 0.02}},
        {"Plate", {{"r_m", 0.25}, {"t_m", 0.010}, {"holes", "bolt_circle"}, {"through_hole_m", {-0.087, 0.023, 0.03}}}, 1.0, {0.5, 0.010, 0.5}},
        {"Plate", {{"r_m", 0.3}, {"t_m", 0.02}, {"holes", "feedthrough_ring"}, {"kf_ports", 2}, {"viewport", true}}, 1.0, {0.6, 0.02 + 0.045, 0.6}},
        {"Frame", {{"width_m", 1.4}, {"depth_m", 1.4}, {"height_m", 2.6}, {"profile_mm", 40}, {"support_y_m", 0.99}}, 1.0, {1.4, 2.6, 1.4}},
        {"Tube", {{"r_m", 0.0012}, {"bundle", 12}, {"bundle_pitch_m", 0.0026}, {"points_m", {{0, 0, 0}, {0, -0.3, 0}}}}, 1.0, {0.0024, 0.3, 0.0024 + 11 * 0.0026}}, // a vertical run spreads its ribbon along z
        {"Spiral", {{"r_m", 0.04}, {"pitch_m", 0.01}, {"turns", 12}, {"tube_r_m", 0.003}, {"core_r_m", 0.008}}, 1.0, {0.086, 0.126, 0.086}},
        {"Can", {{"r_m", 0.245}, {"h_m", 1.15}, {"t_m", 0.002}, {"base", "flat"}, {"open", "top"}}, 1.0, {0.49, 1.15, 0.49}},
        {"Torus", {{"r_m", 0.15}, {"tube_r_m", 0.01}, {"count", 2}}, 1.0, {0.32, 0.17, 0.32}},
        {"Sphere", {{"r_um", 10}}, 1e6, {20, 20, 20}},
        {"Frame", {{"width_m", 1.4}, {"depth_m", 1.4}, {"height_m", 2.6}, {"profile_mm", 60}}, 1.0, {1.4, 2.6, 1.4}},
        {"Frame", {{"span_m", 1.4}, {"travel_m", 1.6}}, 1.0, {1.4, kGantryHeight_m, 1.6}},
        {"Octagon", {{"r_m", 0.12}, {"h_m", 0.1}}, 1.0, {0.24, 0.1, 0.24}},
        {"ExtrudedU", {{"points_m", {{0, 0, 0}, {2, 0, 0}}}}, 1.0, {2.0 + kTrayWidth_m, kTrayHeight_m, kTrayWidth_m}},
        {"Spiral", {{"r_m", 0.05}, {"pitch_m", 0.01}, {"turns", 20}, {"tube_r_m", 0.003}}, 1.0, {0.106, 0.206, 0.106}},
        {"Cpw", {{"path_um", {{0, 0}, {1000, 0}, {1000, 500}}}, {"w_um", 10}, {"s_um", 6}}, 1e6, {1022, kFilmMetalTop_m * 1e6, 522}},
        {"Meander", {{"f_r_hz", 7e9}, {"pitch_um", 50}, {"amplitude_um", 150}, {"lead_um", 50}}, 1e6, {lenX, kFilmMetalTop_m * 1e6, 2 * amp + 22}},
        {"Xmon", {{"arm_length_um", 150}, {"arm_width_um", 24}, {"gap_um", 24}}, 1e6, {348, kFilmMetalTop_m * 1e6, 348}},
        {"Xmon", {{"arm_length_um", 150}, {"gap_um", 24}, {"scale", 0.6}}, 1e6, {2 * 0.6 * 174, kFilmMetalTop_m * 1e6, 2 * 0.6 * 174}},
        {"Junction", {{"w_lead_um", 1.0}, {"overlap_um", 0.15}}, 1e6, {10.65, 2 * kFilmJunction_m * 1e6, 10.65}},
        {"SquidLoop", {{"area_um2", 60}, {"w_um", 1.0}}, 1e6, {std::sqrt(60.0) + 1.0, kFilmJunction_m * 1e6, std::sqrt(60.0) + 1.0}},
        {"Airbridge", {{"span_um", 30}, {"w_um", 10}, {"h_um", 3}}, 1e6, {30, 3.3, 10}},
        {"WirebondArc", {{"h_um", 300}}, 1e6, {557.5, 312.5, 90}},
        {"TrapChip", {{"electrodes", 40}}, 1.0, {kTrapChipSize_m[0], kTrapChipSize_m[1], kTrapChipSize_m[2]}},
    };
}

void checkWound(const gfx::MeshData& m) {
    std::size_t bad = 0;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const auto& a = m.vertices[m.indices[i]];
        const auto& b = m.vertices[m.indices[i + 1]];
        const auto& c = m.vertices[m.indices[i + 2]];
        glm::dvec3 g = glm::cross(glm::dvec3(b.position - a.position), glm::dvec3(c.position - a.position));
        if (glm::length(g) < 1e-18) continue; // degenerate (sphere poles)
        glm::dvec3 n = glm::dvec3(a.normal) + glm::dvec3(b.normal) + glm::dvec3(c.normal);
        if (glm::dot(g, n) < 0.0) ++bad;
    }
    CHECK(bad == 0);
}

} // namespace

TEST_CASE("every generator builds a valid mesh of the requested size") {
    for (const auto& c : cases()) {
        INFO(c.generator << " " << c.params.dump());
        GenParams p(c.params);
        auto full = generateMesh(c.generator, p, GenContext{Detail::Full, c.unitScale});
        REQUIRE(full.has_value());
        REQUIRE(mesh::validate(*full).empty());
        REQUIRE(full->triangleCount() > 0);
        checkWound(*full);
        glm::dvec3 size = aabbSize(meshBounds(*full));
        for (int k = 0; k < 3; ++k) {
            INFO("axis " << k << " got " << size[k] << " expected " << c.expected[k]);
            CHECK(std::abs(size[k] - c.expected[k]) <= 0.05 * c.expected[k]);
        }
        // simplified level: still valid, still non-empty, roughly the same envelope
        auto simple = generateMesh(c.generator, p, GenContext{Detail::Simple, c.unitScale});
        REQUIRE(simple.has_value());
        REQUIRE(mesh::validate(*simple).empty());
        REQUIRE(simple->triangleCount() > 0);
        CHECK(simple->triangleCount() <= full->triangleCount());
        glm::dvec3 ssize = aabbSize(meshBounds(*simple));
        for (int k = 0; k < 3; ++k) CHECK(std::abs(ssize[k] - c.expected[k]) <= 0.35 * c.expected[k]);
        // hidden level draws nothing
        auto hidden = generateMesh(c.generator, p, GenContext{Detail::Hidden, c.unitScale});
        REQUIRE(hidden.has_value());
        CHECK(hidden->triangleCount() == 0);
    }
}

TEST_CASE("the generator table covers the spec 17 §6 names and rejects unknown ones") {
    std::set<std::string_view> names(generatorNames().begin(), generatorNames().end());
    for (const char* n : {"Box", "Plate", "PlateWithHoles", "RackUnit", "Cylinder", "CylinderSma", "SmaConnector",
                          "Tube", "Can", "Torus", "Sphere", "Frame", "Octagon", "ExtrudedU", "Spiral", "Cpw",
                          "Meander", "Xmon", "Junction", "SquidLoop", "Airbridge", "WirebondArc", "TrapChip", "Post"})
        CHECK(names.count(n) == 1);
    CHECK(names.size() == 24);
    auto bad = generateMesh("Nope", GenParams{}, GenContext{});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == kErrGeometry);
}

TEST_CASE("resonator meanders have the electrical length of spec 17 §6") {
    // L = c / (4 f sqrt(eps_eff)) with eps_eff = (1 + eps_r)/2 = 6.45 for silicon
    const double c = 299792458.0;
    for (double f : {5.0e9, 7.0e9, 7.27e9}) {
        double expect = c / (4.0 * f * std::sqrt(6.45));
        CHECK(quarterWaveLength_m(f) == Catch::Approx(expect).epsilon(1e-12));
    }
    double L = quarterWaveLength_m(7.0e9) * 1e6;
    auto path = meanderCenterline(L, 50.0, 150.0, 50.0);
    REQUIRE(path.size() > 4);
    double len = 0.0;
    for (std::size_t i = 1; i < path.size(); ++i) len += glm::length(path[i] - path[i - 1]);
    CHECK(len == Catch::Approx(L).epsilon(1e-9));
    // the footprint stays compact: about 0.65 mm × 0.35 mm for a 4.2 mm resonator
    double amp = 0.0;
    for (const auto& p : path) amp = std::max(amp, std::abs(p.y));
    CHECK(path.back().x < 800.0);
    CHECK(amp < 200.0);
    CHECK(meanderCenterline(10.0, 50.0, 150.0, 50.0).empty()); // shorter than one turn
}

TEST_CASE("every shipped descriptor's geometry generates from its own parameters") {
    auto cat = ComponentCatalog::load();
    REQUIRE(cat.has_value());
    MeshLibrary lib;
    std::size_t built = 0;
    for (const auto& d : cat->all()) {
        GenParams p = GenParams::merged(d.geometry, core::Json::object());
        bool micro = d.category == "chip" || d.category == "qubit" || d.category == "resonator" ||
                     d.category == "coupler" || d.category == "line";
        GenContext ctx{Detail::Full, micro ? 1e6 : 1.0};
        for (const auto& rule : d.lod) {
            ctx.detail = rule.detail;
            auto m = generateMesh(d.generator, p, ctx);
            INFO(d.id << " (" << d.generator << ", " << detailName(rule.detail) << ")");
            REQUIRE(m.has_value());
            if (rule.detail == Detail::Hidden) continue;
            REQUIRE(mesh::validate(*m).empty());
            REQUIRE(m->triangleCount() > 0);
            lib.add(std::move(*m), p.cacheKey(d.generator, rule.detail, ctx.unitScale));
            ++built;
        }
    }
    CHECK(built >= 104);
    // the parameter cache collapses identical parts (e.g. the two heater blocks)
    CHECK(lib.cacheHits() > 0);
    CHECK(lib.size() < built);
}

TEST_CASE("a plate's through-hole is a real opening and its rim is chamfered") {
    GenParams p(core::Json{{"r_m", 0.25}, {"t_m", 0.010}, {"holes", "bolt_circle"}, {"through_hole_m", {-0.087, 0.023, 0.03}}});
    auto full = generateMesh("Plate", p, GenContext{Detail::Full, 1.0});
    REQUIRE(full.has_value());
    double nearest = 1e9, rimMax = 0.0;
    std::size_t chamferVerts = 0;
    for (const auto& v : full->vertices) {
        nearest = std::min(nearest, std::hypot(v.position.x + 0.087, v.position.z - 0.023));
        double rho = std::hypot(v.position.x, v.position.z);
        rimMax = std::max(rimMax, rho);
        // chamfer band: normals at 45° between radial and axial on the outer edge
        if (rho > 0.248 && std::abs(std::abs(v.normal.y) - 0.7071) < 1e-3) ++chamferVerts;
    }
    CHECK(nearest == Catch::Approx(0.03).margin(1e-6)); // hole wall exactly at the requested radius
    CHECK(rimMax == Catch::Approx(0.25).margin(1e-6));
    CHECK(chamferVerts > 100);
    // bolt circle: one counterbore per 45 mm on the ring 20 mm inside the rim, 24 or 36 of them
    CHECK(plateBoltCount(0.25) == 36);
    CHECK(plateBoltCount(0.19) == 24);
    CHECK(plateBoltRing_m(0.25) == Catch::Approx(0.23));
    // the simple level is a plain disc
    auto simple = generateMesh("Plate", p, GenContext{Detail::Simple, 1.0});
    REQUIRE(simple.has_value());
    CHECK(simple->triangleCount() < 200);
}
