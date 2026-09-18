// Spec 17 §3.3/§3.4 — the two 42U racks with their unit lists and the gas-handling system.
#include "Lab/Builder.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <format>
#include <map>

namespace qlab::lab {

namespace {
constexpr double kRackWidth_m = 0.6, kRackDepth_m = 0.9, kRackHeight_m = 2.0, kRackPlinth_m = 0.1;

gfx::MeshData rackEnclosure() { // posts, side panels, plinth and top of a 19-inch cabinet
    gfx::MeshData m;
    const auto W = static_cast<float>(kRackWidth_m), D = static_cast<float>(kRackDepth_m), H = static_cast<float>(kRackHeight_m);
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f})
            mesh::append(m, mesh::box({sx * (0.5f * W - 0.02f), 0.0f, sz * (0.5f * D - 0.02f)}, {0.04f, H, 0.04f}));
    for (float sx : {-1.0f, 1.0f}) mesh::append(m, mesh::box({sx * (0.5f * W - 0.005f), 0.0f, 0.0f}, {0.01f, H - 0.08f, D - 0.08f}));
    mesh::append(m, mesh::box({0.0f, -0.5f * H + 0.5f * static_cast<float>(kRackPlinth_m), 0.0f},
                              {W, static_cast<float>(kRackPlinth_m), D}));
    mesh::append(m, mesh::box({0.0f, 0.5f * H - 0.01f, 0.0f}, {W, 0.02f, D}));
    mesh::append(m, mesh::box({0.0f, 0.0f, -0.5f * D + 0.005f}, {W - 0.08f, H - 0.08f, 0.01f}));
    return m;
}
} // namespace

Status SceneBuilder::buildRacks(ComponentId root) {
    std::map<std::string, int> instanceOf; // per descriptor: the $i index
    std::vector<std::size_t> outputLines;
    for (const auto& run : scene_.wiringRuns())
        if (run.kind == cryo::LineKind::ReadoutOut) outputLines.push_back(run.lineIndex);

    for (const auto& rack : layout_.racks) {
        ComponentId group = addGroup(root, std::format("rack_{}", rack.id), std::format("Rack {}", rack.id), Group::Rack,
                                     Transform::at(rack.position_m + glm::dvec3(0.0, 0.5 * kRackHeight_m, 0.0)));
        {   // The enclosure carries the rack's descriptor, so clicking the frame selects it.
            const gfx::MeshData enclosure = rackEnclosure();
            NodeSpec spec;
            spec.descriptor = "rack_enclosure";
            spec.instance = std::format("rack_{}.enclosure", rack.id);
            spec.display = std::format("Rack {} enclosure", rack.id);
            spec.group = Group::Rack;
            spec.material = "rack_black";
            spec.params.setToken("rack", rack.id);
            addProp(group, std::move(spec), enclosure);
        }
        double slotTop = 0.5 * kRackHeight_m - 0.03; // below the top panel, units stack downwards
        int slot = 0;
        for (const auto& [unitId, count] : rack.units) {
            const ComponentDescriptor* d = scene_.catalog().find(unitId);
            if (!d) {
                note(std::format("rack {}: unit '{}' has no component.json", rack.id, unitId));
                continue;
            }
            int u = std::max(1, static_cast<int>(std::lround(d->geometryNumber("u", 1))));
            double h = u * kRackUnit_m;
            for (int k = 0; k < std::max(1, count); ++k) {
                int index = instanceOf[unitId]++;
                NodeSpec spec;
                spec.descriptor = unitId;
                spec.instance = count > 1 ? std::format("rack_{}.{}[{}]", rack.id, unitId, index)
                                          : std::format("rack_{}.{}", rack.id, unitId);
                spec.display = count > 1 ? std::format("{} {}", d->name, index + 1) : d->name;
                spec.group = Group::Rack;
                spec.material = "vertex";
                spec.local = Transform::at(0.0, slotTop - 0.5 * h, 0.5 * kRackDepth_m - 0.5 * kRackUnitDepth_m - 0.01);
                spec.assembly = Assembly::RackUnits;
                spec.assemblyIndex = slot++;
                spec.params.setIndex("i", index);
                if (d->instrument && !d->instrument->channels.empty()) spec.params.setIndex("k", 0); // first channel
                if (unitId == "rt_amplifier" && index < static_cast<int>(outputLines.size()))
                    spec.params.setIndex("line", static_cast<long long>(outputLines[static_cast<std::size_t>(index)]));
                QXL_TRY(addComponent(group, std::move(spec)));
                slotTop -= h;
            }
        }
    }
    return {};
}

