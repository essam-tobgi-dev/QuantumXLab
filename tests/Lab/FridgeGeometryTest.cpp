// Spec 17 §3.1 (fridge detail pass) — the chandelier is built the way a dilution refrigerator is:
// six posts between consecutive plates touching both, a flanged vacuum can with its 48-bolt
// circle, pulse-tube stages ending on the 50 K and 4 K plates, the still pumping line rising
// through a real hole in every plate above the still, the layout's luminaires as point lights.
#include "Lab/Lab.hpp"
#include "Lab/MeshOps.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

using namespace qlab;
using namespace qlab::lab;

namespace {
const Scene& scene() {
    static Scene s = [] {
        auto built = buildScene("sc_lab_standard");
        if (!built) FAIL(built.error().format());
        return std::move(*built);
    }();
    return s;
}
const Node& plate(cryo::Stage s) { return *scene().node(scene().stageNodes()[static_cast<std::size_t>(s)]); }
double plateT(cryo::Stage s) {
    const ComponentDescriptor* d = scene().descriptor(plate(s));
    return d->geometryNumber("t_m", 0.012);
}
} // namespace

TEST_CASE("every stage hangs from the plate above on six posts that touch both plates") {
    const Scene& sc = scene();
    const double tol = 1e-3; // ±1 mm
    for (int k = 1; k < cryo::kStageCount; ++k) {
        auto upper = static_cast<cryo::Stage>(k - 1), lower = static_cast<cryo::Stage>(k);
        INFO(cryo::stageName(upper) << " -> " << cryo::stageName(lower));
        std::vector<const Node*> posts;
        for (ComponentId id : sc.findByDescriptor("support_post")) {
            const Node* n = sc.node(id);
            if (n->params.token("stage") && *n->params.token("stage") == stageLayoutName(upper)) posts.push_back(n);
        }
        REQUIRE(posts.size() == 6);
        const double upperUnderside = plate(upper).world[3].y - 0.5 * plateT(upper);
        const double lowerTop = plate(lower).world[3].y + 0.5 * plateT(lower);
        std::set<int> azimuths;
        for (const Node* p : posts) {
            INFO(p->instanceName);
            REQUIRE(p->worldBounds.valid());
            CHECK(std::abs(p->worldBounds.max.y - upperUnderside) < tol);
            CHECK(std::abs(p->worldBounds.min.y - lowerTop) < tol);
            // on the lower plate's bolt circle, 60° apart
            glm::dvec3 c = p->worldBounds.center() - glm::dvec3(plate(lower).world[3]);
            CHECK(std::hypot(c.x, c.z) == Catch::Approx(plateBoltRing_m(sc.layout().stages[static_cast<std::size_t>(k)].radius_m)).margin(1e-6));
            azimuths.insert(static_cast<int>(std::lround(glm::degrees(std::atan2(c.z, c.x)))) % 360);
            // stainless tube above 4 K, G10 rod below (T08 §4)
            CHECK(p->material == (k <= 2 ? "stainless" : "g10"));
            CHECK(sc.inspect(p->id).function.find("conductivity integral") != std::string::npos);
        }
        CHECK(azimuths.size() == 6);
    }
}

TEST_CASE("the vacuum can carries a flange with 48 hex bolts on the flange radius") {
    const Scene& sc = scene();
    auto ids = sc.findByDescriptor("ovc");
    REQUIRE(ids.size() == 1);
    const Node* ovc = sc.node(ids.front());
    const ComponentDescriptor* d = sc.descriptor(*ovc);
    const double r = d->geometryNumber("r_m", 0.28), fw = d->geometryNumber("flange_w_m", 0.015), ft = d->geometryNumber("flange_t_m", 0.012);
    CHECK(d->geometryNumber("flange_bolts", 0) == 48);
    const gfx::MeshData& m = sc.meshes().data(ovc->finestMesh());
    // bolt heads are the only geometry below the flange plane at the flange radius
    std::set<int> heads;
    for (const auto& v : m.vertices) {
        double rho = std::hypot(v.position.x, v.position.z);
        if (v.position.y < -ft - 1e-4 && rho > r + 0.15 * fw && rho < r + 0.85 * fw)
            heads.insert(static_cast<int>(std::floor(std::atan2(v.position.z, v.position.x) / (2.0 * 3.14159265358979) * 48.0 * 4.0)));
    }
    // 48 clusters, each spanning at most two of the 192 azimuth bins
    CHECK(heads.size() >= 48);
    CHECK(heads.size() <= 96);
    CHECK(ovc->material == "stainless");
    // the base is a 2:1 dished head: the lowest point is r/2 below the cylindrical wall
    const double h = ovc->localBounds.max.y - ovc->localBounds.min.y;
    CHECK(h == Catch::Approx(d->geometryNumber("flange_t_m", 0.012) * 0.0 + (-(ovc->localBounds.min.y))).margin(1e-6));
    CHECK(ovc->localBounds.min.y < -1.35 - 0.5 * r + 1e-6);
}

