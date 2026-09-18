// Spec 17 §3.4 — the gas-handling system (rack detail pass): the cabinet with its mimic panel
// (Box front: valve_mimic), the 20 panel valves with labels and 6 dial gauges on the panel's
// shared layout (Generators.hpp), the flow meter, and the plant around it: turbo pump on the
// roof, scroll pump and ³He compressor to the right, the two dump tanks and the LN₂ trap to the
// left, each with its descriptor so every part is inspectable.
#include "Lab/Builder.hpp"
#include "Lab/MeshOps.hpp"
#include "Lab/PropMeshes.hpp"
#include <format>

namespace qlab::lab {

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

    // The panel face is 12 mm proud of the cabinet front (Box valve_mimic); discs and dials are
    // generated along +y and rotated to face the room.
    const double zPanel = 0.5 * D + 0.006;
    auto onPanel = [&](double fx, double fy, double lift) { return Transform::rotated({fx * W, fy * H, zPanel + lift}, {1.0, 0.0, 0.0}, 90.0); };
    for (int i = 0; i < kGhsValveRows * kGhsValveCols; ++i) { // valve mimic: rows are manifold lines, columns the branches
        const int row = i / kGhsValveCols, col = i % kGhsValveCols;
        NodeSpec valve;
        valve.descriptor = "valve";
        valve.instance = std::format("valve[{}]", i);
        valve.display = std::format("Valve V{}", i + 1);
        valve.group = Group::GasHandling;
        valve.material = "stainless";
        valve.local = onPanel(ghsValveX(col), ghsValveY(row), 0.010);
        valve.params.setIndex("i", i);
        QXL_TRY(addComponent(cabinetId, std::move(valve))); // its tag plate (V1 … V20) is part of the valve mesh
    }
    for (int i = 0; i < kGhsGaugeCount; ++i) { // gauge row above the diagram: OVC, still, condensing, dumps, trap, He supply
        NodeSpec gauge;
        gauge.descriptor = "pressure_gauge";
        gauge.instance = std::format("pressure_gauge[{}]", i);
        gauge.display = std::format("Pressure gauge P{}", i + 1);
        gauge.group = Group::GasHandling;
        gauge.material = "vertex";
        gauge.local = onPanel(ghsGaugeX(i), kGhsGaugeY, 0.004);
        gauge.params.setIndex("i", i);
        gauge.overrides = {{"needle_deg", -110.0 + 38.0 * i}}; // each dial at a different reading
        QXL_TRY(addComponent(cabinetId, std::move(gauge)));
    }
    {
        NodeSpec flow; // rotameter tube standing on the panel beside the still line
        flow.descriptor = "flow_meter";
        flow.instance = "flow_meter";
        flow.group = Group::GasHandling;
        flow.material = "glass";
        flow.local = Transform::at(kGhsFlowMeterX * W, kGhsFlowMeterY * H, zPanel + 0.012);
        QXL_TRY(addComponent(cabinetId, std::move(flow)));
    }
    return buildGhsPlant(ghs, W, H, D);
}

