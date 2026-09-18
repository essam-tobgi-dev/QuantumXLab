// Spec 17 §2 (rack detail pass) — furniture and bench instruments: the control workstation
// (desk, two monitors on arms, keyboard, mouse, PC), a task chair, the optical-breadboard
// assembly bench, the stereo microscope, the wedge wire bonder and the sample box.
#include "Lab/MeshOps.hpp"
#include "Lab/PropMeshes.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace qlab::lab::props {

namespace {
const glm::vec4 kDark{0.10f, 0.10f, 0.11f, 1.0f};
const glm::vec4 kBlack{0.05f, 0.05f, 0.06f, 1.0f};
const glm::vec4 kSteel{0.72f, 0.73f, 0.74f, 1.0f};
const glm::vec4 kAlu{0.80f, 0.81f, 0.82f, 1.0f};
const glm::vec4 kWood{0.62f, 0.50f, 0.36f, 1.0f};
const glm::vec4 kScreen{0.04f, 0.07f, 0.11f, 1.0f};
const glm::vec4 kKey{0.80f, 0.80f, 0.78f, 1.0f};
const glm::vec4 kFabric{0.22f, 0.24f, 0.28f, 1.0f};
const glm::vec4 kFace{0.95f, 0.95f, 0.93f, 1.0f};
const glm::vec4 kGreenLed{0.2f, 1.0f, 0.35f, 1.0f};
const glm::vec4 kBlueGrey{0.35f, 0.42f, 0.52f, 1.0f};
const glm::vec4 kHole{0.25f, 0.25f, 0.26f, 1.0f};

glm::mat4 spinY(glm::vec3 at, float deg) { return mesh::translate(at) * glm::rotate(glm::mat4(1.0f), glm::radians(deg), {0.0f, 1.0f, 0.0f}); }
glm::mat4 spinX(glm::vec3 at, float deg) { return mesh::translate(at) * glm::rotate(glm::mat4(1.0f), glm::radians(deg), {1.0f, 0.0f, 0.0f}); }
glm::mat4 alongX(glm::vec3 at) { return mesh::translate(at) * mesh::alignY({1.0f, 0.0f, 0.0f}); }

// 24-inch monitor (slim bezel, dark screen, power LED) on a gas-spring arm from a post at `post`.
void monitorOnArm(MeshData& m, glm::vec3 post, glm::vec3 centre, float yawDeg) {
    mesh::appendColored(m, gfx::shapes::cylinder(0.018f, centre.y + 0.05f, 12, true), kDark, mesh::translate({post.x, 0.5f * (centre.y + 0.05f), post.z}));
    glm::vec3 elbow{post.x, centre.y + 0.03f, post.z};
    mesh::appendColored(m, gfx::shapes::tube({elbow, centre + glm::vec3(0.0f, 0.03f, -0.03f)}, 0.012f, 10, false), kDark);
    const glm::mat4 f = spinY(centre, yawDeg);
    mesh::appendColored(m, mesh::box({0.0f, 0.0f, 0.0f}, {0.56f, 0.34f, 0.018f}), kBlack, f);      // bezel / back
    mesh::appendColored(m, mesh::box({0.0f, 0.004f, 0.0095f}, {0.545f, 0.318f, 0.001f}), kScreen, f); // glass
    mesh::appendColored(m, gfx::shapes::cylinder(0.002f, 0.002f, 8, true), kGreenLed, f * mesh::translate({0.25f, -0.163f, 0.010f}) * mesh::alignY({0.0f, 0.0f, 1.0f}));
}
} // namespace

MeshData workstation(float w, float d) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, -0.015f, 0.0f}, {w, 0.03f, d}), kWood);
    for (float side : {-1.0f, 1.0f}) // pedestal legs and the modesty panel
        mesh::appendColored(m, mesh::box({side * (0.5f * w - 0.04f), -0.39f, 0.0f}, {0.05f, 0.72f, d - 0.10f}), kDark);
    mesh::appendColored(m, mesh::box({0.0f, -0.30f, -0.5f * d + 0.06f}, {w - 0.20f, 0.45f, 0.02f}), kDark);
    mesh::appendColored(m, gfx::shapes::cylinder(0.03f, 0.002f, 16, true), kBlack, mesh::translate({0.35f * w, 0.001f, -0.5f * d + 0.08f})); // cable grommet
    monitorOnArm(m, {-0.30f, 0.0f, -0.5f * d + 0.08f}, {-0.31f, 0.40f, -0.5f * d + 0.20f}, 14.0f);
    monitorOnArm(m, {0.30f, 0.0f, -0.5f * d + 0.08f}, {0.31f, 0.40f, -0.5f * d + 0.20f}, -14.0f);
    mesh::appendColored(m, mesh::box({0.0f, 0.009f, 0.10f}, {0.44f, 0.018f, 0.15f}), kDark); // keyboard
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 15; ++c)
            mesh::appendColored(m, mesh::box({-0.20f + 0.0285f * static_cast<float>(c), 0.021f, 0.045f + 0.0265f * static_cast<float>(r)}, {0.016f, 0.005f, 0.016f}), kKey);
    mesh::appendColored(m, mesh::box({0.32f, 0.018f, 0.12f}, {0.062f, 0.036f, 0.11f}), kDark); // mouse
    mesh::appendColored(m, mesh::box({0.32f, 0.037f, 0.085f}, {0.006f, 0.004f, 0.016f}), kSteel);
    mesh::appendColored(m, mesh::box({0.5f * w - 0.16f, -0.53f, 0.05f}, {0.18f, 0.44f, 0.46f}), kBlack); // PC tower
    mesh::appendColored(m, gfx::shapes::cylinder(0.003f, 0.002f, 8, true), kGreenLed, mesh::translate({0.5f * w - 0.16f, -0.36f, 0.281f}) * mesh::alignY({0.0f, 0.0f, 1.0f}));
    return m;
}

