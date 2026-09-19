// Spec 17 §2 (rack detail pass) — room plant: the 100 L helium storage dewar, the pulse-tube
// compressor package, the laboratory door and the wall safety signs.
#include "Lab/MeshOps.hpp"
#include "Lab/PropMeshes.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace qlab::lab::props {

namespace {
const glm::vec4 kDark{0.10f, 0.10f, 0.11f, 1.0f};
const glm::vec4 kSteel{0.74f, 0.75f, 0.76f, 1.0f};
const glm::vec4 kPaint{0.58f, 0.60f, 0.63f, 1.0f};
const glm::vec4 kFace{0.95f, 0.95f, 0.93f, 1.0f};
const glm::vec4 kRed{0.75f, 0.12f, 0.10f, 1.0f};
const glm::vec4 kGold{0.90f, 0.75f, 0.40f, 1.0f};
const glm::vec4 kGreenLed{0.2f, 1.0f, 0.35f, 1.0f};
const glm::vec4 kYellow{0.98f, 0.80f, 0.05f, 1.0f};
const glm::vec4 kBlack{0.03f, 0.03f, 0.03f, 1.0f};
const glm::vec4 kGlass{0.55f, 0.72f, 0.85f, 1.0f};
const glm::vec4 kLeaf{0.62f, 0.63f, 0.64f, 1.0f};
const glm::vec4 kFrame{0.32f, 0.33f, 0.35f, 1.0f};

glm::mat4 alongZ(glm::vec3 at) {
    return mesh::translate(at) * mesh::alignY({0.0f, 0.0f, 1.0f});
}
glm::mat4 alongX(glm::vec3 at) {
    return mesh::translate(at) * mesh::alignY({1.0f, 0.0f, 0.0f});
}
glm::mat4 spinY(glm::vec3 at, float deg) {
    return mesh::translate(at) *
           glm::rotate(glm::mat4(1.0f), glm::radians(deg), {0.0f, 1.0f, 0.0f});
}
glm::mat4 spinZ(glm::vec3 at, float deg) {
    return mesh::translate(at) *
           glm::rotate(glm::mat4(1.0f), glm::radians(deg), {0.0f, 0.0f, 1.0f});
}

void handwheel(MeshData& m, glm::vec3 at, float r, const glm::mat4& frame) {
    mesh::appendColored(m, gfx::shapes::cylinder(0.12f * r, 1.6f * r, 8, true), kDark,
                        frame * mesh::translate(at + glm::vec3(0.0f, 0.8f * r, 0.0f)));
    mesh::appendColored(m, gfx::shapes::torus(r, 0.09f * r, 24, 8), kRed,
                        frame * mesh::translate(at + glm::vec3(0.0f, 1.6f * r, 0.0f)));
    for (int k = 0; k < 3; ++k)
        mesh::appendColored(
            m, mesh::box({0.5f * r, 0.0f, 0.0f}, {r, 0.1f * r, 0.1f * r}), kRed,
            frame * spinY(at + glm::vec3(0.0f, 1.6f * r, 0.0f), 120.0f * static_cast<float>(k)));
}
} // namespace

