// Spec 17 §7 — interaction: hover/tooltip, selection and breadcrumbs, focus, exploded view,
// cutaway, X-ray, layer toggles, search-to-select and bookmarks. All headless.
#include "Lab/Lab.hpp"
#include "Data/Fidelity.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>

using namespace qlab;
using namespace qlab::lab;

namespace {
Scene build() {
    auto s = buildScene("sc_lab_standard");
    if (!s) FAIL(s.error().format());
    return std::move(*s);
}

// Stage plate world heights, top to bottom.
std::vector<double> stageHeights(const Scene& scene) {
    std::vector<double> y;
    for (ComponentId id : scene.stageNodes()) y.push_back(scene.node(id)->world[3].y);
    return y;
}
} // namespace

TEST_CASE("the exploded view separates the stages monotonically and 0 restores the rest pose") {
    Scene scene = build();
    Interaction ui(scene);
    std::vector<glm::dmat4> rest;
    for (const Node& n : scene.nodes()) rest.push_back(n.world);
    std::vector<gfx::MeshData> restMeshes;
    for (const Node& n : scene.nodes())
        if (n.spline) restMeshes.push_back(scene.meshes().data(n.finestMesh()));

    std::vector<double> gaps0;
    std::vector<double> y0 = stageHeights(scene);
    for (std::size_t k = 1; k < y0.size(); ++k) gaps0.push_back(y0[k - 1] - y0[k]);

    double previousSpread = 0.0;
    for (double s : {0.25, 0.5, 0.75, 1.0}) {
        ui.setExplode(Assembly::FridgeStages, s);
        std::vector<double> y = stageHeights(scene);
        CHECK(y.front() == Catch::Approx(y0.front())); // the top plate stays fixed (spec 17 §7.4)
        double spread = y.front() - y.back();
        CHECK(spread > previousSpread);
        previousSpread = spread;
        for (std::size_t k = 1; k < y.size(); ++k) {
            double gap = y[k - 1] - y[k];
            INFO("stage " << k << " at s = " << s);
            CHECK(gap > gaps0[k - 1] * 0.99);                      // stages only separate
            CHECK(gap == Catch::Approx(gaps0[k - 1] + 0.25 * s));  // 0.25 m per stage at full extension
        }
    }
    // s = 0 restores every transform exactly and re-evaluates the wiring splines back to rest
    ui.setExplode(Assembly::FridgeStages, 0.0);
    for (std::size_t i = 0; i < scene.nodes().size(); ++i) {
        INFO(scene.nodes()[i].instanceName);
        REQUIRE(scene.nodes()[i].world == rest[i]);
    }
    std::size_t spline = 0;
    for (const Node& n : scene.nodes()) {
        if (!n.spline) continue;
        const gfx::MeshData& now = scene.meshes().data(n.finestMesh());
        const gfx::MeshData& before = restMeshes[spline++];
        REQUIRE(now.vertices.size() == before.vertices.size());
        for (std::size_t v = 0; v < now.vertices.size(); ++v) CHECK(now.vertices[v].position == before.vertices[v].position);
    }
    CHECK(spline > 100); // every coax run is a spline
    // exploding stretches the coax runs through the moved clamps
    ComponentId segment = ComponentId{0};
    for (const Node& n : scene.nodes())
        if (n.spline && n.instanceName.starts_with("drive_q0.coax_MXC")) segment = n.id;
    REQUIRE(segment != ComponentId{0});
    double restLength = aabbSize(scene.node(segment)->localBounds).y;
    ui.setExplode(Assembly::FridgeStages, 1.0);
    CHECK(aabbSize(scene.node(segment)->localBounds).y > restLength + 0.2);
    ui.setExplode(Assembly::FridgeStages, 0.0);
    // the other assemblies move their own parts only
    ui.setExplode(Assembly::RackUnits, 1.0);
    ComponentId unit = scene.findByInstance("rack_A.digitizer");
    REQUIRE(unit != ComponentId{0});
    CHECK(scene.node(unit)->explodeOffset.z > 0.3);
    CHECK(scene.node(scene.stageNodes()[5])->explodeOffset == glm::dvec3(0.0));
}

