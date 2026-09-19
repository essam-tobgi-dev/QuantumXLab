// Spec 17 §3.3 — the two 42U racks: cabinet with 19-inch rails, the unit list of the layout
// mounted flush on the front rails, and the rear cable loom from every unit up through the roof
// brush to the ceiling tray (rack detail pass; the gas-handling system is BuildGhs.cpp).
#include "Lab/Builder.hpp"
#include "Lab/MeshOps.hpp"
#include "Lab/PropMeshes.hpp"
#include "Lab/RackPanel.hpp"
#include <algorithm>
#include <format>

namespace qlab::lab {

namespace {
constexpr double kRackWidth_m = 0.6, kRackDepth_m = 0.9, kRackHeight_m = 2.0, kRackRoof_m = 0.02;
constexpr double kRailSetback_m =
    0.030; // front rails behind the cabinet's front edge (RackMeshes.cpp)
constexpr double kFaceplate_m = 0.003; // faceplate thickness (GenRack.cpp)
constexpr double kTrayDrop_m = 0.30;   // tray under the ceiling (BuildRoom.cpp)
const glm::vec4 kCable{0.12f, 0.14f, 0.30f, 1.0f};

// Faceplate relief of a unit: how far its features stand in front of the faceplate (GenRack.cpp).
double unitRelief_m(const ComponentDescriptor& d) {
    return parseRackPanel(GenParams::merged(d.geometry, core::Json::object())).handles ? 0.034
                                                                                       : 0.013;
}
} // namespace

Status SceneBuilder::buildRacks(ComponentId root) {
    std::map<std::string, int> instanceOf; // per descriptor: the $i index
    std::vector<std::size_t> outputLines;
    for (const auto& run : scene_.wiringRuns())
        if (run.kind == cryo::LineKind::ReadoutOut)
            outputLines.push_back(run.lineIndex);
    for (const auto& rack : layout_.racks)
        QXL_TRY(buildRack(root, rack, instanceOf, outputLines));
    return {};
}

Status SceneBuilder::buildRack(ComponentId root, const RackSpec& rack,
                               std::map<std::string, int>& instanceOf,
                               const std::vector<std::size_t>& outputLines) {
    const double H = kRackHeight_m, D = kRackDepth_m;
    ComponentId group =
        addGroup(root, std::format("rack_{}", rack.id), std::format("Rack {}", rack.id),
                 Group::Rack, Transform::at(rack.position_m + glm::dvec3(0.0, 0.5 * H, 0.0)));
    { // The cabinet carries the rack's descriptor, so clicking the frame or a rail selects it.
        const gfx::MeshData enclosure =
            props::rackEnclosure(static_cast<float>(kRackWidth_m), static_cast<float>(H),
                                 static_cast<float>(D), std::max(1, rack.heightU));
        NodeSpec spec;
        spec.descriptor = "rack_enclosure";
        spec.instance = std::format("rack_{}.enclosure", rack.id);
        spec.display = std::format("Rack {} enclosure", rack.id);
        spec.group = Group::Rack;
        spec.material = "vertex";
        spec.params.setToken("rack", rack.id);
        addProp(group, std::move(spec), enclosure);
    }
    // Units stack downwards from the top of the usable height, their faceplates flush against
    // the front rails (RackMeshes.cpp puts the rail face kRailSetback_m behind the front edge).
    const double railFace = 0.5 * D - kRailSetback_m;
    double slotTop = 0.5 * H - kRackRoof_m - 0.03;
    int slot = 0;
    std::vector<std::pair<double, double>>
        unitRears; // (y centre, z of the chassis rear) for the loom stubs
    for (const auto& [unitId, count] : rack.units) {
        const ComponentDescriptor* d = scene_.catalog().find(unitId);
        if (!d) {
            note(std::format("rack {}: unit '{}' has no component.json", rack.id, unitId));
            continue;
        }
        const int u = std::max(1, static_cast<int>(std::lround(d->geometryNumber("u", 1))));
        const double h = u * kRackUnit_m, relief = unitRelief_m(*d);
        const double depth = d->geometry.contains("depth_m")
                                 ? d->geometryNumber("depth_m", kRackUnitDepth_m)
                                 : kRackUnitDepth_m;
        for (int k = 0; k < std::max(1, count); ++k) {
            const int index = instanceOf[unitId]++;
            NodeSpec spec;
            spec.descriptor = unitId;
            spec.instance = count > 1 ? std::format("rack_{}.{}[{}]", rack.id, unitId, index)
                                      : std::format("rack_{}.{}", rack.id, unitId);
            spec.display = count > 1 ? std::format("{} {}", d->name, index + 1) : d->name;
            spec.group = Group::Rack;
            spec.material =
                "panel_flat"; // vertex colours, no powder-coat normal over the engraving
            // faceplate front = local z + depth/2 − relief; its rear face sits on the rail face
            const double zUnit = railFace + kFaceplate_m - 0.5 * depth + relief;
            spec.local = Transform::at(0.0, slotTop - 0.5 * h, zUnit);
            spec.assembly = Assembly::RackUnits;
            spec.assemblyIndex = slot++;
            spec.params.setIndex("i", index);
            if (d->instrument && !d->instrument->channels.empty())
                spec.params.setIndex("k", 0); // first channel
            if (unitId == "rt_amplifier" && index < static_cast<int>(outputLines.size()))
                spec.params.setIndex(
                    "line", static_cast<long long>(outputLines[static_cast<std::size_t>(index)]));
            QXL_TRY(addComponent(group, std::move(spec)));
            unitRears.emplace_back(slotTop - 0.5 * h, zUnit - 0.5 * depth);
            slotTop -= h;
        }
    }
    // Rear loom: a stub of four cables from each chassis rear to the vertical bundle dressed on
    // the cable-tie bar, which climbs through the roof brush to the ceiling tray (spec 17 §2).
    if (scene_.catalog().contains("cable_loom") && !unitRears.empty()) {
        gfx::MeshData loom;
        const float xBar = static_cast<float>(0.5 * kRackWidth_m - 0.09),
                    zBar = static_cast<float>(-0.5 * D + 0.08);
        const float brushZ = static_cast<float>(-0.5 * D + 0.12),
                    roof = static_cast<float>(0.5 * H);
        const float trayY = static_cast<float>(layout_.roomHeight_m - kTrayDrop_m - 0.5 * H - 0.03);
        for (const auto& [y, zRear] : unitRears)
            for (int k = 0; k < 4; ++k) {
                float dy = 0.006f * (static_cast<float>(k) - 1.5f);
                glm::vec3 a{0.10f - 0.03f * static_cast<float>(k), static_cast<float>(y) + dy,
                            static_cast<float>(zRear)};
                glm::vec3 b{xBar - 0.01f, static_cast<float>(y) + dy - 0.03f, zBar};
                mesh::appendColored(loom, gfx::shapes::tube({a, b}, 0.0028f, 6, false), kCable);
            }
        const float bottom = static_cast<float>(unitRears.back().first - 0.05);
        for (int k = 0; k < 12; ++k) { // the vertical bundle: 12 runs in a 3 × 4 pack
            glm::vec3 off{0.007f * static_cast<float>(k % 3) - 0.007f, 0.0f,
                          0.007f * static_cast<float>(k / 3) - 0.0105f};
            std::vector<glm::vec3> path{{xBar, bottom, zBar},
                                        {xBar, roof - 0.15f, zBar},
                                        {0.10f, roof - 0.02f, brushZ},
                                        {0.0f, roof + 0.25f, brushZ},
                                        {0.0f, trayY, 0.0f}};
            for (auto& p : path)
                p += off;
            mesh::appendColored(loom, gfx::shapes::tube(path, 0.0035f, 8, true), kCable);
        }
        NodeSpec spec;
        spec.descriptor = "cable_loom";
        spec.instance = std::format("rack_{}.loom", rack.id);
        spec.display = std::format("Rack {} cable loom", rack.id);
        spec.group = Group::Rack;
        spec.material = "cable_jacket";
        spec.params.setToken("rack", rack.id);
        addProp(group, std::move(spec), loom);
    }
    return {};
}

} // namespace qlab::lab