MeshData heDewar(float r, float h) {
    MeshData m;
    // castor base: a steel ring with four swivel castors
    mesh::appendColored(m, gfx::shapes::cylinder(0.92f * r, 0.03f, 32, true), kDark,
                        mesh::translate({0.0f, 0.075f, 0.0f}));
    for (int k = 0; k < 4; ++k)
        mesh::appendColored(m, gfx::shapes::cylinder(0.035f, 0.025f, 12, true), kDark,
                            spinY({0.0f, 0.035f, 0.0f}, 45.0f + 90.0f * static_cast<float>(k)) *
                                alongX({0.75f * r, 0.0f, 0.0f}));
    const float y0 = 0.09f;
    mesh::appendColored(m, gfx::shapes::cylinder(r, h, 48, true), mesh::kWhite,
                        mesh::translate({0.0f, y0 + 0.5f * h, 0.0f})); // outer vessel
    for (float f : {0.30f, 0.72f}) // rolled seams of the outer shell
        mesh::appendColored(m, gfx::shapes::torus(r, 0.006f, 48, 8), mesh::kWhite,
                            mesh::translate({0.0f, y0 + f * h, 0.0f}));
    mesh::appendColored(m, gfx::shapes::cylinder(0.72f * r, 0.08f, 32, true), mesh::kWhite,
                        mesh::translate({0.0f, y0 + h + 0.04f, 0.0f})); // shoulder
    mesh::appendColored(m, gfx::shapes::cylinder(0.07f, 0.13f, 24, true), mesh::kWhite,
                        mesh::translate({0.0f, y0 + h + 0.08f + 0.065f, 0.0f})); // neck
    mesh::appendColored(m, gfx::shapes::cylinder(0.11f, 0.014f, 24, true), kSteel,
                        mesh::translate({0.0f, y0 + h + 0.21f + 0.007f, 0.0f})); // neck flange
    mesh::appendColored(m, gfx::shapes::torus(0.45f * r, 0.012f, 32, 8), kSteel,
                        mesh::translate({0.0f, y0 + h + 0.09f, 0.0f})); // handling ring
    for (int k = 0; k < 3; ++k)                                         // ring stand-offs
        mesh::appendColored(m, mesh::box({0.36f * r, 0.0f, 0.0f}, {0.18f * r, 0.012f, 0.012f}),
                            kSteel,
                            spinY({0.0f, y0 + h + 0.09f, 0.0f}, 120.0f * static_cast<float>(k)));
    // transfer valve on the neck (handwheel to +x), vent / relief valve and the pressure gauge (+z)
    mesh::appendColored(m, gfx::shapes::cylinder(0.014f, 0.09f, 12, true), kSteel,
                        alongX({0.07f + 0.045f, y0 + h + 0.15f, 0.0f}));
    handwheel(m, {0.0f, 0.0f, 0.0f}, 0.03f, spinZ({0.16f, y0 + h + 0.15f, 0.0f}, -90.0f));
    mesh::appendColored(m, gfx::shapes::cylinder(0.010f, 0.06f, 10, true), kSteel,
                        alongZ({0.0f, y0 + h + 0.17f, 0.07f + 0.03f}));
    mesh::appendColored(m, gfx::shapes::cylinder(0.030f, 0.014f, 24, true), kDark,
                        alongZ({0.0f, y0 + h + 0.17f, 0.13f}));
    mesh::appendColored(m, mesh::disc(0.025f, 0.0075f, 24, true), kFace,
                        alongZ({0.0f, y0 + h + 0.17f, 0.13f}));
    mesh::appendColored(m, mesh::box({0.0f, 0.008f, 0.0f}, {0.0015f, 0.018f, 0.0006f}), kDark,
                        mesh::translate({0.0f, y0 + h + 0.17f, 0.1382f}));
    mesh::appendColored(m, gfx::shapes::cylinder(0.008f, 0.05f, 8, true), kGold,
                        mesh::translate({-0.04f, y0 + h + 0.21f + 0.025f, 0.03f})); // relief valve
    mesh::appendColored(
        m, mesh::box({0.0f, y0 + 0.55f * h, r + 0.0015f}, {0.9f * r, 0.16f * h, 0.003f}),
        kFace); // 100 L / contents label
    mesh::appendColored(
        m, mesh::box({0.0f, y0 + 0.55f * h, r + 0.0035f}, {0.6f * r, 0.04f * h, 0.0006f}), kDark);
    return m;
}