TEST_CASE("search-to-select finds every instance of a component") {
    Scene scene = build();
    Interaction ui(scene);
    std::set<std::uint32_t> attenuators;
    for (ComponentId id : scene.findByDescriptor("attenuator")) attenuators.insert(id.value);
    REQUIRE(attenuators.size() > 50); // 27 drive × 2 + readout and pump lines
    auto hits = ui.search("attenuator");
    std::set<std::uint32_t> found;
    for (std::size_t i = 0; i < attenuators.size(); ++i) {
        REQUIRE(i < hits.size());
        INFO(hits[i].label);
        CHECK(attenuators.count(hits[i].id.value) == 1); // the attenuators rank first
        found.insert(hits[i].id.value);
    }
    CHECK(found == attenuators);
    // ranking: an exact instance name beats a component-name match
    auto exact = ui.search("stage_mxc", 1);
    REQUIRE_FALSE(exact.empty());
    CHECK(scene.node(exact.front().id)->instanceName == "stage_mxc");
    // selection and breadcrumb
    ComponentId picked = ui.searchSelect("drive_q3.att_MXC");
    CHECK(picked != ComponentId{0});
    CHECK(ui.selected() == picked);
    CHECK(ui.breadcrumbText(picked).find("Attenuator") != std::string::npos);
    CHECK(ui.search("no_such_component").empty());
    CHECK(ui.search("").empty());
    // a group node is never selected by a pick, and the search prefers components
    ui.select(scene.root());
    CHECK(ui.selected() == ComponentId{0});
}

TEST_CASE("focus keeps the node's bounding box inside the frustum") {
    Scene scene = build();
    Interaction ui(scene);
    gfx::Camera camera;
    camera.setAspect(16.0 / 9.0);
    camera.lookAt({5.0, 3.0, 6.0}, {0.0, 1.2, 0.0});
    for (const char* instance : {"stage_mxc", "drive_q3.att_MXC", "q[13]", "rack_B.vna"}) {
        ComponentId id = scene.findByInstance(instance);
        INFO(instance);
        REQUIRE(id != ComponentId{0});
        REQUIRE(ui.focus(id, camera, 0.4));
        for (int i = 0; i < 50; ++i) ui.update(0.01, camera); // the 400 ms ease completes
        CHECK_FALSE(camera.transitioning());
        const gfx::Aabb& box = scene.node(id)->subtreeBounds;
        CHECK(camera.frustum().intersects(box));
        for (int c = 0; c < 8; ++c) {
            glm::dvec3 corner{(c & 1) ? box.max.x : box.min.x, (c & 2) ? box.max.y : box.min.y,
                              (c & 4) ? box.max.z : box.min.z};
            glm::dvec2 px;
            REQUIRE(camera.project(corner, 1600, 900, px));
            CHECK(px.x >= 0.0);
            CHECK(px.x <= 1600.0);
            CHECK(px.y >= 0.0);
            CHECK(px.y <= 900.0);
        }
        // the near plane follows the viewing distance so a 0.3 mm qubit pad is not clipped away
        CHECK(camera.nearPlane() < camera.distance());
        CHECK(camera.farPlane() > camera.distance());
    }
}

TEST_CASE("bookmarks come from the layout plus one per qubit") {
    Scene scene = build();
    Interaction ui(scene);
    std::map<std::string, int> names;
    for (const auto& b : ui.bookmarks()) ++names[b.name];
    for (const char* n : {"Overview", "Fridge", "MXC", "Chip", "Rack", "GHS"}) {
        INFO(n);
        CHECK(names.count(n) == 1);
    }
    for (std::size_t q = 0; q < scene.qubitNodes().size(); ++q) CHECK(names.count(std::format("Qubit q[{}]", q)) == 1);
    CHECK(ui.bookmarks().size() == 6 + scene.qubitNodes().size());

    gfx::Camera camera;
    camera.setAspect(1.6);
    // the chip bookmark is anchored to the scale island and shows the chip layers only
    REQUIRE(ui.applyBookmark("Chip", camera, 0.0));
    const Node* island = scene.node(scene.chipRoot());
    CHECK(glm::length(camera.target() - island->subtreeBounds.center()) < 1e-9);
    CHECK_FALSE(ui.layerVisible(Group::FridgeInterior));
    CHECK(ui.layerVisible(Group::ChipMicro));
    // a qubit bookmark frames its pad
    REQUIRE(ui.applyBookmark("Qubit q[5]", camera, 0.0));
    const gfx::Aabb& pad = scene.node(scene.qubitNodes()[5])->subtreeBounds;
    CHECK(camera.frustum().intersects(pad));
    CHECK_FALSE(ui.applyBookmark("Nowhere", camera, 0.0));
    // user bookmarks (Ctrl+digit) are stored alongside
    ui.storeBookmark("Mine", camera);
    REQUIRE(ui.bookmark("Mine") != nullptr);
}

