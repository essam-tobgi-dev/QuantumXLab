// Spec 17 §1/§3/§11/§12 — the standard laboratory builds completely, consistently and within
// budget, and every node resolves to a component descriptor.
#include "Core/Json.hpp"
#include "Core/Paths.hpp"
#include "Core/Timer.hpp"
#include "Lab/Build.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <map>
#include <set>

using namespace qlab;
using namespace qlab::lab;

namespace {
const Scene& standardScene() {
    static Scene scene = [] {
        auto s = buildScene("sc_lab_standard");
        if (!s) FAIL(s.error().format());
        return std::move(*s);
    }();
    return scene;
}
} // namespace

TEST_CASE("the standard laboratory builds with unique non-zero ids in depth-first order") {
    const Scene& scene = standardScene();
    REQUIRE(scene.size() > 500);
    std::set<std::uint32_t> ids;
    for (std::size_t i = 0; i < scene.nodes().size(); ++i) {
        const Node& n = scene.nodes()[i];
        INFO(n.instanceName);
        REQUIRE(n.id.value != 0);
        REQUIRE(n.id.value == i + 1); // ids index the depth-first array (spec 17 §1, §9)
        REQUIRE(ids.insert(n.id.value).second);
        if (n.parent.value != 0) REQUIRE(n.parent.value - 1 < i); // parents before children
        REQUIRE(n.subtreeEnd >= i + 1);
        for (ComponentId c : n.children) REQUIRE(scene.node(c)->parent == n.id);
    }
    // instance names are unique, so search-to-select and the component tree are unambiguous
    std::set<std::string> names;
    for (const Node& n : scene.nodes()) {
        INFO(n.instanceName);
        REQUIRE(names.insert(n.instanceName).second);
    }
    CHECK(scene.node(scene.root())->instanceName == "laboratory");
    CHECK(scene.findByInstance("stage_mxc") != ComponentId{0});
    CHECK(scene.findByInstance("nothing_here") == ComponentId{0});
}

TEST_CASE("every component node resolves to a descriptor and every group is geometry-free") {
    const Scene& scene = standardScene();
    std::size_t components = 0, scenery = 0;
    for (const Node& n : scene.nodes()) {
        INFO(n.instanceName);
        if (n.kind == NodeKind::Component) {
            ++components;
            REQUIRE(scene.descriptor(n) != nullptr); // spec 17 §1
            REQUIRE(n.hasGeometry());
            REQUIRE(n.pickable);
            Inspectable x = scene.inspect(n.id);
            CHECK(x.hasDescriptor);
            CHECK_FALSE(x.function.empty());
            CHECK_FALSE(x.specSheet.empty());
        } else if (n.kind == NodeKind::Group) {
            CHECK_FALSE(n.hasGeometry());
            CHECK_FALSE(n.pickable);
        } else {
            ++scenery;
            CHECK_FALSE(n.pickable); // scenery has no Inspectable, so it must not answer a pick
        }
    }
    CHECK(components > 400);
    // Scenery is the room shell and what has no physics role: floor, grid, two walls, skirting,
    // door, window (frame, glass, wall sleeves), four ceiling lights, three safety signs, the
    // task chair and the GHS pipework (rack detail pass) — 21 for sc_lab_standard.
    CHECK(scenery < 26);
    // Every prop in the shipped layout now has a descriptor, so nothing is drawn un-inspectable
    // (spec 17 §12: every visible node answers a pick).
    for (const auto& d : scene.diagnostics()) {
        INFO(d);
        CHECK(d.find("has no component.json") == std::string::npos);
    }
}

