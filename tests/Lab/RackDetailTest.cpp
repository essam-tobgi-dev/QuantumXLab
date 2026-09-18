// Spec 17 §3.3 / §3.4 / §6 (rack detail pass) — every rack instrument is a different, real
// front panel: pairwise distinct meshes within a per-unit triangle budget, U heights as the
// spec table, a rack stack without overlaps, connector counts equal to the descriptor's port list
// (counted in the mesh by the connectors' body colours), and the gas-handling panel laid out on
// the shared mimic grid.
#include "Lab/Lab.hpp"
#include "Lab/RackPanel.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>
#include <format>
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

std::vector<const ComponentDescriptor*> rackUnits(const ComponentCatalog& cat) {
    std::vector<const ComponentDescriptor*> out;
    for (const auto& d : cat.all())
        if (d.generator == "RackUnit") out.push_back(&d);
    return out;
}

std::uint64_t meshHash(const gfx::MeshData& m) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](float v) {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        h = (h ^ bits) * 1099511628211ull;
    };
    for (const auto& v : m.vertices) {
        mix(v.position.x), mix(v.position.y), mix(v.position.z);
        mix(v.color.r), mix(v.color.g), mix(v.color.b);
    }
    h ^= m.indices.size();
    return h;
}

std::size_t countColour(const gfx::MeshData& m, const glm::vec4& c) {
    std::size_t n = 0;
    for (const auto& v : m.vertices)
        if (v.color == c) ++n;
    return n;
}
} // namespace

