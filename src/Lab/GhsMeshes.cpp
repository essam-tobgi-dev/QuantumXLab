// Spec 17 §3.4 (rack detail pass) — the gas-handling plant around the cabinet: dry scroll pump,
// hermetic ³He circulation compressor, mixture dump tanks with their valves and gauges, the turbo
// pump on the cabinet roof and the LN₂ cold trap, in their real proportions.
#include "Lab/MeshOps.hpp"
#include "Lab/PropMeshes.hpp"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace qlab::lab::props {

namespace {
const glm::vec4 kDark{0.10f, 0.10f, 0.11f, 1.0f};
const glm::vec4 kSteel{0.72f, 0.73f, 0.74f, 1.0f};
const glm::vec4 kAlu{0.80f, 0.81f, 0.82f, 1.0f};
const glm::vec4 kPaint{0.62f, 0.64f, 0.66f, 1.0f};
const glm::vec4 kBlue{0.20f, 0.32f, 0.55f, 1.0f};
const glm::vec4 kFace{0.95f, 0.95f, 0.93f, 1.0f};
const glm::vec4 kRed{0.75f, 0.12f, 0.10f, 1.0f};

glm::mat4 alongZ(glm::vec3 at) { return mesh::translate(at) * mesh::alignY({0.0f, 0.0f, 1.0f}); }
glm::mat4 alongX(glm::vec3 at) { return mesh::translate(at) * mesh::alignY({1.0f, 0.0f, 0.0f}); }

// Handwheel valve on a vertical stem at `at` (stem base), wheel radius r.
void handwheel(MeshData& m, glm::vec3 at, float r) {
    mesh::appendColored(m, gfx::shapes::cylinder(0.35f * r, 1.2f * r, 12, true), kSteel, mesh::translate(at + glm::vec3(0.0f, 0.6f * r, 0.0f)));
    mesh::appendColored(m, gfx::shapes::cylinder(0.12f * r, 1.6f * r, 8, true), kDark, mesh::translate(at + glm::vec3(0.0f, 1.6f * r, 0.0f)));
    glm::vec3 wheel = at + glm::vec3(0.0f, 2.3f * r, 0.0f);
    mesh::appendColored(m, gfx::shapes::torus(r, 0.09f * r, 24, 8), kRed, mesh::translate(wheel));
    for (int k = 0; k < 3; ++k)
        mesh::appendColored(m, mesh::box({0.5f * r, 0.0f, 0.0f}, {r, 0.1f * r, 0.1f * r}), kRed,
                            mesh::translate(wheel) * glm::rotate(glm::mat4(1.0f), glm::radians(120.0f * static_cast<float>(k)), {0.0f, 1.0f, 0.0f}));
}
// Small pressure gauge (dial facing +z) on a stub at `at`.
void gauge(MeshData& m, glm::vec3 at, float r) {
    mesh::appendColored(m, gfx::shapes::cylinder(0.25f * r, 0.6f * r, 8, true), kSteel, mesh::translate(at - glm::vec3(0.0f, 0.3f * r, 0.0f)));
    mesh::appendColored(m, gfx::shapes::cylinder(r, 0.35f * r, 24, true), kDark, alongZ(at));
    mesh::appendColored(m, mesh::disc(0.85f * r, 0.18f * r + 0.0005f, 24, true), kFace, alongZ(at));
    mesh::appendColored(m, mesh::box({0.0f, 0.3f * r, 0.0f}, {0.04f * r, 0.6f * r, 0.0006f}), kDark, mesh::translate(at + glm::vec3(0.0f, 0.0f, 0.18f * r + 0.001f)));
}
} // namespace