TEST_CASE("a prop with no descriptor is reported and drawn as scenery, not dropped") {
    // The guard above only proves the shipped catalog is complete; this proves the reporting path
    // still works, by building a layout that names a prop the catalog does not have.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "qxl_lab_missing_prop";
    std::filesystem::create_directories(dir);
    auto text = core::readTextFile(core::assetDir() / "Lab/Layouts/sc_lab_standard/layout.json");
    REQUIRE(text.has_value());
    core::Json j = core::Json::parse(*text, nullptr, false);
    REQUIRE_FALSE(j.is_discarded());
    j["data"]["props"].push_back({{"id", "not_a_real_prop"}, {"position_m", {0.0, 0.0, 0.0}}});
    REQUIRE(core::writeTextFileAtomic(dir / "layout.json", j.dump(2)).has_value());

    auto s = buildScene(dir);
    REQUIRE(s.has_value());
    bool reported = false;
    for (const auto& d : s->diagnostics())
        reported = reported || (d.find("not_a_real_prop") != std::string::npos &&
                                d.find("has no component.json") != std::string::npos);
    CHECK(reported);
    // It is still drawn, as scenery that never answers a pick.
    const ComponentId id = s->findByInstance("not_a_real_prop");
    REQUIRE(id.value != 0u);
    const Node* prop = s->node(id);
    REQUIRE(prop != nullptr);
    CHECK(prop->kind == NodeKind::Scenery);
    CHECK_FALSE(prop->pickable);
}

TEST_CASE("the fridge stages are ordered RT to MXC, top to bottom, at the layout heights") {
    const Scene& scene = standardScene();
    double previous = 1e9;
    for (int k = 0; k < cryo::kStageCount; ++k) {
        ComponentId id = scene.stageNodes()[static_cast<std::size_t>(k)];
        INFO(cryo::stageName(static_cast<cryo::Stage>(k)));
        REQUIRE(id.value != 0);
        const Node* n = scene.node(id);
        double y = n->world[3].y;
        CHECK(y < previous); // RT highest, MXC lowest
        previous = y;
        bool found = false;
        for (const auto& st : scene.layout().stages)
            if (st.stage == static_cast<cryo::Stage>(k)) {
                found = true;
                CHECK(y == Catch::Approx(st.height_m));
            }
        CHECK(found);
    }
    // the shields nest: each one closes below the next colder assembly
    double innerBottom = 1e9;
    for (const char* id : {"mag_shield_mumetal", "shield_still", "shield_4K", "shield_50K", "ovc"}) {
        auto ids = scene.findByDescriptor(id);
        REQUIRE(ids.size() == 1);
        const Node* n = scene.node(ids.front());
        INFO(id);
        CHECK(n->worldBounds.valid());
        CHECK(n->worldBounds.min.y < innerBottom);
        innerBottom = n->worldBounds.min.y;
    }
    // the chip package hangs under the mixing chamber on the cold finger
    auto puck = scene.findByDescriptor("sample_puck");
    REQUIRE(puck.size() == 1);
    CHECK(scene.node(puck.front())->world[3].y < scene.node(scene.stageNodes()[5])->world[3].y);
}

TEST_CASE("every wiring line has nodes at each stage it passes") {
    const Scene& scene = standardScene();
    REQUIRE(scene.wiringRuns().size() == 40); // sc_heavyhex_27: 27 drive, 4+4 readout, 4 pump, 1 loom
    for (const auto& run : scene.wiringRuns()) {
        INFO(run.lineId);
        for (int k = 0; k < cryo::kStageCount; ++k) {
            INFO(cryo::stageName(static_cast<cryo::Stage>(k)));
            REQUIRE(run.stageAnchor[static_cast<std::size_t>(k)].value != 0);
            const Node* n = scene.node(run.stageAnchor[static_cast<std::size_t>(k)]);
            REQUIRE(n != nullptr);
            CHECK(n->group == Group::Wiring);
            CHECK(n->params.index("line").value() == static_cast<long long>(run.lineIndex));
        }
        CHECK(run.segments.size() >= 5); // one coax run per stage crossing
    }
    // attenuation budget of a drive line: 20 dB at 4 K + 20 dB at MXC (spec 11 §4.1)
    const WiringRun* drive = nullptr;
    for (const auto& run : scene.wiringRuns())
        if (run.lineId == "drive_q0") drive = &run;
    REQUIRE(drive != nullptr);
    double total = 0.0;
    for (const auto& [node, dB] : drive->attenuators) total += dB;
    CHECK(total == Catch::Approx(40.0));
    // and the readout input line carries 70 dB
    for (const auto& run : scene.wiringRuns())
        if (run.lineId == "ro_in_f0") {
            double sum = 0.0;
            for (const auto& [node, dB] : run.attenuators) sum += dB;
            CHECK(sum == Catch::Approx(70.0));
        }
}

