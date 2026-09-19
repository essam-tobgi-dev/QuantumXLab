// Spec 17 §3.5/§6.3 — the processor package and the chip itself. The substrate is the root of the
// ChipMicro scale island: it carries a 1e-6 scale and everything under it is authored in
// micrometres (spec 17 §1).
#include "Lab/Builder.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <glm/gtc/constants.hpp>

namespace qlab::lab {

namespace {
constexpr double kPackageLid_m = 0.005; // package half-height
constexpr double kPcbThickness_m = 0.0016;
constexpr double kSubstrateThickness_um = 350.0;
constexpr double kWirebondReach_um = 800.0;

// Chip plane (x, y) → the island's mesh plane (X, Z) = (x, −y).
glm::dvec3 toMesh(glm::dvec2 chip, double y) {
    return {chip.x, y, -chip.y};
}

core::Json pathOf(const std::vector<glm::dvec2>& path, const ChipLayout& chip) {
    core::Json j = core::Json::array();
    for (const auto& p : path) {
        glm::dvec2 c = chip.centred(p);
        j.push_back({c.x, c.y});
    }
    return j;
}
} // namespace

Status SceneBuilder::buildChip(ComponentId parent, const hw::LoadedDevice& device,
                               const ChipLayout& chip) {
    const double degrees = 180.0 / glm::pi<double>();
    const double filmTop = kFilmMetalTop_m * 1e6;
    const double substrateTop = 0.5 * kSubstrateThickness_um;

    NodeSpec package;
    package.descriptor = "chip_package";
    package.instance = "chip_package";
    package.group = Group::Chip;
    package.material = "aluminium";
    package.local = Transform::at(0.0, 0.0, 0.0);
    package.assembly = Assembly::ChipPackage;
    QXL_TRY_ASSIGN(ComponentId packageId, addComponent(parent, std::move(package)));

    NodeSpec cavity;
    cavity.descriptor = "package_cavity";
    cavity.instance = "package_cavity";
    cavity.group = Group::Chip;
    cavity.material = "vertex";
    cavity.local = Transform::at(0.0, 0.002, 0.0);
    cavity.overrides = {{"w_m", 0.024}, {"h_m", 0.006}, {"d_m", 0.024}, {"wireframe", true}};
    QXL_TRY(addComponent(packageId, std::move(cavity)));

    NodeSpec pcb;
    pcb.descriptor = "pcb";
    pcb.instance = "pcb";
    pcb.group = Group::Chip;
    pcb.material = "vertex"; // the board and its gold launchers come from the mesh's vertex colours
    pcb.local = Transform::at(0.0, kPackageLid_m + 0.5 * kPcbThickness_m, 0.0);
    pcb.assembly = Assembly::ChipPackage;
    pcb.assemblyIndex = 1;
    QXL_TRY(addComponent(packageId, std::move(pcb)));

    NodeSpec substrate;
    substrate.descriptor = "substrate";
    substrate.instance = "substrate";
    substrate.display = std::format("Silicon substrate ({:.1f} × {:.1f} mm)",
                                    chip.size_um.x / 1000.0, chip.size_um.y / 1000.0);
    substrate.group = Group::ChipMicro;
    substrate.material = "silicon";
    substrate.local = Transform::scaled(
        {0.0, kPackageLid_m + kPcbThickness_m + 0.5 * kSubstrateThickness_um * 1e-6, 0.0}, 1e-6);
    substrate.unitScale = 1e6;
    substrate.assembly = Assembly::ChipPackage;
    substrate.assemblyIndex = 2;
    substrate.overrides = {
        {"w_um", chip.size_um.x}, {"d_um", chip.size_um.y}, {"h_um", kSubstrateThickness_um}};
    QXL_TRY_ASSIGN(ComponentId sub, addComponent(packageId, std::move(substrate)));
    scene_.setChipRoot(sub);

    // ---- ground plane with a pocket around every pad
    {
        core::Json holes = core::Json::array();
        for (const auto& q : chip.qubits) {
            glm::dvec2 c = chip.centred(q.pos_um);
            holes.push_back({c.x, -c.y, q.radius_um() + 20.0});
        }
        NodeSpec plane;
        plane.descriptor = "ground_plane";
        plane.instance = "ground_plane";
        plane.group = Group::ChipMicro;
        plane.material = "niobium_film";
        plane.unitScale = 1e6;
        plane.local = Transform::at(0.0, substrateTop, 0.0);
        plane.overrides = {{"w_um", chip.size_um.x},
                           {"d_um", chip.size_um.y},
                           {"t_um", kFilmGround_m * 1e6},
                           {"holes_um", holes}};
        plane.cacheMesh = false;
        QXL_TRY(addComponent(sub, std::move(plane)));
    }

    // ---- qubits: Xmon pad, junction (or SQUID), readout resonator
    const bool tunable = !chip.qubits.empty() && chip.qubits.front().tunable;
    scene_.qubitNodes().assign(chip.qubits.size(), ComponentId{0});
    scene_.resonatorNodes().assign(chip.qubits.size(), ComponentId{0});
    for (const auto& q : chip.qubits) {
        glm::dvec2 c = chip.centred(q.pos_um);
        NodeSpec pad;
        pad.descriptor = q.coupler ? "coupler_tunable" : "transmon_pad";
        pad.instance =
            q.coupler ? std::format("coupler[{}]", q.index) : std::format("q[{}]", q.index);
        pad.display = q.coupler ? std::format("Tunable coupler {}", q.index)
                                : std::format("Qubit q[{}]", q.index);
        pad.group = Group::ChipMicro;
        pad.material = "niobium_film";
        pad.unitScale = 1e6;
        pad.local = Transform::at(toMesh(c, substrateTop));
        pad.overrides = {{"arm_length_um", q.armLength_um},
                         {"arm_width_um", q.armWidth_um},
                         {"gap_um", q.gap_um},
                         {"scale", 1.0}};
        pad.params.setIndex("i", q.index);
        pad.params.setIndex("q", q.index);
        if (q.coupler)
            for (std::size_t e = 0; e < chip.couplers.size(); ++e)
                if (chip.couplers[e].couplerQubit && *chip.couplers[e].couplerQubit == q.index)
                    pad.params.setIndex("e", static_cast<long long>(e));
        QXL_TRY_ASSIGN(ComponentId padId, addComponent(sub, std::move(pad)));
        scene_.qubitNodes()[q.index] = padId;

        // junction (fixed frequency) or SQUID loop with two junctions (flux tunable)
        glm::dvec3 tip{0.0, filmTop, q.armLength_um - 0.5 * q.armWidth_um};
        if (tunable || q.coupler) {
            NodeSpec squid;
            squid.descriptor = "squid_loop";
            squid.instance = std::format("{}.squid", q.coupler ? std::format("coupler[{}]", q.index)
                                                               : std::format("q[{}]", q.index));
            squid.display = "SQUID loop";
            squid.group = Group::ChipMicro;
            squid.material = "niobium_film";
            squid.unitScale = 1e6;
            squid.local = Transform::at(tip);
            squid.params.setIndex("i", q.index);
            QXL_TRY_ASSIGN(ComponentId squidId, addComponent(padId, std::move(squid)));
            double side = std::sqrt(60.0);
            for (int k = 0; k < 2; ++k) {
                NodeSpec jj;
                jj.descriptor = "junction";
                jj.instance = std::format("q[{}].junction[{}]", q.index, k);
                jj.display = std::format("Josephson junction {}", k + 1);
                jj.group = Group::ChipMicro;
                jj.material = "niobium_film";
                jj.unitScale = 1e6;
                jj.local = Transform::at((k == 0 ? -0.5 : 0.5) * side, kFilmJunction_m * 1e6, 0.0);
                jj.params.setIndex("i", q.index);
                QXL_TRY(addComponent(squidId, std::move(jj)));
            }
        } else {
            NodeSpec jj;
            jj.descriptor = "junction";
            jj.instance = std::format("q[{}].junction", q.index);
            jj.group = Group::ChipMicro;
            jj.material = "niobium_film";
            jj.unitScale = 1e6;
            jj.local = Transform::at(tip);
            jj.params.setIndex("i", q.index);
            QXL_TRY(addComponent(padId, std::move(jj)));
        }
    }
    for (const auto& r : chip.resonators) {
        glm::dvec2 c = chip.centred(r.origin_um);
        NodeSpec res;
        res.descriptor = "readout_resonator";
        res.instance = std::format("res[{}]", r.qubit);
        res.display =
            std::format("Readout resonator q[{}] ({:.3f} GHz)", r.qubit, r.frequency_Hz / 1e9);
        res.group = Group::ChipMicro;
        res.material = "niobium_film";
        res.unitScale = 1e6;
        // the meander is generated along +x in the chip plane; the y-flip turns a chip-plane
        // rotation by θ into a mesh rotation by −θ about +Y
        res.local =
            Transform::rotated(toMesh(c, substrateTop), {0.0, 1.0, 0.0}, -r.angle_rad * degrees);
        res.overrides = {
            {"length_um", r.length_um}, {"pitch_um", r.pitch_um}, {"amplitude_um", r.amplitude_um},
            {"lead_um", r.lead_um},     {"w_um", 10.0},           {"s_um", 6.0}};
        res.params.setIndex("i", r.qubit);
        res.params.setIndex("q", r.qubit);
        QXL_TRY_ASSIGN(ComponentId id, addComponent(sub, std::move(res)));
        scene_.resonatorNodes()[r.qubit] = id;
    }

    // ---- feedlines with their Purcell filters, couplers, drive and flux lines
    auto addCpw = [&](std::string descriptor, std::string instance, std::string display,
                      const std::vector<glm::dvec2>& path, InstanceParams params) -> Status {
        if (path.size() < 2)
            return {};
        NodeSpec spec;
        spec.descriptor = std::move(descriptor);
        spec.instance = std::move(instance);
        spec.display = std::move(display);
        spec.group = Group::ChipMicro;
        spec.material = "niobium_film";
        spec.unitScale = 1e6;
        spec.local = Transform::at(0.0, substrateTop, 0.0);
        spec.overrides = {{"path_um", pathOf(path, chip)}, {"w_um", 10.0}, {"s_um", 6.0}};
        spec.params = std::move(params);
        spec.cacheMesh = false;
        QXL_TRY(addComponent(sub, std::move(spec)));
        return {};
    };
    for (const auto& f : chip.feedlines) {
        InstanceParams p;
        p.setIndex("i", f.id);
        QXL_TRY(addCpw("feedline", std::format("feedline[{}]", f.id),
                       std::format("Feedline {}", f.id), f.route.path_um, p));
        QXL_TRY(addCpw("purcell_filter", std::format("purcell[{}]", f.id),
                       std::format("Purcell filter {}", f.id), f.purcell.path_um, p));
    }
    for (std::size_t e = 0; e < chip.couplers.size(); ++e) {
        const auto& cpl = chip.couplers[e];
        if (cpl.couplerQubit)
            continue; // a tunable coupler is its own pad, drawn above
        InstanceParams p;
        p.setIndex("e", static_cast<long long>(e));
        for (std::size_t s = 0; s < cpl.stubs.size(); ++s)
            QXL_TRY(addCpw("coupler_fixed", std::format("coupler[{}-{}]", cpl.a, cpl.b),
                           std::format("Coupler q[{}]–q[{}]", cpl.a, cpl.b), cpl.stubs[s].path_um,
                           p));
    }
    for (const auto* lines : {&chip.driveLines, &chip.fluxLines})
        for (const auto& c : *lines) {
            InstanceParams p;
            p.setIndex("i", c.target);
            p.setIndex("q", c.target);
            QXL_TRY(addCpw(c.flux ? "flux_line" : "drive_line",
                           std::format("{}[{}]", c.flux ? "flux" : "drive", c.target),
                           std::format("{} line q[{}]", c.flux ? "Flux" : "Drive", c.target),
                           c.route.path_um, p));
        }

    // ---- airbridges (instanced by the renderer: one node each, contiguous ids)
    const ComponentDescriptor* bridge = scene_.catalog().find("airbridge");
    for (std::size_t i = 0; i < chip.airbridges.size() && bridge; ++i) {
        const auto& a = chip.airbridges[i];
        glm::dvec2 c = chip.centred(a.pos_um);
        NodeSpec spec;
        spec.descriptor = "airbridge";
        spec.instance = std::format("airbridge[{}]", i);
        spec.group = Group::ChipMicro;
        spec.material = "niobium_film";
        spec.unitScale = 1e6;
        spec.local = Transform::rotated(toMesh(c, substrateTop + kFilmGapTop_m * 1e6),
                                        {0.0, 1.0, 0.0}, -a.angle_rad * degrees);
        QXL_TRY(addComponent(sub, std::move(spec)));
    }

    // ---- flip-chip bumps, when the package carries the control wiring on a carrier die
    if (flipChipBumps_ && scene_.catalog().contains("flip_chip_bump")) {
        const double pitch = 250.0;
        int nx = std::max(2, static_cast<int>(chip.size_um.x / pitch) - 4);
        int ny = std::max(2, static_cast<int>(chip.size_um.y / pitch) - 4);
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                glm::dvec2 p{(i - 0.5 * (nx - 1)) * pitch, (j - 0.5 * (ny - 1)) * pitch};
                NodeSpec bump;
                bump.descriptor = "flip_chip_bump";
                bump.instance = std::format("bump[{},{}]", i, j);
                bump.group = Group::ChipMicro;
                bump.material = "aluminium";
                bump.unitScale = 1e6;
                bump.local = Transform::at(toMesh(p, -substrateTop - 10.0));
                QXL_TRY(addComponent(sub, std::move(bump)));
            }
    }

