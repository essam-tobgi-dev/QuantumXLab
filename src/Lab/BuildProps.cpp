// Spec 17 §2 — the layout's props (rack detail pass): the workstation with its monitors, keyboard,
// mouse and chair; the optical-breadboard bench with the microscope, wire bonder, sample box and
// the RF power meter; the 100 L helium dewars; the pulse-tube compressor outside the −X wall.
// Every prop keeps its descriptor (authored composite via addProp), so it is inspectable.
#include "Lab/Builder.hpp"
#include "Lab/MeshOps.hpp"
#include "Lab/PropMeshes.hpp"
#include <algorithm>
#include <format>

namespace qlab::lab {

namespace {
// Bench-top instruments placed on the breadboard (bench-top frame, y = 0 is the board surface).
struct BenchItem {
    const char* id;
    glm::dvec3 at;
    double yaw_deg;
};
const BenchItem kBenchItems[]{{"microscope", {-0.45, 0.0, 0.05}, 0.0},
                              {"wire_bonder", {0.30, 0.0, 0.0}, 0.0},
                              {"sample_box", {-0.05, 0.0, 0.25}, 15.0},
                              {"power_meter", {-0.05, 0.0, -0.22}, 0.0}};
} // namespace

Status SceneBuilder::buildProps(ComponentId root) {
    ComponentId props = addGroup(root, "props", "Props", Group::Room);
    for (const auto& prop : layout_.props) {
        const ComponentDescriptor* d = scene_.catalog().find(prop.id);
        for (int k = 0; k < std::max(1, prop.count); ++k) {
            NodeSpec spec;
            spec.descriptor = prop.id;
            spec.instance = prop.count > 1 ? std::format("{}[{}]", prop.id, k) : prop.id;
            spec.group = Group::Room;
            spec.material = "vertex";
            gfx::MeshData mesh, simple;
            glm::dvec3 at = prop.position_m;
            double yaw = 0.0;
            if (prop.id == "workstation") { // desk at 0.75 m with two monitors on arms (spec 17 §2)
                const auto w = static_cast<float>(d ? d->geometryNumber("w_m", 1.6) : 1.6),
                           depth = static_cast<float>(d ? d->geometryNumber("d_m", 0.8) : 0.8);
                mesh = props::workstation(w, depth);
                at.y +=
                    0.75; // monitors along the desk's −z edge, the chair on the +z side facing them
                addScenery(props, "office_chair", "Task chair", Group::Room,
                           Transform::rotated(prop.position_m + glm::dvec3(0.0, 0.0, 0.75),
                                              {0.0, 1.0, 0.0}, 180.0),
                           props::officeChair(), "vertex");
            } else if (prop.id == "bench") { // optical-breadboard assembly bench, 0.9 m high
                const auto w = static_cast<float>(d ? d->geometryNumber("w_m", 1.8) : 1.8),
                           h = static_cast<float>(d ? d->geometryNumber("h_m", 0.9) : 0.9);
                const auto depth = static_cast<float>(d ? d->geometryNumber("d_m", 0.8) : 0.8);
                mesh = props::bench(w, h, depth, true);
                simple = props::bench(w, h, depth, false);
                spec.material = "aluminium";
                at.y += static_cast<double>(h);
            } else if (prop.id == "he_dewar") { // 100 L storage dewars along the −X wall
                const auto r = static_cast<float>(d ? d->geometryNumber("r_m", 0.25) : 0.25),
                           h = static_cast<float>(d ? d->geometryNumber("h_m", 1.3) : 1.3);
                mesh = props::heDewar(r, h);
                spec.material = "stainless";
                at.x +=
                    0.55 * k; // the pair stands side by side by the wall, behind the GHS bookmark's
                at.z += 0.5;  // eye and clear of the door's approach
                yaw = 30.0;   // valves and gauge turned toward the room
            } else if (prop.id == "optical_table") {
                spec.material = "black_anodized";
                auto size = prop.size_m.value_or(glm::dvec2(2.4, 1.2));
                mesh::append(mesh, mesh::box({0, 0, 0}, {static_cast<float>(size.x), 0.2f,
                                                         static_cast<float>(size.y)}));
                at.y += 0.8;
            } else {
                spec.material = "plastic_grey";
                mesh::append(mesh, mesh::box({0, 0, 0}, {0.5f, 0.5f, 0.5f}));
                at.y += 0.25;
            }
            spec.local = Transform::rotated(at, {0.0, 1.0, 0.0}, yaw);
            if (simple.triangleCount() > 0)
                spec.authoredSimple = &simple;
            const std::string propId = prop.id;
            const ComponentId propNode = addProp(props, std::move(spec), mesh);
            if (propId == "bench" && propNode.value != 0)
                QXL_TRY(buildBenchInstruments(propNode));
        }
    }
    if (layout_.compressor) { // pulse-tube compressor package; outside the −X wall when the layout
                              // says so,
        // seen through the wall window (BuildRoomDetail.cpp), its helium flex lines through the
        // wall
        const ComponentDescriptor* d = scene_.catalog().find("pt_compressor");
        const auto w = static_cast<float>(d ? d->geometryNumber("w_m", 0.7) : 0.7),
                   h = static_cast<float>(d ? d->geometryNumber("h_m", 1.0) : 1.0);
        const auto depth = static_cast<float>(d ? d->geometryNumber("d_m", 0.8) : 0.8);
        glm::dvec3 at = *layout_.compressor;
        if (layout_.compressorOutside)
            at.x = std::min(at.x,
                            -0.5 * layout_.roomWidth_m - 0.1 - 0.5 * static_cast<double>(w) - 0.25);
        NodeSpec spec;
        spec.descriptor = "pt_compressor";
        spec.instance = "pt_compressor";
        spec.display = "Pulse-tube compressor";
        spec.group = Group::Room;
        spec.material = "vertex";
        spec.local =
            Transform::rotated(at, {0.0, 1.0, 0.0}, 90.0); // control panel toward the window
        addProp(props, std::move(spec), props::ptCompressor(w, h, depth));
    }
    return {};
}

// The instruments that live on the assembly bench (spec 17 §2 amended): the stereo microscope,
// the wedge wire bonder, the sample box and the RF power meter (the lab's absolute dBm reference,
// spec 12 §14), each a component of the bench so the breadcrumb reads Bench › Wire bonder.
Status SceneBuilder::buildBenchInstruments(ComponentId bench) {
    for (const BenchItem& item : kBenchItems) {
        const ComponentDescriptor* d = scene_.catalog().find(item.id);
        if (!d)
            continue;
        const auto w = static_cast<float>(d->geometryNumber("w_m", 0.2)),
                   h = static_cast<float>(d->geometryNumber("h_m", 0.2));
        const auto depth = static_cast<float>(d->geometryNumber("d_m", 0.2));
        gfx::MeshData mesh;
        const std::string id = item.id;
        if (id == "microscope")
            mesh = props::microscope(w, h, depth);
        else if (id == "wire_bonder")
            mesh = props::wireBonder(w, h, depth);
        else if (id == "sample_box")
            mesh = props::sampleBox(w, h, depth);
        else { // power meter: readout unit with its display, the thermistor head on a short cable
            mesh::append(mesh, mesh::box({0.0f, 0.5f * h, 0.0f}, {w, h, depth}));
            mesh::appendColored(
                mesh,
                mesh::box({0.0f, 0.6f * h, 0.5f * depth + 0.001f}, {0.7f * w, 0.35f * h, 0.002f}),
                {0.05f, 0.12f, 0.08f, 1.0f});
            mesh::appendColored(
                mesh, gfx::shapes::cylinder(0.012f, 0.06f, 12, true), {0.25f, 0.25f, 0.27f, 1.0f},
                mesh::translate({0.7f * w, 0.012f, 0.1f}) * mesh::alignY({0.0f, 0.0f, 1.0f}));
            mesh::appendColored(mesh,
                                gfx::shapes::tube({{0.5f * w, 0.5f * h, 0.0f},
                                                   {0.62f * w, 0.02f, 0.06f},
                                                   {0.7f * w, 0.012f, 0.07f}},
                                                  0.003f, 6, true),
                                {0.12f, 0.14f, 0.30f, 1.0f});
        }
        NodeSpec spec;
        spec.descriptor = id;
        spec.instance = id;
        spec.group = Group::Room;
        spec.material = id == "power_meter" ? "plastic_grey" : "vertex";
        spec.local = Transform::rotated(item.at, {0.0, 1.0, 0.0}, item.yaw_deg);
        addProp(bench, std::move(spec), mesh);
    }
    return {};
}

} // namespace qlab::lab