MeshData ptCompressor(float w, float h, float d) {
    MeshData m;
    const float cab = 0.86f * h, y0 = 0.09f;
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f})
            mesh::appendColored(m, gfx::shapes::cylinder(0.04f, 0.03f, 12, true), kDark,
                                alongX({sx * (0.5f * w - 0.08f), 0.04f, sz * (0.5f * d - 0.08f)}));
    mesh::appendColored(m, mesh::box({0.0f, y0 + 0.5f * cab, 0.0f}, {w, cab, d}), kPaint);
    mesh::appendColored(m, mesh::box({0.0f, y0 + cab + 0.01f, 0.0f}, {w - 0.02f, 0.02f, d - 0.02f}),
                        kDark);        // roof
    for (float sx : {-0.22f, 0.22f}) { // supply and return helium couplings (Aeroquip) on the roof
        mesh::appendColored(m, gfx::shapes::cylinder(0.022f, 0.07f, 16, true), kSteel,
                            mesh::translate({sx * w, y0 + cab + 0.02f + 0.035f, -0.25f * d}));
        mesh::appendColored(m, gfx::shapes::cylinder(0.028f, 0.012f, 16, true), kGold,
                            mesh::translate({sx * w, y0 + cab + 0.02f + 0.076f, -0.25f * d}));
    }
    // front control panel: pressure gauge, main switch, hour meter, status LEDs, nameplate
    const float zf = 0.5f * d + 0.001f;
    mesh::appendColored(m, mesh::box({0.0f, y0 + 0.72f * cab, zf}, {0.6f * w, 0.26f * cab, 0.002f}),
                        kDark);
    mesh::appendColored(m, gfx::shapes::cylinder(0.045f, 0.02f, 24, true), kDark,
                        alongZ({-0.18f * w, y0 + 0.72f * cab, zf}));
    mesh::appendColored(m, mesh::disc(0.038f, 0.0105f, 24, true), kFace,
                        alongZ({-0.18f * w, y0 + 0.72f * cab, zf}));
    mesh::appendColored(m, mesh::box({0.0f, 0.012f, 0.0f}, {0.002f, 0.028f, 0.0006f}), kDark,
                        spinZ({-0.18f * w, y0 + 0.72f * cab, zf + 0.0115f}, -35.0f));
    mesh::appendColored(
        m, mesh::box({0.05f * w, y0 + 0.72f * cab, zf + 0.006f}, {0.04f, 0.06f, 0.012f}),
        kRed); // main switch
    mesh::appendColored(
        m, mesh::box({0.20f * w, y0 + 0.76f * cab, zf + 0.002f}, {0.07f, 0.025f, 0.004f}),
        kFace); // hour meter
    mesh::appendColored(m, gfx::shapes::cylinder(0.005f, 0.004f, 10, true), kGreenLed,
                        alongZ({0.17f * w, y0 + 0.66f * cab, zf + 0.002f}));
    mesh::appendColored(m, gfx::shapes::cylinder(0.005f, 0.004f, 10, true), kGold,
                        alongZ({0.21f * w, y0 + 0.66f * cab, zf + 0.002f}));
    mesh::appendColored(m, mesh::box({0.0f, y0 + 0.30f * cab, zf}, {0.5f * w, 0.05f * cab, 0.003f}),
                        kFace);  // nameplate
    for (int k = 0; k < 10; ++k) // side ventilation grille (+x) over the heat exchanger
        mesh::appendColored(m,
                            mesh::box({0.5f * w + 0.001f,
                                       y0 + cab * (0.15f + 0.055f * static_cast<float>(k)), 0.0f},
                                      {0.002f, 0.012f, 0.7f * d}),
                            kDark);
    for (float dy : {-0.06f, 0.06f}) // cooling-water fittings on the rear
        mesh::appendColored(m, gfx::shapes::cylinder(0.012f, 0.05f, 12, true), kSteel,
                            alongZ({-0.3f * w, y0 + 0.25f * cab + dy, -0.5f * d - 0.025f}));
    return m;
}

