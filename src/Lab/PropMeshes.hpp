#pragma once
// Internal — authored composite meshes of the room's props and plant (spec 17 §2, rack detail
// pass). Each is built in its own frame (metres, +Y up, +Z the front face) and handed to
// SceneBuilder::addProp so the prop keeps its descriptor and stays inspectable.
#include "Graphics/Mesh.hpp"
#include <string_view>

namespace qlab::lab::props {

using gfx::MeshData;

// ---- furniture (PropFurniture.cpp)
// Desk of w × d with its top surface at y = 0: two monitors on gas-spring arms, keyboard, mouse.
MeshData workstation(float w, float d);
// Task chair, floor at y = 0, seat facing +z.
MeshData officeChair();
// Optical-breadboard bench of w × d, top surface at y = 0, legs down to −h; `holes` draws the
// M6 grid at 25 mm (full detail) — the simple level omits it.
MeshData bench(float w, float h, float d, bool holes);
// Stereo zoom microscope on a boom stand, base at y = 0 inside w × h × d.
MeshData microscope(float w, float h, float d);
// Wedge wire bonder outline (base, work stage, column, bond head, microscope), base at y = 0.
MeshData wireBonder(float w, float h, float d);
// Gel-pack sample box with a lid, base at y = 0.
MeshData sampleBox(float w, float h, float d);

// ---- plant (PropPlant.cpp)
// 100 L helium storage dewar of body radius r and body height h: castor base, body with rolled
// seams, neck with flange, transfer valve, pressure gauge and a handling ring; floor at y = 0.
MeshData heDewar(float r, float h);
// Pulse-tube compressor package of w × h × d: cabinet on castors, control panel with a gauge and
// switches, side grille, two helium flex-line couplings on top; floor at y = 0.
MeshData ptCompressor(float w, float h, float d);
// Lab door leaf 0.9 × 2.1 m with frame, vision panel, handle and kick plate. Built in the −X
// wall's plane: the leaf spans z ∈ [−0.45, 0.45] around the origin, y from 0.
MeshData labDoor();
// Wall safety sign of w × h facing +z: a yellow warning triangle with the hazard pictogram
// (`kind`: "cryogen", "magnet", "laser") over a white text board with engraved lines.
MeshData safetySign(std::string_view kind, float w, float h);

// ---- gas handling plant (GhsMeshes.cpp)
MeshData scrollPump(float w, float h,
                    float d); // motor, scroll head, fan cowl, feet; floor at y = 0
MeshData he3Compressor(float w, float h, float d); // hermetic compressor on a skid; floor at y = 0
MeshData dumpTank(float r,
                  float h); // vertical tank with dished ends, valve, gauge, stand; floor at y = 0
MeshData turboPump(float r,
                   float h); // turbo with its inlet flange up and controller box; base at y = 0
MeshData ln2Trap(float r, float h);    // LN2 dewar with the trap coil and lid; floor at y = 0
MeshData valveLabel(float w, float h); // engraved label plate facing +z

// ---- rack (RackMeshes.cpp)
// 19-inch cabinet of W × H × D centred on the origin: corner posts, side and rear panels, plinth,
// top with the cable brush strip, front and rear mounting rails with the EIA hole pattern.
MeshData rackEnclosure(float W, float H, float D, int heightU);

} // namespace qlab::lab::props
