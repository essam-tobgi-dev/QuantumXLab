// Spec 17 §3.6 — trapped-ion laboratory at reduced detail: the UHV chamber replaces the fridge
// subtree, everything else (room, racks, props) is shared.
#include "Lab/Builder.hpp"
#include <format>
#include <glm/gtc/constants.hpp>

namespace qlab::lab {

Status SceneBuilder::buildIonLab(ComponentId root, const hw::LoadedDevice& device) {
    if (!layout_.chamberPosition_m) return {};
    const double pi = glm::pi<double>();
    ComponentId chamberGroup = addGroup(root, "ion_chamber", "Ion trap", Group::FridgeInterior,
                                       Transform::at(*layout_.chamberPosition_m));
    NodeSpec chamber;
    chamber.descriptor = "vacuum_chamber";
    chamber.instance = "vacuum_chamber";
    chamber.group = Group::FridgeInterior;
    chamber.material = "stainless";
    chamber.overrides = {{"r_m", layout_.chamberRadius_m}};
    QXL_TRY_ASSIGN(ComponentId chamberId, addComponent(chamberGroup, std::move(chamber)));

    NodeSpec trap;
    trap.descriptor = "trap_chip";
    trap.instance = "trap_chip";
    trap.display = std::format("Surface trap ({} ions)", device.device.qubitCount());
    trap.group = Group::ChipMicro;
    trap.material = "vertex";
    trap.local = Transform::at(0.0, -0.01, 0.0);
    trap.params.setIndex("i", 0);
    QXL_TRY(addComponent(chamberId, std::move(trap)));

    for (std::size_t i = 0; i < layout_.lasers.size(); ++i) {
        const auto& laser = layout_.lasers[i];
        double angle = pi * (0.25 + 0.25 * static_cast<double>(i));
        NodeSpec beam;
        beam.descriptor = laser.id.empty() ? "laser_path" : laser.id;
        beam.instance = std::format("laser_path[{}]", i);
        beam.display = std::format("{:.0f} nm beam ({})", laser.wavelength_nm, laser.role);
        beam.group = Group::FridgeInterior;
        beam.material = "glow";
        beam.local = Transform::rotated({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, angle * 180.0 / pi);
        beam.local.rotation = glm::normalize(beam.local.rotation *
                                             glm::angleAxis(0.5 * pi, glm::dvec3(0.0, 0.0, 1.0)));
        beam.overrides = {{"wavelength_nm", laser.wavelength_nm}, {"colour_from", "wavelength"}};
        beam.params.setIndex("i", static_cast<long long>(i));
        QXL_TRY(addComponent(chamberGroup, std::move(beam)));
    }

    struct Aux {
        const char* id;
        const char* material;
        glm::dvec3 at;
    };
    const Aux kAux[]{{"imaging_objective", "glass", {0.0, 0.09, 0.0}},
                     {"pmt_camera", "black_anodized", {0.0, 0.17, 0.0}},
                     {"helical_resonator", "copper", {0.22, 0.0, 0.0}},
                     {"magnetic_coils", "copper", {0.0, 0.0, 0.0}}};
    for (const auto& a : kAux) {
        if (!scene_.catalog().contains(a.id)) continue;
        NodeSpec spec;
        spec.descriptor = a.id;
        spec.instance = a.id;
        spec.group = Group::FridgeInterior;
        spec.material = a.material;
        spec.local = Transform::at(a.at);
        spec.params.setIndex("i", 0);
        QXL_TRY(addComponent(chamberGroup, std::move(spec)));
    }
    return {};
}

} // namespace qlab::lab