MeshData scrollPump(float w, float h, float d) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, 0.015f, 0.0f}, {w, 0.03f, d}), kDark); // base frame
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f})
            mesh::appendColored(m, mesh::box({sx * (0.5f * w - 0.03f), 0.04f, sz * (0.5f * d - 0.03f)}, {0.03f, 0.02f, 0.03f}), kDark); // rubber feet
    const float r = 0.32f * h;
    mesh::appendColored(m, gfx::shapes::cylinder(r, 0.5f * d, 32, true), kPaint, alongZ({0.0f, 0.03f + r, -0.15f * d})); // motor
    mesh::appendColored(m, gfx::shapes::cylinder(1.1f * r, 0.06f * d, 32, true), kDark, alongZ({0.0f, 0.03f + r, -0.5f * d + 0.04f * d})); // fan cowl
    for (int k = 0; k < 6; ++k) // cowl grille
        mesh::appendColored(m, mesh::box({0.0f, 0.03f + r + 1.6f * r * (static_cast<float>(k) / 5.0f - 0.5f), -0.5f * d + 0.006f * d}, {1.6f * r, 0.012f, 0.004f}), kPaint);
    mesh::appendColored(m, mesh::box({0.0f, 0.03f + 0.42f * h, 0.28f * d}, {0.62f * w, 0.84f * h, 0.36f * d}), kPaint); // scroll head
    mesh::appendColored(m, gfx::shapes::cylinder(0.16f * w, 0.05f * h, 24, true), kSteel, mesh::translate({0.0f, 0.03f + 0.84f * h + 0.025f * h, 0.28f * d})); // KF40 inlet
    mesh::appendColored(m, gfx::shapes::cylinder(0.05f * w, 0.30f * w, 12, true), kSteel, alongX({0.5f * w - 0.05f * w, 0.5f * h, 0.28f * d})); // exhaust
    mesh::appendColored(m, mesh::box({0.0f, 0.03f + 0.15f * h, 0.5f * d - 0.004f}, {0.4f * w, 0.14f * h, 0.006f}), kDark); // nameplate / controls
    return m;
}

MeshData he3Compressor(float w, float h, float d) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, 0.03f, 0.0f}, {w, 0.06f, d}), kDark); // skid
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f})
            mesh::appendColored(m, mesh::box({sx * (0.5f * w - 0.06f), 0.09f, sz * (0.5f * d - 0.06f)}, {0.06f, 0.06f, 0.06f}), kDark); // vibration mounts
    const float r = 0.34f * std::min(w, d), body = 0.66f * h;
    mesh::appendColored(m, gfx::shapes::cylinder(r, body, 32, true), kBlue, mesh::translate({0.0f, 0.12f + 0.5f * body, 0.0f})); // hermetic shell
    mesh::appendColored(m, gfx::shapes::sphere(r, 24, 8), kBlue, mesh::translate({0.0f, 0.12f + body, 0.0f}) * glm::scale(glm::mat4(1.0f), {1.0f, 0.35f, 1.0f}));
    for (float sx : {-1.0f, 1.0f}) // suction and discharge lines up from the shell
        mesh::appendColored(m, gfx::shapes::cylinder(0.012f, 0.32f * h, 12, true), kSteel, mesh::translate({sx * 0.5f * r, 0.12f + body + 0.16f * h, 0.0f}));
    mesh::appendColored(m, mesh::box({0.5f * w - 0.06f, 0.12f + 0.3f * body, 0.0f}, {0.10f, 0.4f * body, 0.5f * d}), kDark); // terminal / control box
    mesh::appendColored(m, mesh::box({0.5f * w - 0.005f, 0.12f + 0.3f * body, 0.0f}, {0.004f, 0.25f * body, 0.3f * d}), kFace); // label
    return m;
}

MeshData dumpTank(float r, float h) {
    MeshData m;
    mesh::appendColored(m, gfx::shapes::cylinder(0.72f * r, 0.08f, 24, true), kDark, mesh::translate({0.0f, 0.04f, 0.0f})); // skirt
    const float y0 = 0.08f + r;
    mesh::appendColored(m, gfx::shapes::sphere(r, 32, 16), kSteel, mesh::translate({0.0f, y0, 0.0f}));            // lower dished head
    mesh::appendColored(m, gfx::shapes::cylinder(r, h, 32, false), kSteel, mesh::translate({0.0f, y0 + 0.5f * h, 0.0f}));
    mesh::appendColored(m, gfx::shapes::sphere(r, 32, 16), kSteel, mesh::translate({0.0f, y0 + h, 0.0f}));        // upper dished head
    mesh::appendColored(m, gfx::shapes::cylinder(1.004f * r, 0.08f * h, 32, false), kBlue, mesh::translate({0.0f, y0 + 0.5f * h, 0.0f})); // identification band
    mesh::appendColored(m, gfx::shapes::cylinder(0.08f * r, 0.15f * r, 16, true), kSteel, mesh::translate({0.0f, y0 + h + r + 0.07f * r, 0.0f})); // top nozzle
    handwheel(m, {0.0f, y0 + h + r + 0.15f * r, 0.0f}, 0.22f * r);
    gauge(m, {0.45f * r, y0 + h + r + 0.55f * r, 0.0f}, 0.12f * r);
    // outlet pipe from the top nozzle down the side toward the cabinet (+z), with an isolation valve
    std::vector<glm::vec3> pipe{{0.0f, y0 + h + r + 0.6f * r, 0.0f}, {0.0f, y0 + h + r + 0.6f * r, 1.25f * r}, {0.0f, 0.10f, 1.25f * r}, {0.0f, 0.10f, 2.2f * r}};
    mesh::appendColored(m, gfx::shapes::tube(pipe, 0.035f * r, 8, false), kSteel);
    handwheel(m, {0.0f, y0 + h + r + 0.6f * r, 0.9f * r}, 0.12f * r);
    return m;
}