MeshData officeChair() {
    MeshData m;
    for (int k = 0; k < 5; ++k) {
        mesh::appendColored(m, mesh::box({0.16f, 0.0f, 0.0f}, {0.32f, 0.03f, 0.04f}), kBlack, spinY({0.0f, 0.06f, 0.0f}, 72.0f * static_cast<float>(k)));
        mesh::appendColored(m, gfx::shapes::cylinder(0.025f, 0.02f, 12, true), kBlack, spinY({0.0f, 0.03f, 0.0f}, 72.0f * static_cast<float>(k)) * alongX({0.31f, 0.0f, 0.0f}));
    }
    mesh::appendColored(m, gfx::shapes::cylinder(0.028f, 0.36f, 16, true), kSteel, mesh::translate({0.0f, 0.25f, 0.0f})); // gas lift
    mesh::appendColored(m, mesh::box({0.0f, 0.46f, 0.0f}, {0.48f, 0.07f, 0.46f}), kFabric);                            // seat
    mesh::appendColored(m, mesh::box({0.0f, 0.78f, -0.21f}, {0.44f, 0.52f, 0.06f}), kFabric);                          // backrest
    mesh::appendColored(m, mesh::box({0.0f, 0.50f, -0.21f}, {0.10f, 0.14f, 0.05f}), kBlack);                           // back bracket
    for (float side : {-1.0f, 1.0f}) {
        mesh::appendColored(m, mesh::box({side * 0.26f, 0.62f, 0.02f}, {0.05f, 0.03f, 0.26f}), kBlack); // armrests
        mesh::appendColored(m, mesh::box({side * 0.26f, 0.54f, 0.02f}, {0.03f, 0.14f, 0.05f}), kBlack);
    }
    return m;
}

MeshData bench(float w, float h, float d, bool holes) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, -0.03f, 0.0f}, {w, 0.06f, d}), mesh::kWhite); // honeycomb breadboard, 60 mm thick
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f})
            mesh::appendColored(m, mesh::box({sx * (0.5f * w - 0.05f), -0.06f - 0.5f * (h - 0.06f), sz * (0.5f * d - 0.05f)}, {0.06f, h - 0.06f, 0.06f}), kDark);
    mesh::appendColored(m, mesh::box({0.0f, -h + 0.16f, 0.0f}, {w - 0.14f, 0.02f, d - 0.14f}), kDark); // lower shelf
    mesh::appendColored(m, mesh::box({0.0f, -0.03f, 0.5f * d + 0.002f}, {0.06f, 0.02f, 0.004f}), kSteel); // ESD bonding point
    if (holes) { // M6 tapped holes on a 25 mm grid, 25 mm from the edges
        int nx = static_cast<int>((w - 0.05f) / 0.025f) + 1, nz = static_cast<int>((d - 0.05f) / 0.025f) + 1;
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < nz; ++j)
                mesh::appendColored(m, mesh::disc(0.003f, 0.0004f, 6, true), kHole,
                                    mesh::translate({-0.5f * w + 0.025f + 0.025f * static_cast<float>(i), 0.0f, -0.5f * d + 0.025f + 0.025f * static_cast<float>(j)}));
    }
    return m;
}

