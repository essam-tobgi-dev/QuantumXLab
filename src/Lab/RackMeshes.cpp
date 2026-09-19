// Spec 17 §3.3 (rack detail pass) — the 19-inch cabinet: welded corner posts, side and rear
// panels, plinth, roof with the cable brush strip, and front / rear mounting rails carrying the
// EIA-310 hole pattern (three square holes per U) that the units' rack ears bolt to.
#include "Lab/Generators.hpp"
#include "Lab/MeshOps.hpp"
#include "Lab/PropMeshes.hpp"

namespace qlab::lab::props {

namespace {
const glm::vec4 kPanel{0.07f, 0.07f, 0.08f, 1.0f}; // powder-coated black
const glm::vec4 kRail{0.68f, 0.68f, 0.66f, 1.0f};  // zinc-plated steel
const glm::vec4 kHole{0.02f, 0.02f, 0.02f, 1.0f};
const glm::vec4 kBrush{0.16f, 0.16f, 0.17f, 1.0f};
const glm::vec4 kBristle{0.30f, 0.30f, 0.32f, 1.0f};
} // namespace

MeshData rackEnclosure(float W, float H, float D, int heightU) {
    MeshData m;
    const float plinth = 0.10f, roof = 0.02f, post = 0.04f;
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f})
            mesh::appendColored(
                m,
                mesh::box({sx * (0.5f * W - 0.5f * post), 0.0f, sz * (0.5f * D - 0.5f * post)},
                          {post, H, post}),
                kPanel);
    for (float sx : {-1.0f, 1.0f}) // side panels with a shallow louvre band near the bottom
        mesh::appendColored(m,
                            mesh::box({sx * (0.5f * W - 0.005f), 0.0f, 0.0f},
                                      {0.01f, H - 2.0f * post, D - 2.0f * post}),
                            kPanel);
    mesh::appendColored(m, mesh::box({0.0f, -0.5f * H + 0.5f * plinth, 0.0f}, {W, plinth, D}),
                        kPanel);
    mesh::appendColored(m, mesh::box({0.0f, 0.5f * H - 0.5f * roof, 0.0f}, {W, roof, D}), kPanel);
    mesh::appendColored(
        m, mesh::box({0.0f, 0.0f, -0.5f * D + 0.005f}, {W - 2.0f * post, H - 2.0f * post, 0.01f}),
        kPanel);
    // Roof cable entry: a brush strip in a 300 × 60 mm cut-out at the rear of the roof, the
    // bristles meeting in the middle so the cable loom passes through and the airflow does not.
    const float brushZ = -0.5f * D + 0.12f;
    mesh::appendColored(m, mesh::box({0.0f, 0.5f * H + 0.004f, brushZ}, {0.32f, 0.008f, 0.08f}),
                        kBrush);
    for (float side : {-1.0f, 1.0f})
        mesh::appendColored(
            m,
            mesh::box({0.0f, 0.5f * H + 0.010f, brushZ + side * 0.016f}, {0.30f, 0.004f, 0.028f}),
            kBristle);
    // Mounting rails: 19-inch (482.6 mm) between the ears; the rail centre lines sit under the
    // ears' slot rows at ±(241.3 − 7.9) mm. Front rails 30 mm behind the front edge, rear rails
    // 100 mm ahead of the rear panel, both with the EIA hole pattern on the front rails.
    const float railX = 0.5f * kRackPanelWidth_m - 0.0079f, railH = H - plinth - roof;
    const float railY = 0.5f * (plinth - roof), zFront = 0.5f * D - 0.030f,
                zRear = -0.5f * D + 0.10f;
    for (float side : {-1.0f, 1.0f})
        for (float z : {zFront, zRear}) {
            mesh::appendColored(
                m, mesh::box({side * railX, railY, z - 0.002f}, {0.022f, railH, 0.004f}), kRail);
            mesh::appendColored(m,
                                mesh::box({side * (railX + 0.011f * side), railY, z - 0.014f},
                                          {0.004f, railH, 0.028f}),
                                kRail); // L-flange
        }
    // Hole pattern: 3 per U, 15.875 mm apart, from the top of the usable height down.
    const float usableTop = 0.5f * H - roof - 0.03f;
    for (float side : {-1.0f, 1.0f})
        for (int u = 0; u < heightU; ++u) {
            float yc = usableTop - static_cast<float>(kRackUnit_m) * (static_cast<float>(u) + 0.5f);
            if (yc - 0.02f < -0.5f * H + plinth)
                break;
            for (int k = 0; k < 3; ++k) {
                float y = yc + 0.015875f * (1.0f - static_cast<float>(k));
                mesh::appendColored(
                    m, mesh::box({side * railX, y, zFront + 0.0002f}, {0.0095f, 0.0095f, 0.0006f}),
                    kHole);
            }
        }
    // Vertical cable-tie bar behind the rear rails, where the looms are dressed.
    mesh::appendColored(
        m, mesh::box({0.5f * W - 0.07f, railY, -0.5f * D + 0.05f}, {0.02f, railH, 0.02f}), kRail);
    return m;
}

} // namespace qlab::lab::props
