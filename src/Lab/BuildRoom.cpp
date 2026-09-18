// Spec 17 §2 — the room shell (floor, walls, skirting, door, ceiling light panels, cable tray with
// its bundle) and the layout's props.
#include "Lab/Builder.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <format>

namespace qlab::lab {

Status SceneBuilder::buildRoom(ComponentId root) {
    const double W = layout_.roomWidth_m, D = layout_.roomDepth_m, H = layout_.roomHeight_m;
    ComponentId room = addGroup(root, "room", "Room", Group::Room);
    addScenery(room, "floor", "Floor", Group::Room, Transform::at(0.0, -0.05, 0.0),
               mesh::box({0, 0, 0}, {static_cast<float>(W), 0.1f, static_cast<float>(D)}), "floor");
    // false floor on a 0.6 m tile grid (spec 17 §2); the square grid is squeezed to the room depth
    int tiles = std::max(2, static_cast<int>(std::lround(W / 0.6)));
    Transform gridPose = Transform::at(0.0, 0.003, 0.0);
    gridPose.scale = {1.0, 1.0, D / W};
    addScenery(room, "floor_grid", "False floor", Group::Room, gridPose,
               gfx::shapes::grid(static_cast<float>(0.5 * W), tiles, 0.012f), "plastic_grey");
    // Only the −X and −Z walls are built: the viewport looks into the room from +X/+Z (spec 17 §2
    // has the door on the +Z wall), so the near walls would occlude every bookmark.
    const auto w = static_cast<float>(W), d = static_cast<float>(D), h = static_cast<float>(H);
    addScenery(room, "wall_x", "Wall (−X)", Group::Room, Transform::at(-0.5 * W - 0.05, 0.5 * H, 0.0),
               mesh::box({0, 0, 0}, {0.1f, h, d}), "wall");
    addScenery(room, "wall_z", "Wall (−Z)", Group::Room, Transform::at(0.0, 0.5 * H, -0.5 * D - 0.05),
               mesh::box({0, 0, 0}, {w, h, 0.1f}), "wall");
    {   // skirting board along both walls, and the lab door in the −X wall (spec 17 §2 amended)
        gfx::MeshData skirting;
        mesh::append(skirting, mesh::box({-0.5f * w + 0.008f, 0.05f, 0.0f}, {0.016f, 0.1f, d}));
        mesh::append(skirting, mesh::box({0.0f, 0.05f, -0.5f * d + 0.008f}, {w, 0.1f, 0.016f}));
        addScenery(room, "skirting", "Skirting board", Group::Room, {}, std::move(skirting), "plastic_grey");
        gfx::MeshData door;
        const float dz = 0.3f * d; // door leaf 0.9 × 2.1 m, recessed 30 mm, with frame and handle
        mesh::appendColored(door, mesh::box({-0.5f * w + 0.03f, 1.05f, dz}, {0.06f, 2.1f, 0.9f}), {0.55f, 0.56f, 0.58f, 1.0f});
        mesh::appendColored(door, mesh::box({-0.5f * w + 0.03f, 2.14f, dz}, {0.06f, 0.08f, 1.06f}), {0.35f, 0.36f, 0.38f, 1.0f});
        for (float side : {-1.0f, 1.0f})
            mesh::appendColored(door, mesh::box({-0.5f * w + 0.03f, 1.05f, dz + side * 0.49f}, {0.06f, 2.1f, 0.08f}), {0.35f, 0.36f, 0.38f, 1.0f});
        mesh::appendColored(door, mesh::box({-0.5f * w + 0.08f, 1.0f, dz - 0.35f}, {0.04f, 0.03f, 0.14f}), {0.75f, 0.75f, 0.72f, 1.0f});
        mesh::appendColored(door, mesh::box({-0.5f * w + 0.04f, 1.55f, dz}, {0.05f, 0.5f, 0.3f}), {0.6f, 0.8f, 0.95f, 1.0f}); // window
        addScenery(room, "door", "Door", Group::Room, {}, std::move(door), "plastic_grey");
    }
    QXL_TRY(buildLights(room));

    if (scene_.catalog().contains("cable_tray") && !layout_.racks.empty()) {
        const double ceiling = H - 0.3;
        core::Json pts = core::Json::array();
        glm::dvec3 rack = layout_.racks.front().position_m;
        glm::dvec3 fridge = layout_.fridgePosition_m;
        for (glm::dvec3 p : {glm::dvec3(fridge.x, ceiling, fridge.z), glm::dvec3(0.6 * rack.x, ceiling, fridge.z),
                             glm::dvec3(rack.x, ceiling, fridge.z), glm::dvec3(rack.x, ceiling, rack.z + 0.4)})
            pts.push_back({p.x, p.y, p.z});
        // the bundle drops from the tray onto the top plate's feedthrough ring
        if (layout_.hasFridge()) {
            gfx::MeshData drop;
            const float top = static_cast<float>(ceiling - 0.03), plate = static_cast<float>(layout_.stages.front().height_m + 0.02);
            for (int k = 0; k < 12; ++k) {
                float a = 0.52f * static_cast<float>(k), r = 0.16f + 0.02f * static_cast<float>(k % 3);
                glm::vec3 foot{static_cast<float>(fridge.x) + r * std::cos(a), plate, static_cast<float>(fridge.z) + r * std::sin(a)};
                glm::vec3 head{static_cast<float>(fridge.x) + 0.06f * std::cos(a), top, static_cast<float>(fridge.z) + 0.06f * std::sin(a)};
                glm::vec3 mid = 0.5f * (foot + head) + glm::vec3(0.4f * (foot.x - head.x), 0.0f, 0.4f * (foot.z - head.z));
                mesh::appendColored(drop, gfx::shapes::tube({head, mid, foot}, 0.005f, 8, true), {0.12f, 0.14f, 0.30f, 1.0f});
            }
            addScenery(room, "cable_drop", "Cable drop to the top plate", Group::Room, {}, std::move(drop), "plastic_grey");
        }
        NodeSpec tray;
        tray.descriptor = "cable_tray";
        tray.instance = "cable_tray";
        tray.group = Group::Room;
        tray.material = "aluminium";
        tray.overrides = {{"points_m", pts}, {"cables", 12}};
        tray.cacheMesh = false;
        QXL_TRY(addComponent(room, std::move(tray)));
    }
    return {};
}

Status SceneBuilder::buildProps(ComponentId root) {
    ComponentId props = addGroup(root, "props", "Props", Group::Room);
    for (const auto& prop : layout_.props) {
        for (int k = 0; k < std::max(1, prop.count); ++k) {
            NodeSpec spec;
            spec.descriptor = prop.id;
            spec.instance = prop.count > 1 ? std::format("{}[{}]", prop.id, k) : prop.id;
            spec.group = Group::Room;
            gfx::MeshData mesh;
            glm::dvec3 at = prop.position_m;
            if (prop.id == "workstation") { // desk with two monitors (spec 17 §2)
                spec.material = "desk";
                mesh::append(mesh, mesh::box({0, -0.025f, 0}, {1.6f, 0.05f, 0.8f}));
                for (float side : {-1.0f, 1.0f}) {
                    mesh::append(mesh, mesh::box({side * 0.45f, -0.375f, 0.35f}, {0.06f, 0.7f, 0.06f}));
                    mesh::appendColored(mesh, mesh::box({side * 0.35f, 0.2f, -0.25f}, {0.6f, 0.36f, 0.02f}),
                                        {0.05f, 0.09f, 0.14f, 1.0f});
                    mesh::append(mesh, mesh::box({side * 0.35f, 0.02f, -0.25f}, {0.12f, 0.04f, 0.16f}));
                }
                at.y += 0.75;
            } else if (prop.id == "bench") { // chip-mounting bench with a microscope
                spec.material = "plastic_grey";
                mesh::append(mesh, mesh::box({0, -0.025f, 0}, {1.2f, 0.05f, 0.7f}));
                mesh::append(mesh, mesh::box({-0.3f, 0.18f, 0.0f}, {0.1f, 0.36f, 0.1f}));
                mesh::append(mesh, gfx::shapes::cylinder(0.05f, 0.3f, 16, true), mesh::translate({-0.3f, 0.36f, -0.12f}));
                mesh::append(mesh, mesh::box({-0.3f, 0.02f, -0.12f}, {0.3f, 0.04f, 0.3f}));
                at.y += 0.9;
            } else if (prop.id == "he_dewar") { // ⁴He dewar / gas bottle
                spec.material = "stainless";
                mesh::append(mesh, gfx::shapes::cylinder(0.2f, 1.4f, 24, true));
                mesh::append(mesh, gfx::shapes::cylinder(0.07f, 0.1f, 12, true), mesh::translate({0, 0.75f, 0}));
                at.x += 0.5 * k;
                at.y += 0.7;
            } else if (prop.id == "optical_table") {
                spec.material = "black_anodized";
                auto size = prop.size_m.value_or(glm::dvec2(2.4, 1.2));
                mesh::append(mesh, mesh::box({0, 0, 0}, {static_cast<float>(size.x), 0.2f, static_cast<float>(size.y)}));
                at.y += 0.8;
            } else {
                spec.material = "plastic_grey";
                mesh::append(mesh, mesh::box({0, 0, 0}, {0.5f, 0.5f, 0.5f}));
                at.y += 0.25;
            }
            spec.local = Transform::at(at);
            const std::string propId = prop.id;
            const ComponentId propNode = addProp(props, std::move(spec), mesh);
            // Bench-top instruments: the power meter is the lab's absolute dBm reference and lives
            // on the assembly bench rather than in a rack (spec 12 §14).
            if (propId == "bench" && propNode.value != 0 && scene_.catalog().contains("power_meter")) {
                gfx::MeshData head;
                mesh::append(head, mesh::box({0, 0, 0}, {0.12f, 0.05f, 0.16f}));
                mesh::appendColored(head, mesh::box({0, 0.026f, -0.03f}, {0.08f, 0.004f, 0.05f}),
                                    {0.05f, 0.12f, 0.08f, 1.0f}); // display
                NodeSpec meter;
                meter.descriptor = "power_meter";
                meter.instance = "power_meter";
                meter.group = Group::Room;
                meter.material = "plastic_grey";
                meter.local = Transform::at({0.35, 0.025, 0.15});
                addProp(propNode, std::move(meter), head);
            }
        }
    }
    if (layout_.compressor) { // pulse-tube compressor, 0.6 × 0.9 footprint (spec 17 §2)
        gfx::MeshData mesh;
        mesh::append(mesh, mesh::box({0, 0, 0}, {0.6f, 0.8f, 0.9f}));
        mesh::appendColored(mesh, mesh::box({0, 0.42f, 0}, {0.5f, 0.04f, 0.8f}), {0.2f, 0.2f, 0.22f, 1.0f});
        NodeSpec spec;
        spec.descriptor = "pt_compressor";
        spec.instance = "pt_compressor";
        spec.display = "Pulse-tube compressor";
        spec.group = Group::Room;
        spec.material = "plastic_grey";
        spec.local = Transform::at(*layout_.compressor + glm::dvec3(0.0, 0.4, 0.0));
        addProp(props, std::move(spec), mesh);
    }
    return {};
}


// Spec 17 §11 `lights[]` — one emissive panel per luminaire, flush with the ceiling; the renderer
// point lights come from the same list (SceneRenderer::submit).
Status SceneBuilder::buildLights(ComponentId room) {
    int k = 0;
    for (const auto& light : layout_.lights) {
        gfx::MeshData panel;
        const auto w = static_cast<float>(light.size_m.x), d = static_cast<float>(light.size_m.y);
        mesh::appendColored(panel, mesh::box({0.0f, 0.02f, 0.0f}, {w + 0.06f, 0.04f, d + 0.06f}), {0.9f, 0.9f, 0.9f, 1.0f}); // housing
        mesh::append(panel, mesh::box({0.0f, -0.002f, 0.0f}, {w, 0.004f, d}));                                             // diffuser
        addScenery(room, std::format("light_panel[{}]", k), std::format("Ceiling light {}", k + 1), Group::Room,
                   Transform::at(light.position_m), std::move(panel), "light_panel");
        ++k;
    }
    return {};
}

} // namespace qlab::lab