    // ---- bond pads with their aluminium wires to the PCB launchers
    for (std::size_t i = 0; i < chip.bondPads.size(); ++i) {
        const auto& b = chip.bondPads[i];
        glm::dvec2 c = chip.centred(b.pos_um);
        glm::dvec3 out = toMesh(b.outward, 0.0) * kWirebondReach_um;
        NodeSpec spec;
        spec.descriptor = "bond_pad";
        spec.instance = std::format("bond_pad[{}]", i);
        spec.display = b.signal >= 0 && b.signal < static_cast<int>(chip.signalNames.size())
                           ? std::format("Bond pad {} ({})", i,
                                         chip.signalNames[static_cast<std::size_t>(b.signal)])
                           : std::format("Bond pad {} (ground)", i);
        spec.group = Group::ChipMicro;
        spec.material = "aluminium";
        spec.unitScale = 1e6;
        spec.local = Transform::at(toMesh(c, substrateTop));
        spec.overrides = {{"p0_um", {0.0, 0.0, 0.0}},
                          {"p1_um", {out.x, -kSubstrateThickness_um - 20.0, out.z}},
                          {"h_um", 300.0},
                          {"pad_um", 90.0}};
        spec.cacheMesh = false;
        QXL_TRY(addComponent(sub, std::move(spec)));
    }
    (void)device;
    return {};
}

} // namespace qlab::lab