MeshData labDoor() {
    MeshData m; // −X wall plane: the leaf's outer face at x = 0, the room at +x
    const float W = 0.90f, H = 2.10f, t = 0.045f;
    mesh::appendColored(m, mesh::box({0.5f * t, 0.5f * H, 0.0f}, {t, H, W}), kLeaf);
    for (float side : {-1.0f, 1.0f})
        mesh::appendColored(m,
                            mesh::box({0.05f, 0.5f * H + 0.02f, side * (0.5f * W + 0.04f)},
                                      {0.10f, H + 0.04f, 0.08f}),
                            kFrame); // jambs
    mesh::appendColored(m, mesh::box({0.05f, H + 0.04f, 0.0f}, {0.10f, 0.08f, W + 0.16f}),
                        kFrame); // head
    mesh::appendColored(m, mesh::box({0.5f * t, 1.55f, 0.05f}, {t + 0.004f, 0.60f, 0.30f}),
                        kFrame); // vision-panel frame
    mesh::appendColored(m, mesh::box({0.5f * t, 1.55f, 0.05f}, {t + 0.008f, 0.54f, 0.24f}), kGlass);
    mesh::appendColored(m, mesh::box({0.5f * t, 0.15f, 0.0f}, {t + 0.004f, 0.30f, W - 0.04f}),
                        kSteel); // kick plate
    mesh::appendColored(m, gfx::shapes::cylinder(0.011f, 0.07f, 12, true), kSteel,
                        alongX({t + 0.035f, 1.05f, 0.35f})); // lever handle rose + lever
    mesh::appendColored(m, mesh::box({t + 0.065f, 1.05f, 0.29f}, {0.014f, 0.014f, 0.13f}), kSteel);
    mesh::appendColored(m, mesh::box({t + 0.03f, 1.05f, 0.33f}, {0.02f, 0.07f, 0.03f}),
                        kDark); // lock escutcheon
    for (float y : {0.25f, 1.05f, 1.85f})
        mesh::appendColored(m, mesh::box({t, y, -0.5f * W + 0.01f}, {0.012f, 0.10f, 0.02f}),
                            kSteel); // hinges
    return m;
}

MeshData safetySign(std::string_view kind, float w, float h) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, 0.0f, 0.002f}, {w, h, 0.004f}), kFace);
    const float s = 0.42f * h, yc = 0.22f * h; // warning triangle (black border, yellow field)
    auto tri = [&](float scale, float z, const glm::vec4& colour) {
        std::vector<glm::vec2> p{{-0.577f * s * scale, 0.333f * s * scale},
                                 {0.577f * s * scale, 0.333f * s * scale},
                                 {0.0f, -0.667f * s * scale}};
        mesh::appendColored(m, mesh::prism(p, 0.0f, 0.002f), colour,
                            mesh::translate({0.0f, yc, z}) * mesh::alignY({0.0f, 0.0f, 1.0f}));
    };
    tri(1.0f, 0.004f, kBlack);
    tri(0.82f, 0.0045f, kYellow);
    const float zp = 0.0072f;
    if (kind == "cryogen") { // snowflake: three crossed bars
        for (int k = 0; k < 3; ++k)
            mesh::appendColored(m, mesh::box({0.0f, 0.0f, 0.0f}, {0.03f * s, 0.42f * s, 0.001f}),
                                kBlack,
                                spinZ({0.0f, yc + 0.02f * s, zp}, 60.0f * static_cast<float>(k)));
    } else if (kind == "magnet") { // horseshoe magnet: a U of three bars and two pole tips
        mesh::appendColored(
            m, mesh::box({0.0f, yc - 0.10f * s, zp}, {0.34f * s, 0.07f * s, 0.001f}), kBlack);
        for (float sx : {-0.135f, 0.135f}) {
            mesh::appendColored(
                m, mesh::box({sx * s, yc + 0.06f * s, zp}, {0.07f * s, 0.32f * s, 0.001f}), kBlack);
            mesh::appendColored(
                m, mesh::box({sx * s, yc + 0.24f * s, zp}, {0.07f * s, 0.06f * s, 0.001f}), kRed);
        }
    } else { // laser: starburst from a point
        for (int k = 0; k < 4; ++k)
            mesh::appendColored(m, mesh::box({0.0f, 0.0f, 0.0f}, {0.025f * s, 0.40f * s, 0.001f}),
                                kBlack, spinZ({0.0f, yc, zp}, 45.0f * static_cast<float>(k)));
        mesh::appendColored(m, gfx::shapes::cylinder(0.06f * s, 0.001f, 16, true), kBlack,
                            alongZ({0.0f, yc, zp}));
    }
    for (int line = 0; line < 3; ++line) // text lines of the sign board
        mesh::appendColored(
            m,
            mesh::box({0.0f, -0.16f * h - 0.11f * h * static_cast<float>(line), 0.0045f},
                      {(line == 0 ? 0.78f : 0.62f) * w, 0.045f * h, 0.001f}),
            line == 0 ? kBlack : kFrame);
    return m;
}

} // namespace qlab::lab::props