MeshData turboPump(float r, float h) {
    MeshData m;
    mesh::appendColored(m, gfx::shapes::cylinder(r, 0.62f * h, 32, true), kAlu, mesh::translate({0.0f, 0.31f * h, 0.0f}));       // rotor housing
    mesh::appendColored(m, gfx::shapes::cylinder(1.4f * r, 0.10f * h, 32, true), kSteel, mesh::translate({0.0f, 0.67f * h, 0.0f})); // inlet flange (ISO-K)
    mesh::appendColored(m, gfx::shapes::cylinder(1.15f * r, 0.28f * h, 32, true), kSteel, mesh::translate({0.0f, 0.86f * h, 0.0f})); // inlet screen / bellows
    for (int k = 0; k < 8; ++k) // flange claw clamps
        mesh::appendColored(m, mesh::box({1.35f * r, 0.0f, 0.0f}, {0.25f * r, 0.12f * h, 0.15f * r}), kDark,
                            mesh::translate({0.0f, 0.67f * h, 0.0f}) * glm::rotate(glm::mat4(1.0f), glm::radians(45.0f * static_cast<float>(k)), {0.0f, 1.0f, 0.0f}));
    mesh::appendColored(m, gfx::shapes::cylinder(0.22f * r, 1.2f * r, 12, true), kSteel, alongX({0.9f * r, 0.3f * h, 0.0f})); // fore-vacuum port KF25
    mesh::appendColored(m, mesh::box({-1.35f * r, 0.28f * h, 0.0f}, {0.9f * r, 0.5f * h, 1.4f * r}), kDark); // drive electronics
    mesh::appendColored(m, mesh::box({-1.35f * r, 0.42f * h, 0.7f * r + 0.001f}, {0.5f * r, 0.12f * h, 0.002f}), kFace); // status display
    return m;
}

MeshData ln2Trap(float r, float h) {
    MeshData m;
    mesh::appendColored(m, gfx::shapes::cylinder(r, h, 32, true), kSteel, mesh::translate({0.0f, 0.5f * h, 0.0f}));            // dewar
    mesh::appendColored(m, gfx::shapes::torus(r, 0.02f * h, 32, 8), kSteel, mesh::translate({0.0f, h, 0.0f}));                  // rolled rim
    mesh::appendColored(m, gfx::shapes::cylinder(0.85f * r, 0.03f * h, 32, true), kDark, mesh::translate({0.0f, h + 0.015f * h, 0.0f})); // foam lid
    for (float sx : {-0.4f, 0.4f}) { // trap inlet and outlet lines through the lid, bending toward the cabinet
        std::vector<glm::vec3> line{{sx * r, h + 0.03f * h, 0.0f}, {sx * r, h + 0.28f * h, 0.0f}, {sx * r, h + 0.28f * h, 1.8f * r}};
        mesh::appendColored(m, gfx::shapes::tube(line, 0.05f * r, 8, false), kSteel);
    }
    mesh::appendColored(m, mesh::box({0.0f, h + 0.02f * h, 0.7f * r}, {0.35f * r, 0.03f * h, 0.15f * r}), kBlue); // level sensor head
    mesh::appendColored(m, mesh::box({0.0f, 0.55f * h, r + 0.001f}, {0.8f * r, 0.25f * h, 0.002f}), kFace);      // label
    return m;
}

MeshData valveLabel(float w, float h) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, 0.0f, 0.0005f}, {w, h, 0.001f}), kFace);
    mesh::appendColored(m, mesh::box({0.0f, 0.0f, 0.0011f}, {0.6f * w, 0.4f * h, 0.0002f}), kDark);
    return m;
}

} // namespace qlab::lab::props
