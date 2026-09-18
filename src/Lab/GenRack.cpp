// Spec 17 §6 — RackUnit(u, front_panel): a 19-inch unit (faceplate 482.6 mm × u · 44.45 mm,
// chassis behind it) whose front carries the real features of its instrument class from the
// descriptor's `front_panel` block (Lab/RackPanel.hpp): display with bezel, keypads, knobs,
// LEDs, connectors in their real layout, card slots, chassis handles, vent grooves, rack-ear
// slots and a nameplate. Everything stays inside the unit's envelope so the AABB is the unit.
#include "Lab/Generators.hpp"
#include "Lab/MeshOps.hpp"
#include "Lab/RackPanel.hpp"
#include "Lab/RackParts.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::lab::gen {

using gfx::MeshData;
namespace {
const glm::vec4 kChassis{0.10f, 0.10f, 0.11f, 1.0f};
const glm::vec4 kSlotBay{0.06f, 0.06f, 0.07f, 1.0f};
const glm::vec4 kCard{0.80f, 0.80f, 0.79f, 1.0f};
const glm::vec4 kEjector{0.16f, 0.18f, 0.22f, 1.0f};
constexpr int kSlotsPerU = 3; // EIA-310: three mounting holes per rack unit
} // namespace

Result<MeshData> rackUnit(const GenParams& p, const GenContext& c) {
    const RackPanelSpec s = parseRackPanel(p);
    const float W = c.F(kRackPanelWidth_m), H = c.F(kRackUnit_m * s.u), D = c.F(p.length("depth", kRackUnitDepth_m));
    const float relief = c.F(s.handles ? 0.034 : 0.013), plate = c.F(0.003);
    const float zFace = 0.5f * D - relief; // faceplate front surface
    MeshData m;
    // The unit's LOD levels: `simple` keeps the faceplate colour and the screen so the units stay
    // distinguishable from across the room; `full` adds every panel feature.
    // The chassis starts BEHIND the faceplate (its front face at zFace would be coplanar with the
    // plate's and z-fight through it as dark hatched patches on every light-coloured unit).
    mesh::appendColored(m, mesh::box({0, 0, zFace - plate - 0.5f * (D - relief - plate)}, {c.F(0.44), 0.94f * H, D - relief - plate}), kChassis);
    mesh::appendColored(m, mesh::box({0, 0, zFace - 0.5f * plate}, {W, H - c.F(0.0008), plate}), s.faceColour);
    auto fx = [&](double f) { return static_cast<float>(f) * W; };
    auto fy = [&](double f) { return static_cast<float>(f) * H; };
    auto place = [&](MeshData part, float x, float y, float z = 0.0f) { mesh::append(m, part, mesh::translate({x, y, zFace + z})); };
    if (s.screen) {
        if (c.detail == Detail::Simple) {
            mesh::appendColored(m, mesh::box({fx(s.screen->x), fy(s.screen->y), zFace + c.F(0.0005)},
                                             {fx(s.screen->w), fy(s.screen->h), c.F(0.001)}), rackparts::kGlass);
        } else {
            place(rackparts::screen(fx(s.screen->w), fy(s.screen->h), c), fx(s.screen->x), fy(s.screen->y));
        }
    }
    if (s.slots && c.detail == Detail::Simple)
        mesh::appendColored(m, mesh::box({fx(0.5 * (s.slots->x0 + s.slots->x1)), 0.0f, zFace + c.F(0.0005)},
                                         {fx(s.slots->x1 - s.slots->x0), 0.82f * H, c.F(0.001)}), kSlotBay);
    if (c.detail == Detail::Simple) return m;

    for (const auto& k : s.keypads) { // 9 × 7 mm keys on a 12 × 10 mm pitch, centred on (x, y)
        float pitchX = c.F(0.012), pitchY = c.F(0.010);
        for (int r = 0; r < k.rows; ++r)
            for (int col = 0; col < k.cols; ++col) {
                float x = fx(k.x) + pitchX * (static_cast<float>(col) - 0.5f * static_cast<float>(k.cols - 1));
                float y = fy(k.y) + pitchY * (0.5f * static_cast<float>(k.rows - 1) - static_cast<float>(r));
                bool power = r == k.rows - 1 && col == 0 && k.rows * k.cols >= 4;
                place(rackparts::key(c.F(0.009), c.F(0.007), power ? ledColour("green") * 0.7f : rackparts::kKey, c), x, y);
            }
    }
    for (const auto& k : s.knobs) place(rackparts::knob(c.F(k.r_m), c.F(0.012), c), fx(k.x), fy(k.y));
    for (const auto& l : s.leds) place(rackparts::led(l.colour, c), fx(l.x), fy(l.y));
    for (const auto& cn : s.connectors) place(rackparts::connector(cn.type, c), fx(cn.x), fy(cn.y));
    if (s.slots) { // card cage: a dark bay with `count` plug-in cards, each an SMA column + LED + ejector
        const float bayW = fx(s.slots->x1 - s.slots->x0), bayX = fx(0.5 * (s.slots->x0 + s.slots->x1)), bayH = 0.84f * H;
        const float recess = c.F(0.006);
        mesh::appendColored(m, mesh::box({bayX, 0.0f, zFace - 0.5f * recess + c.F(0.0002)}, {bayW, bayH, recess}), kSlotBay);
        const float pitch = bayW / static_cast<float>(s.slots->count), cardW = 0.78f * pitch, cardH = 0.96f * bayH;
        const float smaPitch = std::min(c.F(0.014), (cardH - c.F(0.030)) / static_cast<float>(std::max(1, s.slots->smaPerCard)));
        for (int i = 0; i < s.slots->count; ++i) {
            float x = bayX - 0.5f * bayW + pitch * (static_cast<float>(i) + 0.5f);
            mesh::appendColored(m, mesh::box({x, 0.0f, zFace - recess + 0.5f * plate}, {cardW, cardH, plate}), kCard);
            float zCard = -recess + plate;
            place(rackparts::led(ledColour(i % 3 == 2 ? "amber" : "green"), c), x, 0.5f * cardH - c.F(0.006), zCard);
            for (int k = 0; k < s.slots->smaPerCard; ++k)
                place(rackparts::connector(ConnectorType::Sma, c), x, 0.5f * cardH - c.F(0.020) - smaPitch * static_cast<float>(k), zCard);
            mesh::appendColored(m, mesh::box({x, -0.5f * cardH + c.F(0.006), zFace - recess + plate + c.F(0.002)},
                                             {0.6f * cardW, c.F(0.006), c.F(0.004)}), kEjector);
        }
    }
    if (s.vents) { // horizontal grooves (ventilation slots) in the faceplate
        float x = fx(s.vents->x), w = fx(s.vents->w), h = fy(s.vents->h);
        for (int r = 0; r < s.vents->rows; ++r) {
            float y = fy(s.vents->y) + h * (0.5f - (static_cast<float>(r) + 0.5f) / static_cast<float>(std::max(1, s.vents->rows)));
            place(rackparts::ventGroove(w, c), x, y);
        }
    }
    if (s.handles && s.u >= 2)
        for (float side : {-1.0f, 1.0f}) place(rackparts::handle(0.62f * H, c), side * (0.5f * W - c.F(0.030)), 0.0f);
    if (s.nameplate) {
        glm::dvec3 np = rackNameplateLocal(s.u, p.length("depth", kRackUnitDepth_m));
        place(rackparts::nameplate(c.F(0.055), c.F(0.007), c), c.F(np.x), c.F(np.y));
    }
    // rack ears: 15.9 mm flanges with three mounting slots per U on each side (EIA-310)
    for (float side : {-1.0f, 1.0f})
        for (int u = 0; u < s.u; ++u)
            for (int k = 0; k < kSlotsPerU; ++k) {
                float yc = 0.5f * H - c.F(kRackUnit_m) * (static_cast<float>(u) + 0.5f);
                float y = yc + c.F(0.015875) * (1.0f - static_cast<float>(k));
                place(rackparts::earSlot(c), side * (0.5f * W - c.F(0.0079)), y);
            }
    return m;
}

} // namespace qlab::lab::gen