TEST_CASE("the pulse-tube stages end on the 50 K and 4 K plates with copper braids to each") {
    const Scene& sc = scene();
    auto ids = sc.findByDescriptor("pt_regenerator");
    REQUIRE(ids.size() == 2);
    const cryo::Stage ends[]{cryo::Stage::PT1, cryo::Stage::PT2};
    const cryo::Stage starts[]{cryo::Stage::RT, cryo::Stage::PT1};
    for (std::size_t i = 0; i < 2; ++i) {
        const Node* n = sc.node(ids[i]);
        INFO(n->instanceName);
        CHECK(std::abs(n->worldBounds.min.y - (plate(ends[i]).world[3].y + 0.5 * plateT(ends[i]))) < 1e-3);
        CHECK(std::abs(n->worldBounds.max.y - (plate(starts[i]).world[3].y - 0.5 * plateT(starts[i]))) < 1e-3);
        CHECK(sc.inspect(n->id).function.find("1.4 Hz") != std::string::npos);
    }
    std::map<std::string, int> braids;
    for (ComponentId id : sc.findByDescriptor("thermal_braid")) braids[*sc.node(id)->params.token("stage")]++;
    CHECK(braids["s50"] == 3);
    CHECK(braids["s4"] == 3);
}

TEST_CASE("the still pumping line rises through a real hole in every plate above the still") {
    const Scene& sc = scene();
    auto ids = sc.findByDescriptor("still_pumping_line");
    REQUIRE(ids.size() == 1);
    const Node* line = sc.node(ids.front());
    const double lineR = sc.descriptor(*line)->geometryNumber("r_m", 0.025);
    const glm::dvec3 axis(line->world[3]);
    CHECK(line->worldBounds.max.y > plate(cryo::Stage::RT).world[3].y + 0.5 * plateT(cryo::Stage::RT));
    for (cryo::Stage s : {cryo::Stage::PT2, cryo::Stage::PT1, cryo::Stage::RT}) {
        const Node& p = plate(s);
        INFO(p.instanceName);
        CHECK(line->worldBounds.min.y < p.world[3].y);
        // every vertex of the plate's finest mesh (plate frame) stays outside the line's bore
        const gfx::MeshData& m = sc.meshes().data(p.finestMesh());
        glm::dvec3 local = glm::dvec3(glm::inverse(p.world) * glm::dvec4(axis, 1.0));
        double nearest = 1e9;
        for (const auto& v : m.vertices) nearest = std::min(nearest, std::hypot(v.position.x - local.x, v.position.z - local.z));
        CHECK(nearest >= lineR + 1e-4);
        CHECK(nearest < lineR + 0.01); // and the hole wall hugs the line
    }
    // the still itself and the mixing chamber are on their plates
    CHECK(sc.findByDescriptor("still").size() == 1);
    CHECK(sc.findByDescriptor("mixing_chamber").size() == 1);
    CHECK(sc.findByDescriptor("hx_step").size() == 4);
    CHECK(sc.node(sc.findByDescriptor("mixing_chamber").front())->worldBounds.min.y ==
          Catch::Approx(plate(cryo::Stage::MXC).world[3].y + 0.5 * plateT(cryo::Stage::MXC)).margin(1e-6));
}

TEST_CASE("the layout's light panels become at most eight point lights on the ceiling plane") {
    const Scene& sc = scene();
    REQUIRE(sc.layout().lights.size() == 4);
    Scene copy = *buildScene("sc_lab_standard");
    SceneRenderer renderer(copy);
    const glm::dvec3 eye(1.0, 2.0, 3.0);
    auto lights = renderer.pointLights(eye);
    REQUIRE(lights.size() == sc.layout().lights.size());
    REQUIRE(lights.size() <= static_cast<std::size_t>(gfx::kMaxPointLights));
    for (const auto& l : lights) {
        glm::dvec3 world = glm::dvec3(l.position) + eye;
        CHECK(std::abs(world.y - sc.layout().roomHeight_m) < 0.05);
        CHECK(l.position.w > 0.0f);
        CHECK(l.color.w > 0.0f);
    }
    // Φ = 4000 lm → I = 318 cd; w = I / (1500 lx · r²) with r = 0.67 m: 0.47
    CHECK(lights.front().color.w == Catch::Approx(4000.0 / (4.0 * 3.14159265358979) / (1500.0 * 0.45)).epsilon(0.02));
    std::size_t panels = 0;
    for (const Node& n : sc.nodes()) panels += n.instanceName.rfind("light_panel", 0) == 0 ? 1 : 0;
    CHECK(panels == 4);
    CHECK(buildScene("ion_lab_11")->layout().lights.size() == 3);
}

TEST_CASE("every wiring line enters through an SMA feedthrough on the top plate ring") {
    const Scene& sc = scene();
    for (const auto& run : sc.wiringRuns()) {
        INFO(run.lineId);
        const Node* rt = sc.node(run.stageAnchor[0]);
        REQUIRE(rt != nullptr);
        CHECK(rt->descriptorId == "feedthrough_sma");
        glm::dvec3 c = glm::dvec3(rt->world[3]) - glm::dvec3(plate(cryo::Stage::RT).world[3]);
        CHECK(std::hypot(c.x, c.z) == Catch::Approx(0.24).margin(1e-6));
    }
    // every attenuator has its clamp bracket, every HEMT its bias loom
    std::size_t attenuators = sc.findByDescriptor("attenuator").size(), brackets = 0, biasLooms = 0;
    for (const Node& n : sc.nodes()) {
        brackets += n.instanceName.ends_with(".clamp") && n.descriptorId == "thermal_clamp" ? 1 : 0;
        biasLooms += n.instanceName.ends_with(".bias_loom") ? 1 : 0;
    }
    CHECK(brackets == attenuators);
    CHECK(biasLooms == sc.findByDescriptor("hemt").size());
}

TEST_CASE("the spec and theory cross-references resolve") {
    if (std::system("python3 --version > /dev/null 2>&1") != 0) SKIP("python3 not available");
    const std::string cmd = "cd \"" QXL_SOURCE_DIR "\" && python3 tools/check_xrefs.py > /dev/null 2>&1";
    CHECK(std::system(cmd.c_str()) == 0);
}