TEST_CASE("every rack instrument has its own front panel: distinct meshes within budget") {
    const auto& cat = standardScene().catalog();
    const auto units = rackUnits(cat);
    REQUIRE(units.size() == 14);
    std::map<std::uint64_t, std::string> seen;
    std::size_t total = 0;
    for (const auto* d : units) {
        GenParams p = GenParams::merged(d->geometry, core::Json::object());
        auto full = generateMesh("RackUnit", p, GenContext{Detail::Full, 1.0});
        REQUIRE(full.has_value());
        INFO(d->id << ": " << full->triangleCount() << " triangles");
        CHECK(full->triangleCount() > 200);     // a real panel, not a box
        CHECK(full->triangleCount() < 16'000);  // per-unit budget: the 48-way patch panel is the largest
        total += full->triangleCount();
        auto [it, inserted] = seen.emplace(meshHash(*full), d->id);
        INFO("duplicate of " << it->second);
        CHECK(inserted);
        // the simple level keeps the finish colour so units differ from across the room
        auto simple = generateMesh("RackUnit", p, GenContext{Detail::Simple, 1.0});
        REQUIRE(simple.has_value());
        CHECK(simple->triangleCount() < 60);
        RackPanelSpec spec = parseRackPanel(p);
        CHECK(countColour(*simple, spec.faceColour) > 0);
        CHECK(countColour(*full, spec.faceColour) > 0);
    }
    CHECK(total < 90'000); // all 14 classes at full detail
}

TEST_CASE("rack units have the U heights of spec 17 §3.3 and the stack does not overlap") {
    const Scene& scene = standardScene();
    const std::map<std::string, int> kHeightU{{"mw_generator", 2}, {"control_chassis", 4}, {"digitizer", 3}, {"iq_mixer_board", 1},
                                              {"dc_source", 3},    {"power_dist", 1},      {"vna", 4},       {"spectrum_analyzer", 4},
                                              {"oscilloscope", 4}, {"rt_amplifier", 1},    {"ref_10mhz", 1}, {"clock_dist", 1},
                                              {"trigger_unit", 1}, {"patch_panel", 1}};
    for (const auto& [id, u] : kHeightU) {
        const ComponentDescriptor* d = scene.catalog().find(id);
        REQUIRE(d != nullptr);
        CHECK(d->geometryNumber("u", 0) == u);
        CHECK(d->detailAt(2.4) == Detail::Full); // the Rack bookmark is 2.4 m from the panels
    }
    std::map<std::string, std::vector<const Node*>> perRack;
    for (const Node& n : scene.nodes())
        if (n.assembly == Assembly::RackUnits) perRack[n.instanceName.substr(0, 6)].push_back(&n);
    REQUIRE(perRack.size() == 2);
    for (auto& [rack, nodes] : perRack) {
        std::sort(nodes.begin(), nodes.end(), [](const Node* a, const Node* b) { return a->worldBounds.min.y > b->worldBounds.min.y; });
        const Node* enclosure = scene.node(scene.findByInstance(rack + ".enclosure"));
        REQUIRE(enclosure != nullptr);
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const Node* n = nodes[i];
            INFO(n->instanceName);
            const double u = scene.catalog().find(n->descriptorId)->geometryNumber("u", 0);
            CHECK(aabbSize(n->worldBounds).y == Catch::Approx(u * kRackUnit_m).margin(1.5e-3));
            CHECK(aabbSize(n->worldBounds).x == Catch::Approx(kRackPanelWidth_m).margin(1e-3));
            // inside the cabinet's footprint, faceplates flush at one z (real rails)
            CHECK(n->worldBounds.min.x >= enclosure->worldBounds.min.x - 1e-9);
            CHECK(n->worldBounds.max.x <= enclosure->worldBounds.max.x + 1e-9);
            CHECK(n->worldBounds.min.y >= enclosure->worldBounds.min.y);
            if (i > 0) CHECK(nodes[i - 1]->worldBounds.min.y >= n->worldBounds.max.y - 1e-6); // no overlap, top to bottom
        }
        CHECK(nodes.size() >= 9);
    }
}

TEST_CASE("connector counts in each unit's mesh match the descriptor's front-panel port list") {
    const auto& cat = standardScene().catalog();
    for (const auto* d : rackUnits(cat)) {
        GenParams p = GenParams::merged(d->geometry, core::Json::object());
        RackPanelSpec spec = parseRackPanel(p);
        auto full = generateMesh("RackUnit", p, GenContext{Detail::Full, 1.0});
        REQUIRE(full.has_value());
        INFO(d->id);
        REQUIRE(spec.connectorCount() > 0); // every instrument presents at least one port
        // N and BNC share the nickel body colour; the other types are unique
        const std::size_t nickel = countColour(*full, connectorBodyColour(ConnectorType::N));
        CHECK(nickel == spec.connectorCount(ConnectorType::N) * connectorVertexCount(ConnectorType::N) +
                            spec.connectorCount(ConnectorType::Bnc) * connectorVertexCount(ConnectorType::Bnc));
        for (ConnectorType t : {ConnectorType::Sma, ConnectorType::Iec, ConnectorType::Dsub}) {
            INFO(connectorTypeName(t));
            CHECK(countColour(*full, connectorBodyColour(t)) == static_cast<std::size_t>(spec.connectorCount(t)) * connectorVertexCount(t));
        }
        // the label list is the port list the inspector would show
        std::set<std::string> labels;
        for (const auto& c : spec.connectors) labels.insert(c.label);
        CHECK(labels.size() == spec.connectors.size());
        // the nameplate anchor of the renderer's model label lies on the unit's faceplate
        glm::dvec3 np = rackNameplateLocal(spec.u, kRackUnitDepth_m);
        gfx::Aabb box = emptyAabb();
        for (const auto& v : full->vertices) box.expand(glm::dvec3(v.position));
        CHECK(np.x > box.min.x);
        CHECK(np.x < box.max.x);
        CHECK(np.y < box.max.y);
        CHECK(np.z <= box.max.z);
    }
    // the specific real layouts of spec 17 §3.3 (amended)
    auto spec = [&](const char* id) { return parseRackPanel(GenParams::merged(cat.find(id)->geometry, core::Json::object())); };
    CHECK(spec("vna").connectorCount(ConnectorType::N) == 2);
    CHECK(spec("iq_mixer_board").connectorCount(ConnectorType::Sma) == 5);
    CHECK(spec("digitizer").connectorCount(ConnectorType::Sma) == 9); // 8 channels + clock in
    CHECK(spec("clock_dist").connectorCount(ConnectorType::Sma) == 13); // 12 outputs + reference in
    CHECK(spec("patch_panel").connectorCount(ConnectorType::Sma) == 48);
    CHECK(spec("control_chassis").slots.has_value());
    CHECK(spec("control_chassis").connectorCount() == 8 * 4 + 2);
    CHECK(spec("power_dist").connectorCount(ConnectorType::Iec) == 8);
    CHECK(spec("oscilloscope").connectorCount(ConnectorType::Bnc) == 5);
    CHECK(spec("oscilloscope").knobs.size() == 6);
    CHECK(spec("mw_generator").screen.has_value());
    CHECK(spec("ref_10mhz").finish == "blue_grey");
    CHECK(spec("dc_source").finish == "off_white");
    CHECK_FALSE(spec("patch_panel").nameplate);
    CHECK(spec("control_chassis").handles);
    CHECK_FALSE(spec("iq_mixer_board").handles);
}

TEST_CASE("the gas-handling panel carries its valves and gauges on the mimic grid") {
    const Scene& scene = standardScene();
    const Node* cab = scene.node(scene.findByInstance("ghs_cabinet"));
    REQUIRE(cab != nullptr);
    const ComponentDescriptor* d = scene.catalog().find("ghs_cabinet");
    const double W = d->geometryNumber("w_m", 0.8), H = d->geometryNumber("h_m", 1.9);
    CHECK(d->detailAt(2.4) == Detail::Full); // the GHS bookmark sees the mimic diagram, not a box
    CHECK(scene.catalog().find("valve")->detailAt(2.4) == Detail::Full);
    const glm::dvec3 origin(cab->world[3]);
    auto valves = scene.findByDescriptor("valve");
    REQUIRE(valves.size() == 20);
    for (ComponentId id : valves) {
        const Node* v = scene.node(id);
        auto i = v->params.index("i");
        REQUIRE(i.has_value());
        const glm::dvec3 at(v->world[3]);
        CHECK(at.x == Catch::Approx(origin.x + ghsValveX(static_cast<int>(*i % 5)) * W).margin(1e-9));
        CHECK(at.y == Catch::Approx(origin.y + ghsValveY(static_cast<int>(*i / 5)) * H).margin(1e-9));
        CHECK(at.z > cab->worldBounds.max.z - 0.03); // on the front panel
        CHECK(v->displayName == std::format("Valve V{}", *i + 1));
    }
    auto gauges = scene.findByDescriptor("pressure_gauge");
    REQUIRE(gauges.size() == 6);
    for (ComponentId id : gauges) {
        const Node* g = scene.node(id);
        CHECK(glm::dvec3(g->world[3]).y == Catch::Approx(origin.y + kGhsGaugeY * H).margin(1e-9));
    }
    // the plant stands on the floor around the cabinet (its pipework runs into the cabinet's
    // sides, so the vessels' floor points are tested, not their AABBs); the turbo pump on the roof
    for (const char* id : {"scroll_pump", "he3_compressor", "ln2_trap", "dump_tank"})
        for (ComponentId n : scene.findByDescriptor(id)) {
            const Node* p = scene.node(n);
            INFO(p->instanceName);
            const glm::dvec3 foot(p->world[3]);
            CHECK((foot.x <= cab->worldBounds.min.x - 0.1 || foot.x >= cab->worldBounds.max.x + 0.1));
            CHECK(foot.y == Catch::Approx(0.0).margin(1e-9));
            CHECK(p->worldBounds.min.y >= -1e-6);
        }
    const Node* turbo = scene.node(scene.findByDescriptor("turbo_pump").at(0));
    CHECK(turbo->worldBounds.min.y >= cab->worldBounds.max.y - 1e-6);
    CHECK(scene.findByDescriptor("dump_tank").size() == 2);
    // the new inspectable parts of the pass are in the scene with their function paragraphs
    for (const char* id : {"cable_loom", "microscope", "wire_bonder", "sample_box", "he_dewar", "pt_compressor", "workstation", "bench"}) {
        INFO(id);
        auto nodes = scene.findByDescriptor(id);
        REQUIRE_FALSE(nodes.empty());
        CHECK(scene.inspect(nodes.front()).function.size() > 200);
    }
    CHECK(scene.findByDescriptor("cable_loom").size() == 3); // two rack looms and the drop to the top plate
}
