#pragma once
// Internal — front-panel part meshes of the RackUnit generator (GenRackParts.cpp). Every part is
// built in a panel frame: +z points out of the faceplate, the part's mounting face is at z = 0,
// x to the right and y up when seen from the front. Sizes are the real ones (metres × unitScale).
#include "Lab/Generators.hpp"
#include "Lab/RackPanel.hpp"

namespace qlab::lab::rackparts {

using gfx::MeshData;

// Panel-mount connector: SMA bulkhead jack (hex nut, threaded barrel, PTFE dielectric, pin),
// N female (nickel body, dielectric, socket), BNC (nickel body with two bayonet lugs), IEC C13
// mains outlet (recessed black socket with three pin slots) and a D-sub 25 shell.
MeshData connector(ConnectorType type, const GenContext& c);
// Knurled control knob of radius r protruding `length`, with a white pointer line.
MeshData knob(float r, float length, const GenContext& c);
// Raised keypad key of w × h with a rounded-looking cap.
MeshData key(float w, float h, const glm::vec4& colour, const GenContext& c);
// 3 mm indicator LED (a coloured lens in a black bezel).
MeshData led(const glm::vec4& colour, const GenContext& c);
// Vertical chassis handle: rod of `length` on two standoffs, 25 mm proud of the panel.
MeshData handle(float length, const GenContext& c);
// Display: bezel frame proud of the panel around a dark glass recessed behind it, w × h outer.
MeshData screen(float w, float h, const GenContext& c);
// Model nameplate bar (light-grey engraved plate) of w × h.
MeshData nameplate(float w, float h, const GenContext& c);
// One vent groove of width w (dark slot cut into the faceplate).
MeshData ventGroove(float w, const GenContext& c);
// Rack-ear mounting slot (dark 6 × 10 mm slot in the flange).
MeshData earSlot(const GenContext& c);

inline const glm::vec4 kSmaGold{0.93f, 0.78f, 0.42f, 1.0f};
inline const glm::vec4 kNickel{0.70f, 0.70f, 0.68f, 1.0f};
inline const glm::vec4 kIecBody{0.09f, 0.09f, 0.10f, 1.0f};
inline const glm::vec4 kIecPin{0.86f, 0.86f, 0.84f, 1.0f};
inline const glm::vec4 kDsubShell{0.74f, 0.73f, 0.70f, 1.0f};
inline const glm::vec4 kPtfe{0.95f, 0.95f, 0.92f, 1.0f};
inline const glm::vec4 kDark{0.05f, 0.05f, 0.06f, 1.0f};
inline const glm::vec4 kKnobBody{0.24f, 0.24f, 0.26f, 1.0f};
inline const glm::vec4 kPointer{0.95f, 0.95f, 0.95f, 1.0f};
inline const glm::vec4 kKey{0.78f, 0.78f, 0.76f, 1.0f};
inline const glm::vec4 kGlass{0.03f, 0.05f, 0.07f, 1.0f};
inline const glm::vec4 kBezel{0.55f, 0.56f, 0.58f, 1.0f};
inline const glm::vec4 kBezelGlow{0.45f, 0.72f, 0.85f, 1.0f};
inline const glm::vec4 kTrace{0.20f, 0.65f, 0.75f, 1.0f};
inline const glm::vec4 kPlate{0.82f, 0.82f, 0.80f, 1.0f};
inline const glm::vec4 kHandle{0.62f, 0.62f, 0.64f, 1.0f};

} // namespace qlab::lab::rackparts