TEST_CASE("the chip carries one node per qubit and stays inside the triangle budget") {
    const Scene& scene = standardScene();
    REQUIRE(scene.chipLayout().has_value());
    CHECK(scene.chipLayout()->qubits.size() == 27);
    REQUIRE(scene.qubitNodes().size() == 27);
    std::size_t qubits = 0;
    for (ComponentId id : scene.qubitNodes()) {
        REQUIRE(id.value != 0);
        const Node* n = scene.node(id);
        CHECK(n->descriptorId == "transmon_pad");
        CHECK(n->group == Group::ChipMicro);
        CHECK(n->params.index("q").has_value());
        ++qubits;
    }
    CHECK(qubits == 27);
    CHECK(scene.findByDescriptor("junction").size() == 27);
    CHECK(scene.findByDescriptor("readout_resonator").size() == 27);
    CHECK(scene.findByDescriptor("feedline").size() == 4);
    CHECK(scene.findByDescriptor("drive_line").size() == 27);
    // the ChipMicro island is a 1e-6 scale node (spec 17 §1)
    const Node* substrate = scene.node(scene.chipRoot());
    REQUIRE(substrate != nullptr);
    CHECK(substrate->local.scale.x == Catch::Approx(1e-6));
    CHECK(substrate->group == Group::ChipMicro);
    // authored in micrometres, rendered at the physical size
    glm::dvec3 size = aabbSize(substrate->worldBounds);
    CHECK(size.x == Catch::Approx(scene.chipLayout()->size_um.x * 1e-6).epsilon(1e-6));
    CHECK(size.y == Catch::Approx(350e-6).epsilon(1e-6));
    // spec 24 §6 / 17 §12: 3 M triangles per frame. The whole scene at its finest level is well
    // under that; the fridge detail pass (chamfered plates with bolt circles, flanged cans with
    // bolts, posts, braids, loom ribbons, T-slot frame) raised it from 392 k to ≈ 470 k, and the
    // rack detail pass (14 real front panels with ≈ 230 connectors, rails with the EIA hole
    // pattern, looms, the GHS mimic with 20 handwheel valves and 6 dials, the breadboard's
    // 2 000 holes, dewars, compressor, furniture) to ≈ 570 k. 800 k is the ceiling this test
    // holds it to: the next pass must instance or LOD its parts rather than spend the margin.
    CHECK(scene.stats().trianglesFinest < 800'000);
    CHECK(scene.stats().trianglesFinest < 3'000'000);
    CHECK(scene.stats().cacheHits > 100); // repeated parts share one mesh
    WARN(std::format("nodes {} ({} components), meshes {} ({} cache hits), triangles {} (unique {}), build {:.0f} ms",
                     scene.stats().nodes, scene.stats().components, scene.stats().meshes, scene.stats().cacheHits,
                     scene.stats().trianglesFinest, scene.stats().uniqueTriangles, scene.stats().buildMs));
}

TEST_CASE("the shipped layouts instantiate every descriptor in the catalog") {
    // spec 17 §12: every id of §3 exists and is used. Which ids a scene needs depends on the
    // device and the package: the fixed-frequency heavy-hex lab, the tunable grid (flux lines,
    // SQUIDs, tunable couplers, bias tees, flip-chip bumps), a JPA wiring variant (JPA +
    // circulator) and the ion lab together cover the catalog.
    std::set<std::string> used;
    auto collect = [&used](const Scene& scene) {
        for (const Node& n : scene.nodes())
            if (n.kind == NodeKind::Component) used.insert(n.descriptorId);
    };
    collect(standardScene());
    {
        BuildOptions options;
        options.deviceOverride = "sc_tunable_grid_54";
        options.flipChipBumps = true;
        options.chip.routeGrid_um = 200.0;      // a coarser router and fewer bridges keep this test
        options.chip.airbridgePitch_um = 800.0; // quick; the shipped defaults follow spec 17 §6.3
        auto scene = buildScene("sc_lab_standard", options);
        REQUIRE(scene.has_value());
        collect(*scene);
    }
    {   // a readout line with a JPA instead of a TWPA: no shipped device uses one (spec 11 §4.4)
        std::filesystem::path file = std::filesystem::path(QXL_SOURCE_DIR) / "build" / "lab_jpa_wiring.json";
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        core::Json lines = core::Json::array();
        lines.push_back({{"id", "drive_q0"}, {"channel", "d[0]"}, {"chain", "drive_std"}});
        lines.push_back({{"id", "ro_in_f0"}, {"channel", "m[0]"}, {"chain", "readout_in_std"}});
        lines.push_back({{"id", "ro_out_f0"}, {"channel", "a[0]"}, {"chain", "readout_out_std"}, {"preamp", "jpa"}});
        lines.push_back({{"id", "pump_f0"}, {"channel", "pump[0]"}, {"chain", "pump_std"}});
        lines.push_back({{"id", "dc_loom_0"}, {"channel", "dc[0..11]"}, {"chain", "dc_loom_std"}});
        REQUIRE(core::JsonEnvelope::save(file, "wiring",
                                         {{"device", "sc_heavyhex_27"}, {"layout", "sc_lab_standard"}, {"lines", lines}})
                    .has_value());
        BuildOptions options;
        options.wiringOverride = file;
        options.buildChip = false;
        auto scene = buildScene("sc_lab_standard", options);
        REQUIRE(scene.has_value());
        collect(*scene);
    }
    {
        auto scene = buildScene("ion_lab_11");
        REQUIRE(scene.has_value());
        collect(*scene);
    }
    for (const auto& d : standardScene().catalog().all()) {
        INFO(d.id << " (" << d.category << ")");
        CHECK(used.count(d.id) == 1);
    }
    CHECK(used.size() == standardScene().catalog().size());
}

TEST_CASE("bounds and breadcrumbs follow the hierarchy") {
    const Scene& scene = standardScene();
    // the subtree bounds of the root cover the room
    const Node* root = scene.node(scene.root());
    REQUIRE(root->subtreeBounds.valid());
    CHECK(aabbSize(root->subtreeBounds).x >= scene.layout().roomWidth_m * 0.5);
    for (const Node& n : scene.nodes()) {
        if (!n.worldBounds.valid()) continue;
        INFO(n.instanceName);
        CHECK(n.subtreeBounds.min.x <= n.worldBounds.min.x + 1e-9);
        CHECK(n.subtreeBounds.max.y + 1e-9 >= n.worldBounds.max.y);
        if (n.parent.value != 0) {
            const Node* p = scene.node(n.parent);
            CHECK(p->subtreeBounds.min.y <= n.subtreeBounds.min.y + 1e-9);
        }
    }
    // spec 17 §7.2: "Fridge › Mixing chamber › Line 3 › Attenuator 30 dB"
    ComponentId attn = scene.findByInstance("drive_q3.att_MXC");
    REQUIRE(attn != ComponentId{0});
    std::string crumbs = scene.breadcrumbText(attn);
    INFO(crumbs);
    CHECK(crumbs.find("Refrigerator") != std::string::npos);
    CHECK(crumbs.find("Mixing chamber") != std::string::npos);
    CHECK(crumbs.find("Line drive_q3") != std::string::npos);
    CHECK(crumbs.find("Attenuator 20 dB") != std::string::npos);
    CHECK(scene.breadcrumb(attn).front() == scene.root());
    CHECK(scene.breadcrumb(attn).back() == attn);
}
