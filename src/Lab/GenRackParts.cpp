// Spec 17 §6 (rack detail pass) — front-panel parts of the RackUnit generator: connectors in
// their real sizes (SMA 1/4-36 bulkhead, N 5/8-24, BNC 3/8, IEC C13, D-sub 25), knobs, keys,
// LEDs, handles, display bezel, nameplate, grooves and ear slots.
#include "Lab/MeshOps.hpp"
#include "Lab/RackParts.hpp"
#include <glm/gtc/constants.hpp>

namespace qlab::lab::rackparts {

namespace {
// Cylinder along +z with its base at z = 0.
glm::mat4 outZ(float z0, float length) { return mesh::translate({0.0f, 0.0f, z0 + 0.5f * length}) * mesh::alignY({0.0f, 0.0f, 1.0f}); }
void cylZ(MeshData& m, float r, float z0, float length, int seg, const glm::vec4& colour) {
    mesh::appendColored(m, gfx::shapes::cylinder(r, length, seg, true), colour, outZ(z0, length));
}
// Hex nut of `acrossFlats` from z0 to z0 + length, centred on x.
void hexZ(MeshData& m, float acrossFlats, float x, float z0, float length, const glm::vec4& colour) {
    float circum = 0.5f * acrossFlats / std::cos(glm::pi<float>() / 6.0f);
    mesh::appendColored(m, mesh::ngonPrism(6, circum, 0.0f, length), colour, mesh::translate({x, 0.0f, z0}) * mesh::alignY({0.0f, 0.0f, 1.0f}));
}
} // namespace

MeshData connector(ConnectorType type, const GenContext& c) {
    MeshData m;
    switch (type) {
    case ConnectorType::Sma: // hex nut 8 mm AF, barrel Ø 6.35, PTFE Ø 4.1, pin Ø 1.27
        hexZ(m, c.F(0.008), 0.0f, 0.0f, c.F(0.0025), kSmaGold);
        cylZ(m, c.F(0.00318), c.F(0.0025), c.F(0.0045), 16, kSmaGold);
        cylZ(m, c.F(0.00205), c.F(0.0065), c.F(0.0008), 12, kPtfe);
        cylZ(m, c.F(0.00064), c.F(0.0065), c.F(0.0018), 8, kDark);
        break;
    case ConnectorType::N: // female N: Ø 15.9 body, dielectric Ø 7, socket Ø 3
        cylZ(m, c.F(0.0080), 0.0f, c.F(0.010), 24, kNickel);
        cylZ(m, c.F(0.0035), c.F(0.0095), c.F(0.0010), 16, kPtfe);
        cylZ(m, c.F(0.0015), c.F(0.0098), c.F(0.0010), 8, kDark);
        break;
    case ConnectorType::Bnc: // Ø 11 body, two bayonet lugs, dielectric Ø 5
        cylZ(m, c.F(0.0055), 0.0f, c.F(0.009), 20, kNickel);
        for (float side : {-1.0f, 1.0f})
            mesh::appendColored(m, mesh::box({side * c.F(0.0060), 0.0f, c.F(0.0065)}, {c.F(0.0018), c.F(0.0018), c.F(0.0020)}), kNickel);
        cylZ(m, c.F(0.0025), c.F(0.0085), c.F(0.0010), 12, kPtfe);
        cylZ(m, c.F(0.0007), c.F(0.0088), c.F(0.0010), 8, kDark);
        break;
    case ConnectorType::Iec: { // C13 outlet: 27 × 20 mm black recess with three pin slots
        mesh::appendColored(m, mesh::box({0.0f, 0.0f, c.F(0.0015)}, {c.F(0.027), c.F(0.020), c.F(0.003)}), kIecBody);
        for (float x : {-c.F(0.007), 0.0f, c.F(0.007)})
            mesh::appendColored(m, mesh::box({x, x == 0.0f ? c.F(0.004) : -c.F(0.002), c.F(0.0032)}, {c.F(0.002), c.F(0.005), c.F(0.0006)}), kIecPin);
        break;
    }
    case ConnectorType::Dsub: { // DB-25 shell: nickel D-shell 41 × 12 mm with two 4-40 jack posts
        std::vector<glm::vec2> shell{{-c.F(0.0205), -c.F(0.006)}, {c.F(0.0205), -c.F(0.006)}, {c.F(0.0175), c.F(0.006)}, {-c.F(0.0175), c.F(0.006)}};
        mesh::appendColored(m, mesh::prism(shell, 0.0f, c.F(0.006)), kDsubShell, mesh::alignY({0.0f, 0.0f, 1.0f}));
        mesh::appendColored(m, mesh::box({0.0f, 0.0f, c.F(0.0055)}, {c.F(0.033), c.F(0.008), c.F(0.0012)}), kDark); // insulator insert
        for (float x : {-c.F(0.0245), c.F(0.0245)}) hexZ(m, c.F(0.005), x, 0.0f, c.F(0.005), kDsubShell);
        break;
    }
    }
    return m;
}

MeshData knob(float r, float length, const GenContext& c) {
    MeshData m;
    mesh::appendColored(m, mesh::ngonPrism(24, r, 0.0f, 0.7f * length), kKnobBody, mesh::alignY({0.0f, 0.0f, 1.0f}));
    mesh::appendColored(m, mesh::ngonPrism(24, 0.85f * r, 0.7f * length, length), kKnobBody, mesh::alignY({0.0f, 0.0f, 1.0f}));
    mesh::appendColored(m, mesh::box({0.0f, 0.5f * r, length + c.F(0.0003)}, {c.F(0.0012), 0.8f * r, c.F(0.0006)}), kPointer);
    return m;
}

MeshData key(float w, float h, const glm::vec4& colour, const GenContext& c) {
    MeshData m;
    float rise = c.F(0.0025);
    mesh::appendColored(m, mesh::box({0.0f, 0.0f, 0.5f * rise}, {w, h, rise}), colour);
    mesh::appendColored(m, mesh::box({0.0f, 0.0f, rise + c.F(0.0002)}, {0.75f * w, 0.7f * h, c.F(0.0004)}), colour * 1.06f);
    return m;
}

MeshData led(const glm::vec4& colour, const GenContext& c) {
    MeshData m;
    cylZ(m, c.F(0.0025), 0.0f, c.F(0.0010), 12, kDark);
    cylZ(m, c.F(0.0015), c.F(0.0010), c.F(0.0012), 12, colour);
    return m;
}

MeshData handle(float length, const GenContext& c) {
    MeshData m;
    float stand = c.F(0.022), rod = c.F(0.0055);
    for (float y : {-0.5f * length + c.F(0.008), 0.5f * length - c.F(0.008)})
        mesh::appendColored(m, mesh::box({0.0f, y, 0.5f * stand}, {c.F(0.016), c.F(0.016), stand}), kHandle);
    mesh::appendColored(m, gfx::shapes::cylinder(rod, length, 12, true), kHandle, mesh::translate({0.0f, 0.0f, stand + rod}));
    return m;
}

MeshData screen(float w, float h, const GenContext& c) {
    MeshData m;
    float b = c.F(0.004), lip = c.F(0.0015), recess = c.F(0.0015);
    for (float side : {-1.0f, 1.0f}) { // bezel frame: four bars proud of the panel
        mesh::appendColored(m, mesh::box({side * (0.5f * w - 0.5f * b), 0.0f, 0.5f * lip}, {b, h, lip}), kBezel);
        mesh::appendColored(m, mesh::box({0.0f, side * (0.5f * h - 0.5f * b), 0.5f * lip}, {w - 2.0f * b, b, lip}), kBezel);
    }
    float gw = w - 2.0f * b, gh = h - 2.0f * b;
    // glass set back behind the bezel, its face 0.4 mm proud of the faceplate so it never z-fights it
    mesh::appendColored(m, mesh::box({0.0f, 0.0f, 0.5f * c.F(0.0004) - 0.5f * recess}, {gw, gh, recess + c.F(0.0004)}), kGlass);
    // the lit inner edge of the bezel (the frame glow) and the idle trace / graticule baseline
    float e = c.F(0.0008);
    for (float side : {-1.0f, 1.0f}) {
        mesh::appendColored(m, mesh::box({side * (0.5f * gw - 0.5f * e), 0.0f, e}, {e, gh, e}), kBezelGlow);
        mesh::appendColored(m, mesh::box({0.0f, side * (0.5f * gh - 0.5f * e), e}, {gw, e, e}), kBezelGlow);
    }
    mesh::appendColored(m, mesh::box({0.0f, -0.15f * gh, -0.2f * recess}, {0.86f * gw, c.F(0.0007), c.F(0.0004)}), kTrace);
    mesh::appendColored(m, mesh::box({-0.5f * gw + 0.06f * gw, 0.30f * gh, -0.2f * recess}, {0.1f * gw, c.F(0.0025), c.F(0.0004)}), kTrace);
    return m;
}

MeshData nameplate(float w, float h, const GenContext& c) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, 0.0f, 0.5f * c.F(0.0008)}, {w, h, c.F(0.0008)}), kPlate);
    mesh::appendColored(m, mesh::box({0.0f, 0.0f, c.F(0.0009)}, {0.9f * w, 0.28f * h, c.F(0.0002)}), kDark); // engraving line
    return m;
}