Status SceneBuilder::buildGasHandling(ComponentId root) {
    if (!layout_.ghs) return {};
    ComponentId ghs = addGroup(root, "ghs", "Gas handling system", Group::GasHandling, Transform::at(*layout_.ghs));
    const ComponentDescriptor* cab = scene_.catalog().find("ghs_cabinet");
    if (!cab) return fail(kErrScene, "gas handling needs the 'ghs_cabinet' component");
    const double W = cab->geometryNumber("w_m", 0.8), H = cab->geometryNumber("h_m", 1.9), D = cab->geometryNumber("d_m", 0.7);
    NodeSpec cabinet;
    cabinet.descriptor = "ghs_cabinet";
    cabinet.instance = "ghs_cabinet";
    cabinet.group = Group::GasHandling;
    cabinet.material = "plastic_grey";
    cabinet.local = Transform::at(0.0, 0.5 * H, 0.0);
    cabinet.params.setToken("state", "circulating");
    QXL_TRY_ASSIGN(ComponentId cabinetId, addComponent(ghs, std::move(cabinet)));

    auto onPanel = [&](double x, double y) { // front panel faces +Z; discs and dials face the room
        return Transform::rotated({x, y, 0.5 * D + 0.012}, {1.0, 0.0, 0.0}, 90.0);
    };
    for (int i = 0; i < 20; ++i) { // valve mimic: 5 × 4 grid of handwheels
        NodeSpec valve;
        valve.descriptor = "valve";
        valve.instance = std::format("valve[{}]", i);
        valve.display = std::format("Valve {}", i + 1);
        valve.group = Group::GasHandling;
        valve.material = "stainless";
        valve.local = onPanel(W * (0.17 * (i % 5) - 0.34), H * (0.06 - 0.12 * (i / 5)));
        valve.params.setIndex("i", i);
        QXL_TRY(addComponent(cabinetId, std::move(valve)));
    }
    for (int i = 0; i < 6; ++i) {
        NodeSpec gauge;
        gauge.descriptor = "pressure_gauge";
        gauge.instance = std::format("pressure_gauge[{}]", i);
        gauge.display = std::format("Pressure gauge {}", i + 1);
        gauge.group = Group::GasHandling;
        gauge.material = "vertex";
        gauge.local = onPanel(W * (0.15 * i - 0.375), 0.32 * H);
        gauge.params.setIndex("i", i);
        QXL_TRY(addComponent(cabinetId, std::move(gauge)));
    }
    {
        NodeSpec flow;
        flow.descriptor = "flow_meter";
        flow.instance = "flow_meter";
        flow.group = Group::GasHandling;
        flow.material = "glass";
        flow.local = Transform::at(0.35 * W, 0.2 * H, 0.5 * D + 0.02);
        QXL_TRY(addComponent(cabinetId, std::move(flow)));
    }
    struct Aux {
        const char* id;
        const char* material;
        glm::dvec3 at;
        int index;
    };
    const Aux kAux[]{{"turbo_pump", "aluminium", {0.0, H + 0.075, 0.0}, 0},
                     {"scroll_pump", "plastic_grey", {0.65, 0.15, 0.1}, 0},
                     {"he3_compressor", "plastic_grey", {-0.7, 0.2, 0.0}, 0},
                     {"ln2_trap", "stainless", {0.75, 0.2, -0.35}, 0},
                     {"dump_tank", "stainless", {-0.35, 0.3, -0.55}, 0},
                     {"dump_tank", "stainless", {0.05, 0.3, -0.55}, 1}};
    for (const auto& a : kAux) {
        const ComponentDescriptor* d = scene_.catalog().find(a.id);
        if (!d) continue;
        NodeSpec spec;
        spec.descriptor = a.id;
        spec.instance = std::string(a.id) == "dump_tank" ? std::format("dump_tank[{}]", a.index) : a.id;
        spec.display = std::string(a.id) == "dump_tank" ? std::format("{} {}", d->name, a.index + 1) : d->name;
        spec.group = Group::GasHandling;
        spec.material = a.material;
        spec.local = Transform::at(a.at);
        spec.params.setIndex("i", a.index);
        QXL_TRY(addComponent(ghs, std::move(spec)));
    }
    return {};
}

} // namespace qlab::lab
