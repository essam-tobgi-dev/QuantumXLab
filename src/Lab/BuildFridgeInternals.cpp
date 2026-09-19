// Spec 17 §3.1 (fridge detail pass) — inside the chandelier: the two pulse-tube stages with their
// copper braids, the circulation path (still with its heater and Ø 50 mm pumping line rising
// through every plate, condensing line with its impedance, continuous and step heat exchangers,
// mixing chamber with its heater), thermometry, and the sample stage under the mixing chamber.
#include "Lab/Builder.hpp"
#include <algorithm>
#include <format>

namespace qlab::lab {

namespace {
using cryo::Stage;

NodeSpec part(std::string descriptor, std::string instance, std::string material, Transform local) {
    NodeSpec s;
    s.descriptor = std::move(descriptor);
    s.instance = std::move(instance);
    s.group = Group::FridgeInterior;
    s.material = std::move(material);
    s.local = local;
    return s;
}

core::Json pointList(const std::vector<glm::dvec3>& pts) {
    core::Json j = core::Json::array();
    for (const auto& p : pts)
        j.push_back({p.x, p.y, p.z});
    return j;
}
} // namespace

Status SceneBuilder::buildPulseTubeStages() {
    if (!scene_.catalog().contains("pt_regenerator") || ptHead_.value == 0)
        return {};
    const glm::dvec3 pt = polar(kPulseTubeAzimuth_deg, kPulseTubeRadius_m);
    const double headH = scene_.catalog().find("pulse_tube_head")->geometryNumber("h_m", 0.45);
    struct StageTube {
        Stage from, to; // plate it hangs from, plate its cold flange bolts to
        double radius;
        const char* display;
    };
    const StageTube kTubes[]{{Stage::RT, Stage::PT1, 0.020, "Pulse-tube stage 1 (45 K)"},
                             {Stage::PT1, Stage::PT2, 0.015, "Pulse-tube stage 2 (4 K)"}};
    int index = 0;
    for (const auto& st : kTubes) {
        if (!hasStage(st.from) || !hasStage(st.to))
            continue;
        const double top = plateUnderside(st.from), bottom = plateTop(st.to), L = top - bottom;
        // stage 1 hangs under the head (child of the head node), stage 2 under the 50 K plate
        const bool underHead = st.from == Stage::RT;
        const ComponentId parent = underHead ? ptHead_ : stageNode(st.from);
        const double parentY =
            underHead ? stageHeight(Stage::RT) + 0.5 * plateThickness(Stage::RT) + 0.5 * headH
                      : stageHeight(st.from);
        NodeSpec tube =
            part("pt_regenerator", std::format("pt_regenerator[{}]", index), "stainless",
                 Transform::at(underHead ? 0.0 : pt.x, bottom + 0.5 * L - parentY,
                               underHead ? 0.0 : pt.z));
        tube.display = st.display;
        tube.overrides = {{"r_m", st.radius}, {"h_m", L}};
        tube.params.setIndex("i", index);
        tube.params.setToken("stage", std::string(stageLayoutName(st.to)));
        QXL_TRY(addComponent(parent, std::move(tube)));
        if (scene_.catalog().contains(
                "thermal_braid")) { // three braid bundles from the flange to the plate
            const ComponentId plate = stageNode(st.to);
            const double y0 = 0.5 * plateThickness(st.to);
            for (int k = 0; k < 3; ++k) {
                glm::dvec3 dir = polar(kPulseTubeAzimuth_deg + 60.0 + 120.0 * k, 1.0);
                std::vector<glm::dvec3> path{
                    pt + dir * st.radius + glm::dvec3(0.0, y0 + 0.014, 0.0),
                    pt + dir * (st.radius + 0.025) + glm::dvec3(0.0, y0 + 0.022, 0.0),
                    pt + dir * (st.radius + 0.055) + glm::dvec3(0.0, y0 + 0.003, 0.0)};
                NodeSpec braid = part(
                    "thermal_braid", std::format("thermal_braid.{}[{}]", stageLayoutName(st.to), k),
                    "copper", {});
                braid.display = std::format("Copper braid {} ({})", k + 1, cryo::stageName(st.to));
                braid.overrides = {{"points_m", pointList(path)}};
                braid.params.setIndex("i", k);
                braid.params.setToken("stage", std::string(stageLayoutName(st.to)));
                braid.cacheMesh = false;
                QXL_TRY(addComponent(plate, std::move(braid)));
            }
        }
        ++index;
    }
    return {};
}

Status SceneBuilder::buildCirculation() {
    if (!hasStage(Stage::STILL))
        return {};
    const glm::dvec3 still = polar(kStillAzimuth_deg, kStillRadius_m);
    const ComponentId stillPlate = stageNode(Stage::STILL);
    const double tStill = plateThickness(Stage::STILL);
    const double stillTop =
        plateTop(Stage::STILL) + kStillBodyHeight_m; // world y of the still's lid
    ComponentId stillId{0};
    if (scene_.catalog().contains("still")) {
        NodeSpec body =
            part("still", "still", "copper",
                 Transform::at(still.x, 0.5 * tStill + 0.5 * kStillBodyHeight_m, still.z));
        body.overrides = {{"r_m", kStillBodyRadius_m}, {"h_m", kStillBodyHeight_m}};
        body.params.setToken("stage", "still");
        QXL_TRY_ASSIGN(stillId, addComponent(stillPlate, std::move(body)));
        if (scene_.catalog().contains("still_pumping_line") && hasStage(Stage::RT)) {
            // Ø 50 mm line from the still lid up through the 4 K, 50 K and RT plates (real holes)
            const double top = plateTop(Stage::RT) + 0.06 - stillTop;
            NodeSpec line = part("still_pumping_line", "still_pumping_line", "stainless", {});
            line.overrides = {{"points_m", pointList({{0.0, 0.5 * kStillBodyHeight_m, 0.0},
                                                      {0.0, 0.5 * kStillBodyHeight_m + top, 0.0}})},
                              {"r_m", kPumpLineRadius_m}};
            line.params.setNumber("length_m", top);
            QXL_TRY(addComponent(stillId, std::move(line)));
        }
        NodeSpec heater = part("still_heater", "still_heater.still", "plastic_grey",
                               Transform::at(kStillBodyRadius_m + 0.005, -0.008, 0.0));
        heater.display = "Still heater";
        heater.params.setToken("stage", "still");
        QXL_TRY(addComponent(stillId, std::move(heater)));
    }
    if (hasStage(Stage::PT2)) { // condensing line 4 K → still with its flow impedance
        const ComponentId plate4K = stageNode(Stage::PT2);
        const double y0 = -0.5 * plateThickness(Stage::PT2);
        const glm::dvec3 a = polar(kStillAzimuth_deg, kStillRadius_m + 0.04);
        const double dyTop =
            stillTop - 0.012 - stageHeight(Stage::PT2); // enters the still's upper wall
        const glm::dvec3 entry = still + polar(kStillAzimuth_deg + 90.0, kStillBodyRadius_m);
        std::vector<glm::dvec3> path{{a.x, y0, a.z},
                                     {a.x, y0 + 0.45 * (dyTop - y0), a.z},
                                     {entry.x + 0.02 * (entry.x - still.x) / kStillBodyRadius_m,
                                      dyTop,
                                      entry.z + 0.02 * (entry.z - still.z) / kStillBodyRadius_m},
                                     {entry.x, dyTop, entry.z}};
        NodeSpec line = part("condensing_line", "condensing_line", "cuni", {});
        line.overrides = {{"points_m", pointList(path)}};
        line.cacheMesh = false;
        QXL_TRY_ASSIGN(ComponentId cond, addComponent(plate4K, std::move(line)));
        NodeSpec imp = part("flow_impedance", "flow_impedance", "cuni",
                            Transform::at(a.x, y0 + 0.45 * (dyTop - y0), a.z));
        QXL_TRY(addComponent(cond, std::move(imp)));
    }
    if (hasStage(Stage::CP)) { // continuous heat exchanger coil under the still, on its return tube
        const ComponentDescriptor* d = scene_.catalog().find("hx_continuous");
        double gap = plateUnderside(Stage::STILL) - plateTop(Stage::CP);
        double turns = std::max(4.0, d->geometryNumber("turns", 12.0));
        double tubeR = d->geometryNumber("tube_r_m", 0.003);
        double pitch = std::max(2.1 * tubeR, (gap - 2.0 * tubeR - 0.012) / turns);
        NodeSpec hx =
            part("hx_continuous", "hx_continuous", "cuni",
                 Transform::at(still.x, -0.5 * tStill - 0.006 - 0.5 * pitch * turns, still.z));
        hx.overrides = {{"pitch_m", pitch}};
        QXL_TRY(addComponent(stillPlate, std::move(hx)));
    }
    ComponentId mcId{0};
    if (hasStage(Stage::MXC) && scene_.catalog().contains("mixing_chamber")) {
        const double tMxc = plateThickness(Stage::MXC);
        NodeSpec mc = part("mixing_chamber", "mixing_chamber", "copper",
                           Transform::at(0.0, 0.5 * tMxc + 0.5 * kMcHeight_m, 0.0));
        mc.overrides = {{"r_m", kMcRadius_m}, {"h_m", kMcHeight_m}};
        mc.params.setToken("stage", "mxc");
        QXL_TRY_ASSIGN(mcId, addComponent(stageNode(Stage::MXC), std::move(mc)));
        NodeSpec heater = part("mxc_heater", "mxc_heater.mxc", "plastic_grey",
                               Transform::at(kMcRadius_m + 0.005, -0.015, 0.0));
        heater.display = "Mixing-chamber heater";
        heater.params.setToken("stage", "mxc");
        QXL_TRY(addComponent(mcId, std::move(heater)));
    }
    if (hasStage(Stage::CP) &&
        hasStage(Stage::MXC)) { // step exchangers stacked under the cold plate
        const ComponentId cp = stageNode(Stage::CP);
        const ComponentDescriptor* d = scene_.catalog().find("hx_step");
        int count = std::max(1, static_cast<int>(std::lround(d->geometryNumber("count", 4))));
        const double gap = plateUnderside(Stage::CP) - plateTop(Stage::MXC);
        const double h = std::min(d->geometryNumber("h_m", 0.03), (gap - 0.02) / count - 0.004),
                     spacing = 0.004;
        double y = -0.5 * plateThickness(Stage::CP) - 0.006;
        for (int i = 0; i < count; ++i) {
            NodeSpec step = part("hx_step", std::format("hx_step[{}]", i), "stainless",
                                 Transform::at(still.x, y - 0.5 * h, still.z));
            step.display = std::format("Step heat exchanger {}", i + 1);
            step.overrides = {{"h_m", h}};
            step.params.setIndex("i", i);
            QXL_TRY(addComponent(cp, std::move(step)));
            y -= h + spacing;
        }
        if (mcId.value !=
            0) { // concentrated-side capillary from the last exchanger into the mixing chamber
            const double mcTop = plateTop(Stage::MXC) + kMcHeight_m - stageHeight(Stage::CP);
            std::vector<glm::dvec3> path{{still.x, y + spacing, still.z},
                                         {0.6 * still.x, 0.5 * (y + mcTop), 0.6 * still.z},
                                         {0.012, mcTop, 0.012}};
            NodeSpec line = part("condensing_line", "condensing_line.mc", "cuni", {});
            line.display = "3He inlet to the mixing chamber";
            line.overrides = {{"points_m", pointList(path)}};
            line.cacheMesh = false;
            QXL_TRY(addComponent(cp, std::move(line)));
        }
    }
    return {};
}

Status SceneBuilder::buildThermometry() {
    struct Aux {
        Stage stage;
        const char* id;
        int index;
    };
    const Aux kAux[]{{Stage::MXC, "thermometer_ruo2", 0},
                     {Stage::CP, "thermometer_ruo2", 1},
                     {Stage::STILL, "thermometer_ruo2", 2},
                     {Stage::PT2, "thermometer_cernox", 3},
                     {Stage::PT1, "thermometer_cernox", 4}};
    for (const auto& a : kAux) {
        ComponentId parent = stageNode(a.stage);
        if (parent.value == 0)
            continue;
        NodeSpec spec =
            part(a.id, std::format("{}.{}", a.id, stageLayoutName(a.stage)), "plastic_grey",
                 Transform::at(polar(75.0, 0.55 * stageRadius(a.stage),
                                     0.5 * plateThickness(a.stage) + 0.0025)));
        spec.display =
            std::format("{} ({})", scene_.catalog().find(a.id)->name, cryo::stageName(a.stage));
        spec.params.setIndex("i", a.index);
        spec.params.setToken("stage", std::string(stageLayoutName(a.stage)));
        QXL_TRY(addComponent(parent, std::move(spec)));
    }
    return {};
}

Status SceneBuilder::buildSampleStage() {
    const ComponentId mxc = stageNode(Stage::MXC);
    if (mxc.value == 0)
        return {};
    const double t = plateThickness(Stage::MXC);
    if (layout_.magShield) { // Cryoperm can with its mounting flange, superconducting Al can inside
        NodeSpec mu = part("mag_shield_mumetal", "mag_shield_mumetal", "mu_metal",
                           Transform::at(0.0, -0.5 * t, 0.0));
        mu.overrides = {
            {"flange", true}, {"flange_t_m", 0.004}, {"flange_w_m", 0.008}, {"flange_bolts", 12}};
        mu.params.setToken("stage", "mxc");
        QXL_TRY_ASSIGN(ComponentId muId, addComponent(mxc, std::move(mu)));
        NodeSpec al =
            part("mag_shield_al", "mag_shield_al", "aluminium", Transform::at(0.0, -0.004, 0.0));
        al.overrides = {{"flange", true}, {"flange_t_m", 0.003}, {"flange_w_m", 0.004}};
        al.params.setToken("stage", "mxc");
        QXL_TRY(addComponent(muId, std::move(al)));
    }
    const double fingerH = scene_.catalog().find("cold_finger")->geometryNumber("h_m", 0.15);
    NodeSpec finger = part("cold_finger", "cold_finger", "gold_plated_cu",
                           Transform::at(0.0, -0.5 * t - 0.5 * fingerH, 0.0));
    finger.params.setToken("stage", "mxc");
    QXL_TRY_ASSIGN(ComponentId fingerId, addComponent(mxc, std::move(finger)));
    const double puckH = scene_.catalog().find("sample_puck")->geometryNumber("h_m", 0.05);
    NodeSpec puck = part("sample_puck", "sample_puck", "gold_plated_cu",
                         Transform::at(0.0, -0.5 * fingerH - 0.5 * puckH, 0.0));
    QXL_TRY_ASSIGN(puck_, addComponent(fingerId, std::move(puck)));
    return {};
}

} // namespace qlab::lab