MeshData ventGroove(float w, const GenContext& c) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, 0.0f, c.F(0.0002)}, {w, c.F(0.0022), c.F(0.0018)}), kDark); // reads as a groove; sits 0.2 mm proud
    return m;
}

MeshData earSlot(const GenContext& c) {
    MeshData m;
    mesh::appendColored(m, mesh::box({0.0f, 0.0f, c.F(0.0003)}, {c.F(0.0065), c.F(0.0100), c.F(0.0006)}), kDark);
    return m;
}

} // namespace qlab::lab::rackparts

namespace qlab::lab {

glm::vec4 connectorBodyColour(ConnectorType t) {
    switch (t) {
    case ConnectorType::Sma: return rackparts::kSmaGold;
    case ConnectorType::N: return rackparts::kNickel;
    case ConnectorType::Bnc: return rackparts::kNickel;
    case ConnectorType::Iec: return rackparts::kIecBody;
    case ConnectorType::Dsub: return rackparts::kDsubShell;
    }
    return rackparts::kSmaGold;
}

std::size_t connectorVertexCount(ConnectorType t) {
    const gfx::MeshData one = rackparts::connector(t, GenContext{Detail::Full, 1.0});
    const glm::vec4 body = connectorBodyColour(t);
    std::size_t n = 0;
    for (const auto& v : one.vertices)
        if (v.color == body) ++n;
    return n;
}

} // namespace qlab::lab