TEST_CASE("layer toggles, X-ray, cutaway and hover state persist through the view state") {
    Scene scene = build();
    BindingRegistry registry;
    registry.registerProvider(BindingRoot::Wiring, [](std::string_view path) -> std::optional<BindingValue> {
        if (path.ends_with(".P_diss")) return BindingValue::number(1.5e-9, "W", data::FidelityClass::Model);
        return std::nullopt;
    });
    Interaction ui(scene, &registry);
    ComponentId attn = scene.findByInstance("drive_q3.att_MXC");
    REQUIRE(attn != ComponentId{0});

    // tooltip after 250 ms with the top live row (spec 17 §7.1)
    ui.hover(attn, 10.0);
    CHECK_FALSE(ui.tooltip(10.1).has_value());
    auto tip = ui.tooltip(10.3);
    REQUIRE(tip.has_value());
    CHECK(tip->text.find("Attenuator") != std::string::npos);
    CHECK(tip->liveRow.find("attenuation") != std::string::npos);
    CHECK(tip->liveRow.find("20 dB") != std::string::npos); // static.A_dB from the instance
    // The hover card (spec 17 §7.1) explains the part: descriptor name, category, the `function`
    // paragraph, a theory anchor to jump to, and the live rows in sheet order.
    CHECK(tip->name.find("ttenuator") != std::string::npos);
    CHECK(tip->displayName == scene.node(attn)->displayName);
    CHECK_FALSE(tip->category.empty());
    CHECK(tip->function.size() > 80);
    CHECK(tip->theory.rfind("T0", 0) == 0);
    REQUIRE_FALSE(tip->liveRows.empty());
    CHECK(tip->liveRows.size() <= 3);
    CHECK(tip->liveRows.front().text == tip->liveRow);
    CHECK(tip->liveRows.front().cls == tip->cls);
    ui.hover(ComponentId{0}, 10.4);
    CHECK_FALSE(ui.tooltip(11.0).has_value());

    CHECK(ui.layerVisible(Group::Wiring));
    ui.setLayerVisible(Group::Wiring, false);
    CHECK_FALSE(ui.nodeVisible(*scene.node(attn)));
    ui.setLayerVisible(Group::Wiring, true);
    CHECK(ui.nodeVisible(*scene.node(attn)));
    // hiding the cans opens the fridge without touching the interior layer
    ComponentId ovc = scene.findByDescriptor("ovc").front();
    CHECK(ui.nodeVisible(*scene.node(ovc)));
    ui.setCansVisible(false);
    CHECK_FALSE(ui.nodeVisible(*scene.node(ovc)));
    CHECK(ui.nodeVisible(*scene.node(scene.stageNodes()[5])));
    ui.setCansVisible(true);

    ui.setXray(true);
    ui.setCutaway(true, 90.0);
    glm::dvec4 plane = ui.cutawayPlane();
    CHECK(plane.y == 0.0);                                  // a vertical plane through the fridge axis
    CHECK(glm::dot(glm::dvec3(plane), glm::dvec3(0, 0, 1)) == Catch::Approx(1.0));
    CHECK(glm::dot(glm::dvec3(plane), scene.layout().fridgePosition_m) + plane.w == Catch::Approx(0.0));
    ui.setExplode(Assembly::ChipPackage, 0.5);

    core::Json saved = ui.saveViewState();
    Interaction other(scene, &registry);
    other.loadViewState(saved);
    CHECK(other.xray());
    CHECK(other.cutaway());
    CHECK(other.cutawayAngleDeg() == Catch::Approx(90.0));
    CHECK(other.explode(Assembly::ChipPackage) == Catch::Approx(0.5));
    CHECK(other.explode(Assembly::FridgeStages) == Catch::Approx(0.0));
    ui.setExplode(Assembly::ChipPackage, 0.0);
}

TEST_CASE("leaving a chip bookmark restores the layers it hid (the way back to the room)") {
    Scene scene = build();
    Interaction ui(scene);
    gfx::Camera camera;
    ui.setLayerVisible(Group::Rack, false);          // a choice the user made before
    REQUIRE(ui.applyBookmark("Chip", camera, 0.0));
    CHECK(ui.layerVisible(Group::Chip));
    CHECK_FALSE(ui.layerVisible(Group::Room));
    CHECK_FALSE(ui.layerVisible(Group::FridgeInterior));
    REQUIRE(ui.layersBeforeIsland().has_value());
    // A second chip-scale bookmark keeps the saved set (it does not save the hidden state).
    const auto marks = ui.bookmarks();
    for (const LayoutBookmark& b : marks)
        if (b.name.rfind("Qubit", 0) == 0) { REQUIRE(ui.applyBookmark(b.name, camera, 0.0)); break; }
    CHECK_FALSE(ui.layerVisible(Group::Room));
    REQUIRE(ui.applyBookmark("Overview", camera, 0.0));
    CHECK(ui.layerVisible(Group::Room));
    CHECK(ui.layerVisible(Group::FridgeInterior));
    CHECK(ui.layerVisible(Group::Wiring));
    CHECK_FALSE(ui.layerVisible(Group::Rack));       // the user's own choice survives the round trip
    CHECK_FALSE(ui.layersBeforeIsland().has_value());
}