Status SceneBuilder::buildGhsPlant(ComponentId ghs, double W, double H, double D) {
    struct Plant {
        const char* id;
        const char* material;
        glm::dvec3 at; // floor point in the GHS frame
        int index;
        double yaw_deg;
    };
    // Turbo on the roof; scroll pump and compressor on the right (+x); dumps and LN₂ trap on the
    // left, their pipework toward the cabinet side.
    const Plant kPlant[]{{"turbo_pump", "aluminium", {0.15, H, -0.10}, 0, 0.0},
                         {"scroll_pump", "vertex", {0.5 * W + 0.30, 0.0, 0.10}, 0, 0.0},
                         {"he3_compressor", "vertex", {0.5 * W + 0.30, 0.0, -0.50}, 0, 0.0},
                         {"dump_tank", "stainless", {-0.5 * W - 0.30, 0.0, -0.22}, 0, 90.0},
                         {"dump_tank", "stainless", {-0.5 * W - 0.30, 0.0, 0.22}, 1, 90.0},
                         {"ln2_trap", "stainless", {-0.5 * W - 0.30, 0.0, 0.70}, 0, 90.0}};
    for (const auto& a : kPlant) {
        const ComponentDescriptor* d = scene_.catalog().find(a.id);
        if (!d) continue;
        const std::string id = a.id;
        const auto r = static_cast<float>(d->geometryNumber("r_m", 0.1)), h = static_cast<float>(d->geometryNumber("h_m", 0.3));
        const auto w = static_cast<float>(d->geometryNumber("w_m", 0.3)), depth = static_cast<float>(d->geometryNumber("d_m", 0.5));
        gfx::MeshData mesh;
        if (id == "turbo_pump") mesh = props::turboPump(r, h);
        else if (id == "scroll_pump") mesh = props::scrollPump(w, h, depth);
        else if (id == "he3_compressor") mesh = props::he3Compressor(w, h, depth);
        else if (id == "dump_tank") mesh = props::dumpTank(r, h);
        else if (id == "ln2_trap") mesh = props::ln2Trap(r, h);
        NodeSpec spec;
        spec.descriptor = id;
        spec.instance = id == "dump_tank" ? std::format("dump_tank[{}]", a.index) : id;
        spec.display = id == "dump_tank" ? std::format("{} {}", d->name, a.index + 1) : d->name;
        spec.group = Group::GasHandling;
        spec.material = a.material;
        spec.local = Transform::rotated(a.at, {0.0, 1.0, 0.0}, a.yaw_deg);
        spec.params.setIndex("i", a.index);
        addProp(ghs, std::move(spec), mesh);
    }
    // Interconnecting pipework (stainless, Ø 12 mm): turbo fore-line down the cabinet back,
    // dump manifold into the cabinet's left side, pump lines into its right side.
    gfx::MeshData pipes;
    const float xL = static_cast<float>(-0.5 * W), xR = static_cast<float>(0.5 * W), Hf = static_cast<float>(H), Df = static_cast<float>(D);
    const glm::vec4 steel{0.72f, 0.73f, 0.74f, 1.0f};
    mesh::appendColored(pipes, gfx::shapes::tube({{0.15f, Hf + 0.05f, -0.10f}, {0.15f, Hf + 0.05f, -0.5f * Df - 0.03f}, {0.15f, 0.25f, -0.5f * Df - 0.03f},
                                                  {xR + 0.30f, 0.25f, -0.5f * Df - 0.03f}}, 0.006f, 8, false), steel);
    mesh::appendColored(pipes, gfx::shapes::tube({{xL - 0.30f, 0.10f, -0.05f}, {xL - 0.30f, 0.10f, 0.05f}}, 0.006f, 8, false), steel);
    mesh::appendColored(pipes, gfx::shapes::tube({{xL - 0.30f, 0.10f, 0.0f}, {xL - 0.02f, 0.10f, 0.0f}, {xL - 0.02f, 0.45f * Hf, 0.0f}}, 0.006f, 8, false), steel);
    mesh::appendColored(pipes, gfx::shapes::tube({{xL - 0.30f, 0.62f, 0.70f}, {xL - 0.02f, 0.62f, 0.70f}, {xL - 0.02f, 0.62f, 0.30f}}, 0.006f, 8, false), steel);
    mesh::appendColored(pipes, gfx::shapes::tube({{xR + 0.30f, 0.30f, 0.10f}, {xR + 0.02f, 0.30f, 0.10f}, {xR + 0.02f, 0.30f, -0.20f}}, 0.006f, 8, false), steel);
    addScenery(ghs, "ghs_pipework", "GHS pipework", Group::GasHandling, {}, std::move(pipes), "stainless");
    return {};
}

} // namespace qlab::lab
