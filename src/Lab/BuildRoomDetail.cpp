// Spec 17 §2 (rack detail pass) — the laboratory door in its opening, the window onto the
// pulse-tube compressor outside the −X wall, and the wall safety signs. All scenery: never picked.
#include "Lab/Builder.hpp"
#include "Lab/MeshOps.hpp"
#include "Lab/PropMeshes.hpp"
#include <format>

namespace qlab::lab {

Status SceneBuilder::buildDoorAndWindow(ComponentId room) {
    const double W = layout_.roomWidth_m;
    const WallOpening door = doorOpening();
    // The leaf's outer face lies on the wall's inner face (x = −W/2), the room at +x.
    addScenery(room, "door", "Door", Group::Room, Transform::at(-0.5 * W - 0.045, 0.0, door.z),
               props::labDoor(), "vertex");
    if (auto win = windowOpening()) {
        gfx::MeshData frame; // aluminium frame in the reveal, sill, and the glazing bar
        const auto wz = static_cast<float>(win->width), wy = static_cast<float>(win->height);
        const auto yc = static_cast<float>(win->y0 + 0.5 * win->height);
        for (float side : {-1.0f, 1.0f}) {
            mesh::appendColored(
                frame, mesh::box({0.0f, yc, side * (0.5f * wz - 0.02f)}, {0.10f, wy, 0.04f}),
                {0.55f, 0.56f, 0.58f, 1.0f});
            mesh::appendColored(frame,
                                mesh::box({0.0f, yc + side * (0.5f * wy - 0.02f), 0.0f},
                                          {0.10f, 0.04f, wz - 0.08f}),
                                {0.55f, 0.56f, 0.58f, 1.0f});
        }
        mesh::appendColored(frame,
                            mesh::box({0.07f, static_cast<float>(win->y0) - 0.012f, 0.0f},
                                      {0.24f, 0.024f, wz + 0.12f}),
                            {0.55f, 0.56f, 0.58f, 1.0f}); // sill
        addScenery(room, "window_frame", "Window frame", Group::Room,
                   Transform::at(-0.5 * W - 0.05, 0.0, win->z), std::move(frame), "vertex");
        addScenery(room, "window_glass", "Window", Group::Room,
                   Transform::at(-0.5 * W - 0.05, win->y0 + 0.5 * win->height, win->z),
                   mesh::box({0.0f, 0.0f, 0.0f}, {0.008f, wy - 0.08f, wz - 0.08f}), "glass");
        // helium flex lines to the compressor pass through the wall beside the window
        gfx::MeshData sleeve;
        for (float dy : {-0.05f, 0.05f})
            mesh::append(sleeve, gfx::shapes::cylinder(0.03f, 0.16f, 12, true),
                         mesh::translate({0.0f, static_cast<float>(win->y0) - 0.25f + dy * 2.0f,
                                          -0.5f * wz - 0.12f}) *
                             mesh::alignY({1.0f, 0.0f, 0.0f}));
        addScenery(room, "wall_sleeves", "Wall sleeves (helium lines)", Group::Room,
                   Transform::at(-0.5 * W - 0.05, 0.0, win->z), std::move(sleeve), "stainless");
    }
    return {};
}

Status SceneBuilder::buildSignage(ComponentId room) {
    const double W = layout_.roomWidth_m, D = layout_.roomDepth_m;
    struct Sign {
        const char* kind;
        const char* name;
        glm::dvec3 at; // sign centre; on the −Z wall they face +z, on the −X wall +x
        bool onZWall;
    };
    std::vector<Sign> signs;
    if (layout_.ghs) // cryogen / asphyxiation hazard beside the gas-handling system
        signs.push_back({"cryogen",
                         "Cryogen hazard sign",
                         {layout_.ghs->x + 0.9, 1.75, -0.5 * D + 0.004},
                         true});
    if (layout_.hasFridge()) // strong magnetic field: the fridge's magnet and the mu-metal shield's
                             // field
        signs.push_back({"magnet",
                         "Magnetic field sign",
                         {layout_.fridgePosition_m.x + 1.2, 1.75, -0.5 * D + 0.004},
                         true});
    if (!layout_.lasers.empty()) // laser radiation: class 3B / 4 beams on the optical table
        signs.push_back({"laser",
                         "Laser safety sign",
                         {layout_.fridgePosition_m.x - 0.6, 1.75, -0.5 * D + 0.004},
                         true});
    // a second cryogen sign at the door, seen when entering
    const WallOpening door = doorOpening();
    signs.push_back({"cryogen",
                     "Cryogen hazard sign (door)",
                     {-0.5 * W + 0.004, 1.75, door.z - 0.5 * door.width - 0.35},
                     false});
    int k = 0;
    for (const Sign& s : signs) {
        Transform pose =
            s.onZWall ? Transform::at(s.at) : Transform::rotated(s.at, {0.0, 1.0, 0.0}, 90.0);
        addScenery(room, std::format("sign[{}]", k++), s.name, Group::Room, pose,
                   props::safetySign(s.kind, 0.30f, 0.42f), "vertex");
    }
    return {};
}

} // namespace qlab::lab