MeshData microscope(float w, float h, float d) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, 0.012f, 0.0f}, {w, 0.024f, d}), kDark);                                          // base
    mesh::appendColored(m, mesh::box({0.0f, 0.03f, 0.08f * d}, {0.55f * w, 0.012f, 0.5f * d}), kAlu);                         // stage plate
    mesh::appendColored(m, gfx::shapes::cylinder(0.02f, 0.9f * h, 16, true), kSteel, mesh::translate({0.0f, 0.024f + 0.45f * h, -0.36f * d})); // column
    mesh::appendColored(m, mesh::box({0.0f, 0.78f * h, -0.12f * d}, {0.05f, 0.05f, 0.5f * d}), kDark);                        // boom arm
    mesh::appendColored(m, mesh::box({0.0f, 0.62f * h, 0.10f * d}, {0.10f, 0.14f, 0.11f}), kDark);                            // zoom body
    for (float sx : {-0.028f, 0.028f}) // eyepiece tubes, 45° up toward the user (+z)
        mesh::appendColored(m, gfx::shapes::cylinder(0.014f, 0.09f, 12, true), kDark, spinX({sx, 0.70f * h, 0.16f * d}, -45.0f) * mesh::translate({0.0f, 0.045f, 0.0f}));
    mesh::appendColored(m, gfx::shapes::cylinder(0.022f, 0.035f, 16, true), kDark, mesh::translate({0.0f, 0.62f * h - 0.085f, 0.10f * d})); // objective
    mesh::appendColored(m, gfx::shapes::torus(0.045f, 0.006f, 24, 8), kFace, mesh::translate({0.0f, 0.62f * h - 0.10f, 0.10f * d}));         // LED ring light
    for (float sx : {-1.0f, 1.0f}) // focus and zoom knobs on the sides of the body
        mesh::appendColored(m, gfx::shapes::cylinder(0.018f, 0.012f, 16, true), kBlack, alongX({sx * 0.056f, 0.62f * h, 0.10f * d}));
    return m;
}

MeshData wireBonder(float w, float h, float d) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, 0.13f * h, 0.0f}, {w, 0.26f * h, d}), kBlueGrey);                          // base cabinet
    mesh::appendColored(m, mesh::box({0.0f, 0.27f * h, 0.15f * d}, {0.55f * w, 0.02f * h, 0.45f * d}), kAlu);           // heated work stage
    mesh::appendColored(m, mesh::box({0.0f, 0.29f * h, 0.15f * d}, {0.12f * w, 0.008f, 0.12f * w}), kFace);             // package holder
    mesh::appendColored(m, mesh::box({0.0f, 0.62f * h, -0.36f * d}, {0.24f * w, 0.72f * h, 0.20f * d}), kBlueGrey);     // column
    mesh::appendColored(m, mesh::box({0.0f, 0.80f * h, -0.06f * d}, {0.10f * w, 0.08f * h, 0.62f * d}), kDark);        // bond-head arm
    mesh::appendColored(m, mesh::box({0.0f, 0.62f * h, 0.16f * d}, {0.08f * w, 0.30f * h, 0.07f * d}), kDark);         // bond head
    mesh::appendColored(m, gfx::shapes::cylinder(0.0025f, 0.05f * h, 8, true), kSteel, mesh::translate({0.0f, 0.45f * h, 0.16f * d})); // wedge tool
    mesh::appendColored(m, gfx::shapes::torus(0.03f, 0.008f, 20, 8), kAlu, spinX({0.0f, 0.86f * h, 0.20f * d}, 90.0f)); // wire spool
    mesh::appendColored(m, gfx::shapes::cylinder(0.02f, 0.30f * h, 16, true), kDark, spinX({0.0f, 0.95f * h, 0.05f * d}, -40.0f)); // microscope tube
    mesh::appendColored(m, mesh::box({-0.30f * w, 0.26f * h + 0.02f, 0.30f * d}, {0.14f * w, 0.02f, 0.10f * d}), kDark); // keypad
    mesh::appendColored(m, gfx::shapes::cylinder(0.008f, 0.07f, 10, true), kBlack, mesh::translate({0.32f * w, 0.26f * h + 0.05f, 0.30f * d})); // joystick
    mesh::appendColored(m, gfx::shapes::sphere(0.014f, 12, 6), kBlack, mesh::translate({0.32f * w, 0.26f * h + 0.09f, 0.30f * d}));
    mesh::appendColored(m, mesh::box({-0.36f * w, 0.62f * h, -0.30f * d}, {0.006f, 0.30f * h, 0.34f * d}), kBlack);     // process monitor
    mesh::appendColored(m, mesh::box({-0.355f * w, 0.62f * h, -0.30f * d}, {0.002f, 0.27f * h, 0.30f * d}), kScreen);
    return m;
}

MeshData sampleBox(float w, float h, float d) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, 0.28f * h, 0.0f}, {w, 0.56f * h, d}), kBlueGrey);
    mesh::appendColored(m, mesh::box({0.0f, 0.78f * h, 0.0f}, {w, 0.42f * h, d}), kAlu);                  // lid
    mesh::appendColored(m, mesh::box({0.0f, 0.99f * h, 0.0f}, {0.6f * w, 0.004f, 0.4f * d}), kFace);      // sample label
    mesh::appendColored(m, mesh::box({0.0f, 0.99f * h + 0.0005f, 0.0f}, {0.4f * w, 0.0006f, 0.06f * d}), kDark);
    mesh::appendColored(m, mesh::box({0.0f, 0.55f * h, 0.5f * d + 0.003f}, {0.14f * w, 0.20f * h, 0.006f}), kDark); // latch
    mesh::appendColored(m, gfx::shapes::cylinder(0.005f, 0.006f, 8, true), kSteel, alongX({0.5f * w + 0.003f, 0.35f * h, -0.35f * d})); // purge fitting
    return m;
}

} // namespace qlab::lab::props
