// Spec 17 §3.1 — the dilution refrigerator (Bluefors LD-class proportions, fridge detail pass):
// T-slot frame with the damped top-plate support and gantry, the RT top plate with its feedthrough
// ring, KF ports and viewport, the pulse-tube head, the stage plates at their layout heights with
// six support posts per gap, the flanged radiation shields and the outer vacuum can. The
// circulation bodies, thermometry and sample stage are in BuildFridgeInternals.cpp.
#include "Lab/Builder.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <format>
#include <glm/gtc/constants.hpp>

namespace qlab::lab {

namespace {
constexpr double kShieldClearance_m = 0.02;

NodeSpec part(std::string descriptor, std::string instance, Group group, std::string material, Transform local) {
    NodeSpec s;
    s.descriptor = std::move(descriptor);
    s.instance = std::move(instance);
    s.group = group;
    s.material = std::move(material);
    s.local = local;
    return s;
}

core::Json pointList(const std::vector<glm::dvec3>& pts) {
    core::Json j = core::Json::array();
    for (const auto& p : pts) j.push_back({p.x, p.y, p.z});
    return j;
}
} // namespace

glm::dvec3 SceneBuilder::polar(double azimuth_deg, double radius_m, double y) {
    const double a = glm::radians(azimuth_deg);
    return {radius_m * std::cos(a), y, radius_m * std::sin(a)};
}

double SceneBuilder::plateThickness(cryo::Stage s) const {
    const ComponentDescriptor* d = scene_.catalog().find(stageDescriptorId(s));
    return d ? d->geometryNumber("t_m", s == cryo::Stage::RT ? 0.02 : 0.012) : (s == cryo::Stage::RT ? 0.02 : 0.012);
}

ComponentId SceneBuilder::stageNode(cryo::Stage s) const { return scene_.stageNodes()[static_cast<std::size_t>(s)]; }

Status SceneBuilder::buildFridge(ComponentId root) {
    using cryo::Stage;
    ComponentId fridge = addGroup(root, "fridge", "Refrigerator", Group::FridgeExterior,
                                  Transform::at(layout_.fridgePosition_m));
    const double frameH = layout_.frameHeight_m;
    if (!hasStage(Stage::RT)) return fail(kErrLayout, "layout has no RT stage");
    const double rtT = plateThickness(Stage::RT);

    // ---- frame: T-slot extrusion, the top plate resting on rubber isolation blocks
    NodeSpec frame = part("fridge_frame", "fridge_frame", Group::FridgeExterior, "aluminium",
                          Transform::at(0.0, 0.5 * frameH, 0.0));
    frame.overrides = {{"width_m", layout_.frameWidth_m},
                       {"depth_m", layout_.frameDepth_m},
                       {"height_m", frameH},
                       {"profile_mm", 40.0},
                       {"support_y_m", stageHeight(Stage::RT) - 0.5 * rtT - 0.5 * frameH},
                       {"support_span_m", 1.5 * stageRadius(Stage::RT)}};
    QXL_TRY_ASSIGN(ComponentId frameId, addComponent(fridge, std::move(frame)));

    NodeSpec gantry = part("gantry_hoist", "gantry_hoist", Group::FridgeExterior, "stainless",
                           Transform::at(0.0, 0.5 * frameH + 0.5 * kGantryHeight_m, 0.0));
    gantry.overrides = {{"span_m", layout_.frameWidth_m}, {"travel_m", layout_.frameDepth_m}};
    gantry.params.setNumber("lowered", 0.0);
    QXL_TRY(addComponent(frameId, std::move(gantry)));

    NodeSpec motor = part("pt_remote_motor", "pt_remote_motor", Group::FridgeExterior, "plastic_grey",
                          Transform::at(0.45 * layout_.frameWidth_m, 0.5 * frameH - 0.075, 0.45 * layout_.frameDepth_m));
    QXL_TRY(addComponent(frameId, std::move(motor)));
    if (scene_.catalog().contains("rt_electronics_box")) { // thermometry bridge on the frame side
        NodeSpec box = part("rt_electronics_box", "rt_electronics_box", Group::FridgeExterior, "plastic_grey",
                            Transform::at(0.5 * layout_.frameWidth_m + 0.06, 0.25, 0.3));
        QXL_TRY(addComponent(frameId, std::move(box)));
    }

    // ---- top plate (RT) with the feedthrough ring of routing.json, KF ports and the viewport;
    // the still pumping line rises through a real hole in it (and in every plate above the still)
    const glm::dvec3 still = polar(kStillAzimuth_deg, kStillRadius_m);
    const core::Json pumpHole = {still.x, still.z, kPumpLineRadius_m + 0.005};
    NodeSpec top = part("top_plate_300K", "top_plate_300K", Group::FridgeExterior, "aluminium",
                        Transform::at(0.0, stageHeight(Stage::RT) - 0.5 * frameH, 0.0));
    top.overrides = {{"r_m", stageRadius(Stage::RT)},
                     {"t_m", rtT},
                     {"holes", "feedthrough_ring"},
                     {"hole_count", routing_.feedthroughCount},
                     {"hole_ring_m", routing_.feedthroughRadius_m},
                     {"start_angle_deg", routing_.feedthroughStart_deg},
                     {"through_hole_m", pumpHole}};
    top.params.setToken("stage", "rt");
    QXL_TRY_ASSIGN(topPlate_, addComponent(frameId, std::move(top)));
    scene_.stageNodes()[static_cast<std::size_t>(Stage::RT)] = topPlate_;

    const double headH = scene_.catalog().find("pulse_tube_head")->geometryNumber("h_m", 0.45);
    NodeSpec ptHead = part("pulse_tube_head", "pulse_tube_head", Group::FridgeExterior, "stainless",
                           Transform::at(polar(kPulseTubeAzimuth_deg, kPulseTubeRadius_m, 0.5 * rtT + 0.5 * headH)));
    QXL_TRY_ASSIGN(ptHead_, addComponent(topPlate_, std::move(ptHead)));
    {
        std::vector<glm::dvec3> path{{0.0, 0.5 * headH, 0.0}, {0.0, 0.5 * headH + 0.25, 0.0}, {-0.8, 0.5 * headH + 0.45, 0.6}};
        glm::dvec3 target = layout_.compressor.value_or(glm::dvec3(-3.2, 0.4, 2.6));
        glm::dvec3 headWorld = layout_.fridgePosition_m + polar(kPulseTubeAzimuth_deg, kPulseTubeRadius_m, stageHeight(Stage::RT) + 0.5 * rtT + 0.5 * headH);
        path.push_back(target + glm::dvec3(0.0, 0.6, 0.0) - headWorld);
        path.push_back(target + glm::dvec3(0.0, 0.4, 0.0) - headWorld);
        NodeSpec flex = part("pt_flex_lines", "pt_flex_lines", Group::FridgeExterior, "stainless", {});
        flex.overrides = {{"points_m", pointList(path)}, {"bundle", 2}, {"bundle_pitch_m", 0.03}};
        flex.cacheMesh = false;
        QXL_TRY(addComponent(ptHead_, std::move(flex)));
    }

    // ---- interior: stage plates at the layout heights, RT → MXC top to bottom, each hanging from
    // the plate above on six posts (stainless above 4 K, G10 below)
    fridgeInterior_ = addGroup(fridge, "fridge_interior", "Fridge interior", Group::FridgeInterior);
    struct StageBuild {
        Stage stage;
        const char* material;
        bool pumpHole;
    };
    const StageBuild kInterior[]{{Stage::PT1, "nickel_plated", true},
                                 {Stage::PT2, "gold_plated_cu", true},
                                 {Stage::STILL, "gold_plated_cu", false},
                                 {Stage::CP, "gold_plated_cu", false},
                                 {Stage::MXC, "gold_plated_cu", false}};
    Stage above = Stage::RT;
    for (const auto& sb : kInterior) {
        if (!hasStage(sb.stage)) continue;
        std::string id(stageDescriptorId(sb.stage));
        NodeSpec plate = part(id, id, Group::FridgeInterior, sb.material, Transform::at(0.0, stageHeight(sb.stage), 0.0));
        plate.overrides = {{"r_m", stageRadius(sb.stage)}, {"t_m", plateThickness(sb.stage)}, {"holes", "bolt_circle"}};
        if (sb.pumpHole) plate.overrides["through_hole_m"] = pumpHole;
        plate.params.setToken("stage", std::string(stageLayoutName(sb.stage)));
        QXL_TRY_ASSIGN(ComponentId plateId, addComponent(fridgeInterior_, std::move(plate)));
        scene_.stageNodes()[static_cast<std::size_t>(sb.stage)] = plateId;

        // posts from the underside of the plate above down to the top of this plate, on this
        // plate's bolt circle (the counterbore azimuths are multiples of 360°/24 or /36)
        const bool stainless = sb.stage == Stage::PT1 || sb.stage == Stage::PT2;
        const double L = plateUnderside(above) - plateTop(sb.stage);
        const double ring = plateBoltRing_m(stageRadius(sb.stage));
        const ComponentId upper = stageNode(above);
        for (int k = 0; k < kPostsPerStage && L > 0.01; ++k) {
            const double az = 30.0 + 360.0 * k / kPostsPerStage;
            NodeSpec post = part("support_post", std::format("support_post.{}[{}]", stageLayoutName(sb.stage), k), Group::FridgeInterior,
                                 stainless ? "stainless" : "g10",
                                 Transform::at(polar(az, ring, -0.5 * plateThickness(above) - 0.5 * L)));
            post.display = std::format("Support post {} ({} → {})", k + 1, cryo::stageName(above), cryo::stageName(sb.stage));
            post.overrides = {{"d_m", stainless ? 0.020 : 0.012}, {"L_m", L}, {"wall_m", stainless ? 0.0005 : 0.006}};
            post.params.setToken("stage", std::string(stageLayoutName(above)));
            post.params.setNumber("length_m", L);
            post.params.setNumber("diameter_mm", stainless ? 20.0 : 12.0);
            QXL_TRY(addComponent(upper, std::move(post)));
        }
        above = sb.stage;
    }

    // ---- shields, fitted so each one encloses everything colder (the descriptor heights assume a
    // different stage spacing; the layout's heights win, spec 17 §11). Each has a dished base of
    // depth r/2 below its cylindrical wall.
    const double magHeight = scene_.catalog().contains("mag_shield_mumetal")
                                 ? scene_.catalog().find("mag_shield_mumetal")->geometryNumber("h_m", 0.2)
                                 : 0.2;
    double innerBottom = plateUnderside(Stage::MXC) - (layout_.magShield ? magHeight : 0.06);
    if (layout_.shields) {
        const struct {
            Stage stage;
            const char* id;
        } kShields[]{{Stage::STILL, "shield_still"}, {Stage::PT2, "shield_4K"}, {Stage::PT1, "shield_50K"}};
        for (const auto& sh : kShields) {
            ComponentId parent = stageNode(sh.stage);
            if (parent.value == 0) continue;
            const double r = stageRadius(sh.stage) - 0.005;
            double base = innerBottom - kShieldClearance_m;
            double height = plateUnderside(sh.stage) - base;
            double authored = scene_.catalog().find(sh.id)->geometryNumber("h_m", height);
            if (std::abs(authored - height) > 0.01)
                note(std::format("{}: height fitted to {:.3f} m for nesting (descriptor says {:.3f} m)", sh.id, height, authored));
            NodeSpec shield = part(sh.id, sh.id, Group::FridgeInterior, "aluminium",
                                   Transform::at(0.0, -0.5 * plateThickness(sh.stage), 0.0));
            shield.overrides = {{"h_m", height}, {"r_m", r}, {"flange_bolts", static_cast<int>(std::lround(2.0 * glm::pi<double>() * r / 0.06))}};
            shield.params.setToken("stage", std::string(stageLayoutName(sh.stage)));
            QXL_TRY(addComponent(parent, std::move(shield)));
            innerBottom = base - 0.5 * r; // the dished head
        }
    }
    {   // outer vacuum can: long enough to close over the outermost shield
        double rim = stageHeight(Stage::RT) - 0.5 * rtT;
        const ComponentDescriptor* d = scene_.catalog().find("ovc");
        double height = std::max(d->geometryNumber("h_m", 1.35), rim - (innerBottom - kShieldClearance_m));
        NodeSpec ovc = part("ovc", "ovc", Group::FridgeExterior, "stainless", Transform::at(0.0, -0.5 * rtT, 0.0));
        ovc.overrides = {{"h_m", height}};
        ovc.params.setNumber("lowered", 0.0);
        QXL_TRY(addComponent(topPlate_, std::move(ovc)));
    }

    QXL_TRY(buildPulseTubeStages());
    QXL_TRY(buildCirculation());
    QXL_TRY(buildThermometry());
    return buildSampleStage();
}

} // namespace qlab::lab
