// Spec 17 §2 — the room shell (floor, walls with the door and window openings, skirting, ceiling
// light panels, cable tray with its bundle and the drop to the top plate). Props are
// BuildProps.cpp; the door, window and signage are BuildRoomDetail.cpp.
#include "Lab/Builder.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <format>

namespace qlab::lab {

SceneBuilder::WallOpening SceneBuilder::doorOpening() const {
    return WallOpening{0.3 * layout_.roomDepth_m, 0.90, 0.0,
                       2.10}; // spec 17 §2: door in the −X wall at z = 0.3 D
}

std::optional<SceneBuilder::WallOpening> SceneBuilder::windowOpening() const {
    if (!layout_.compressor || !layout_.compressorOutside)
        return std::nullopt;
    // A 0.6 × 0.9 m window at eye height, centred on the compressor and clear of the door frame.
    const WallOpening door = doorOpening();
    const double zMin = door.z + 0.5 * door.width + 0.08 + 0.30,
                 zMax = 0.5 * layout_.roomDepth_m - 0.08 - 0.30;
    if (zMin > zMax)
        return std::nullopt;
    return WallOpening{std::clamp(layout_.compressor->z, zMin, zMax), 0.60, 1.20, 0.90};
}

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
    addScenery(room, "wall_z", "Wall (−Z)", Group::Room,
               Transform::at(0.0, 0.5 * H, -0.5 * D - 0.05), mesh::box({0, 0, 0}, {w, h, 0.1f}),
               "wall");
    { // −X wall as pieces around the door (and window) openings, so they are real openings
        std::vector<WallOpening> openings{doorOpening()};
        if (auto win = windowOpening())
            openings.push_back(*win);
        std::sort(openings.begin(), openings.end(),
                  [](const WallOpening& a, const WallOpening& b) { return a.z < b.z; });
        gfx::MeshData wall;
        auto piece = [&](double z0, double z1, double y0, double y1) {
            if (z1 - z0 < 1e-6 || y1 - y0 < 1e-6)
                return;
            mesh::append(
                wall, mesh::box({0.0f, static_cast<float>(0.5 * (y0 + y1)),
                                 static_cast<float>(0.5 * (z0 + z1))},
                                {0.1f, static_cast<float>(y1 - y0), static_cast<float>(z1 - z0)}));
        };
        double z = -0.5 * D;
        for (const WallOpening& o : openings) {
            piece(z, o.z - 0.5 * o.width, 0.0, H);
            piece(o.z - 0.5 * o.width, o.z + 0.5 * o.width, 0.0, o.y0);
            piece(o.z - 0.5 * o.width, o.z + 0.5 * o.width, o.y0 + o.height, H);
            z = o.z + 0.5 * o.width;
        }
        piece(z, 0.5 * D, 0.0, H);
        addScenery(room, "wall_x", "Wall (−X)", Group::Room,
                   Transform::at(-0.5 * W - 0.05, 0.0, 0.0), std::move(wall), "wall");
    }
    { // skirting board along both walls, interrupted at the door
        const WallOpening door = doorOpening();
        gfx::MeshData skirting;
        const float dz0 = static_cast<float>(door.z - 0.5 * door.width - 0.08),
                    dz1 = static_cast<float>(door.z + 0.5 * door.width + 0.08);
        mesh::append(skirting, mesh::box({-0.5f * w + 0.008f, 0.05f, 0.5f * (-0.5f * d + dz0)},
                                         {0.016f, 0.1f, dz0 + 0.5f * d}));
        mesh::append(skirting, mesh::box({-0.5f * w + 0.008f, 0.05f, 0.5f * (dz1 + 0.5f * d)},
                                         {0.016f, 0.1f, 0.5f * d - dz1}));
        mesh::append(skirting, mesh::box({0.0f, 0.05f, -0.5f * d + 0.008f}, {w, 0.1f, 0.016f}));
        addScenery(room, "skirting", "Skirting board", Group::Room, {}, std::move(skirting),
                   "plastic_grey");
    }
    QXL_TRY(buildDoorAndWindow(room));
    QXL_TRY(buildSignage(room));
    QXL_TRY(buildLights(room));

    if (scene_.catalog().contains("cable_tray") && !layout_.racks.empty()) {
        // Ceiling tray: from above the fridge to the first rack's x, then along the rack row so
        // every rack's loom rises straight into the channel (BuildRack.cpp).
        const double ceiling = H - 0.3;
        core::Json pts = core::Json::array();
        glm::dvec3 first = layout_.racks.front().position_m, last = layout_.racks.back().position_m;
        glm::dvec3 fridge = layout_.fridgePosition_m;
        std::vector<glm::dvec3> route{{fridge.x, ceiling, fridge.z},
                                      {first.x, ceiling, fridge.z},
                                      {first.x, ceiling, first.z}};
        if (std::abs(last.x - first.x) > 1e-6)
            route.push_back({last.x + 0.25, ceiling, first.z});
        for (glm::dvec3 p : route)
            pts.push_back({p.x, p.y, p.z});
        // the bundle drops from the tray onto the top plate's feedthrough ring (a `cable_loom`)
        if (layout_.hasFridge()) {
            gfx::MeshData drop;
            const float top = static_cast<float>(ceiling - 0.03),
                        plate = static_cast<float>(layout_.stages.front().height_m + 0.02);
            for (int k = 0; k < 12; ++k) {
                float a = 0.52f * static_cast<float>(k),
                      r = 0.16f + 0.02f * static_cast<float>(k % 3);
                glm::vec3 foot{static_cast<float>(fridge.x) + r * std::cos(a), plate,
                               static_cast<float>(fridge.z) + r * std::sin(a)};
                glm::vec3 head{static_cast<float>(fridge.x) + 0.06f * std::cos(a), top,
                               static_cast<float>(fridge.z) + 0.06f * std::sin(a)};
                glm::vec3 mid = 0.5f * (foot + head) +
                                glm::vec3(0.4f * (foot.x - head.x), 0.0f, 0.4f * (foot.z - head.z));
                mesh::appendColored(drop, gfx::shapes::tube({head, mid, foot}, 0.005f, 8, true),
                                    {0.12f, 0.14f, 0.30f, 1.0f});
            }
            NodeSpec loom;
            loom.descriptor = "cable_loom";
            loom.instance = "cable_drop";
            loom.display = "Cable drop to the top plate";
            loom.group = Group::Room;
            loom.material = "cable_jacket";
            addProp(room, std::move(loom), drop);
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

// Spec 17 §11 `lights[]` — one emissive panel per luminaire, flush with the ceiling; the renderer
// point lights come from the same list (SceneRenderer::submit).
Status SceneBuilder::buildLights(ComponentId room) {
    int k = 0;
    for (const auto& light : layout_.lights) {
        gfx::MeshData panel;
        const auto w = static_cast<float>(light.size_m.x), d = static_cast<float>(light.size_m.y);
        mesh::appendColored(panel, mesh::box({0.0f, 0.02f, 0.0f}, {w + 0.06f, 0.04f, d + 0.06f}),
                            {0.9f, 0.9f, 0.9f, 1.0f});                         // housing
        mesh::append(panel, mesh::box({0.0f, -0.002f, 0.0f}, {w, 0.004f, d})); // diffuser
        addScenery(room, std::format("light_panel[{}]", k), std::format("Ceiling light {}", k + 1),
                   Group::Room, Transform::at(light.position_m), std::move(panel), "light_panel");
        ++k;
    }
    return {};
}

} // namespace qlab::lab
