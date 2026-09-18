// Spec 17 §6 — RackUnit(u, faceplate features).
#include "Lab/Generators.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::lab::gen {

using gfx::MeshData;
namespace {
const glm::vec4 kFace{0.16f, 0.17f, 0.19f, 1.0f};
const glm::vec4 kScreen{0.02f, 0.06f, 0.10f, 1.0f};
const glm::vec4 kKnob{0.30f, 0.30f, 0.32f, 1.0f};
const glm::vec4 kConnector{0.95f, 0.80f, 0.45f, 1.0f};
const glm::vec4 kLed{0.2f, 1.0f, 0.35f, 1.0f};
const glm::vec4 kChassis{0.10f, 0.10f, 0.11f, 1.0f};
} // namespace

// 19-inch rack unit: faceplate 482.6 mm wide, u × 44.45 mm high; chassis behind it. Front
// features by `front` kind, all inside a 15 mm relief in front of the faceplate.
Result<MeshData> rackUnit(const GenParams& p, const GenContext& c) {
    int u = std::max(1, p.integer("u", 1));
    float W = c.F(kRackPanelWidth_m), H = c.F(kRackUnit_m * u), D = c.F(p.length("depth", kRackUnitDepth_m));
    MeshData m;
    if (c.detail == Detail::Simple) {
        mesh::appendColored(m, mesh::box({0, 0, 0}, {W, H, D}), kFace);
        return m;
    }
    float relief = c.F(0.015), plate = c.F(0.004);
    float zFace = 0.5f * D - relief; // faceplate front surface
    mesh::appendColored(m, mesh::box({0, 0, -0.5f * relief - 0.5f * plate}, {c.F(0.44), 0.96f * H, D - relief - plate}), kChassis);
    mesh::appendColored(m, mesh::box({0, 0, zFace - 0.5f * plate}, {W, H, plate}), kFace);
    std::string kind = p.string("front", "controller");
    auto screen = [&](float x, float wFrac, float hFrac) {
        mesh::appendColored(m, mesh::box({x, 0.05f * H, zFace + 0.0015f}, {wFrac * W, hFrac * H, c.F(0.003)}), kScreen);
    };
    auto knob = [&](float x, float y, float r) {
        mesh::appendColored(m, gfx::shapes::cylinder(r, 0.8f * relief, 16, true), kKnob,
                            mesh::translate({x, y, zFace + 0.4f * relief}) * mesh::alignY({0, 0, 1}));
    };
    auto connector = [&](float x, float y) {
        mesh::appendColored(m, gfx::shapes::cylinder(c.F(0.0045), relief, 12, true), kConnector,
                            mesh::translate({x, y, zFace + 0.5f * relief}) * mesh::alignY({0, 0, 1}));
    };
    auto led = [&](float x, float y) {
        mesh::appendColored(m, mesh::box({x, y, zFace + c.F(0.001)}, glm::vec3(c.F(0.004), c.F(0.004), c.F(0.002))), kLed);
    };
    float row = u > 1 ? -0.25f * H : 0.0f;
    if (kind == "vna" || kind == "spectrum_analyzer" || kind == "oscilloscope" || kind == "sg_mw" || kind == "dc_source") {
        screen(-0.18f * W, 0.42f, 0.7f);
        knob(0.12f * W, 0.1f * H, std::min(0.18f * H, c.F(0.018)));
        int ports = kind == "vna" ? 2 : (kind == "oscilloscope" ? 4 : (kind == "dc_source" ? 4 : 1));
        for (int i = 0; i < ports; ++i) connector(0.25f * W + static_cast<float>(i) * c.F(0.03), row);
    } else if (kind == "awg" || kind == "digitizer" || kind == "patch" || kind == "iq_mixer" || kind == "amp") {
        int cols = kind == "awg" ? 12 : (kind == "patch" ? 16 : (kind == "digitizer" ? 4 : (kind == "iq_mixer" ? 3 : 2)));
        int rows = (kind == "awg" && u >= 4) ? 3 : (kind == "patch" ? 2 : 1);
        for (int r = 0; r < rows; ++r)
            for (int k = 0; k < cols; ++k)
                connector(-0.4f * W + 0.8f * W * (static_cast<float>(k) + 0.5f) / static_cast<float>(cols),
                          rows == 1 ? row : H * (0.3f - 0.3f * static_cast<float>(r)));
        led(0.46f * W, 0.3f * H);
    } else if (kind == "pdu") {
        for (int k = 0; k < 8; ++k)
            mesh::appendColored(m, mesh::box({-0.35f * W + 0.09f * W * static_cast<float>(k), 0, zFace + c.F(0.002)},
                                             {c.F(0.025), 0.6f * H, c.F(0.004)}), kScreen);
        led(0.45f * W, 0.0f);
    } else { // controller: status LEDs and a BNC row (reference, clock, trigger units)
        for (int k = 0; k < 4; ++k) led(-0.42f * W + c.F(0.012) * static_cast<float>(k), 0.2f * H);
        for (int k = 0; k < 6; ++k) connector(0.05f * W + c.F(0.03) * static_cast<float>(k), row);
    }
    for (float x : {-0.47f * W, 0.47f * W}) // rack ears with mounting holes
        mesh::appendColored(m, mesh::box({x, 0, zFace + c.F(0.0005)}, {c.F(0.006), 0.5f * H, c.F(0.001)}), kScreen);
    return m;
}

} // namespace qlab::lab::gen
