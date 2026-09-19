// Spec 17 §3.2 / §11 — every line of wiring.json routed per routing.json: each kind takes its
// angular sector, each stage its bundle radius, elements sit in the stage slots below their plate
// and the coax runs between them are Catmull–Rom tubes whose control points are anchored to the
// stages they leave and enter (so the exploded view re-evaluates them, spec 17 §7.4).
#include "Lab/Builder.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <glm/gtc/constants.hpp>
#include <map>
#include <set>

namespace qlab::lab {

namespace {
using cryo::ElementKind;
using cryo::Stage;

struct Stop {
    Stage stage = Stage::RT;
    glm::dvec3 world{0.0};
    double halfLength = 0.0;
};

struct SlotKey {
    int stage;
    std::string slot;
    auto operator<=>(const SlotKey&) const = default;
};

// The element's extent along the line (vertical), from its descriptor geometry.
double alongLength(const ComponentDescriptor* d) {
    if (!d)
        return 0.02;
    GenParams p(d->geometry);
    if (d->generator == "CylinderSma")
        return p.length("length", 0.025);
    if (d->generator == "SmaConnector")
        return kSmaLength_m;
    return p.length("h", 0.02);
}

// Descriptor and slot for a chain element (spec 11 §4.6 catalog → spec 17 §3.2 components).
struct Mapped {
    const char* descriptor;
    const char* slot;
    const char* material;
};
std::optional<Mapped> mapElement(const cryo::Element& e) {
    switch (e.kind) {
    case ElementKind::Attenuator:
        return Mapped{"attenuator", "attn", "stainless"};
    case ElementKind::LowPassFilter:
        return Mapped{"lpf", "filter", "stainless"};
    case ElementKind::RcFilter:
        return Mapped{"lpf", "filter", "stainless"}; // RC loom filter, same part family
    case ElementKind::IrFilter:
        return Mapped{"ir_filter", "filter", "eccosorb"};
    case ElementKind::Isolator:
        return Mapped{"isolator", "iso", "stainless"};
    case ElementKind::Circulator:
        return Mapped{"circulator", "iso", "stainless"};
    case ElementKind::ThermalClamp:
        return Mapped{"thermal_clamp", "clamp", "copper"};
    case ElementKind::Bulkhead:
        return Mapped{"feedthrough_sma", "clamp", "vertex"};
    case ElementKind::Amplifier:
        return e.id == "hemt" ? std::optional<Mapped>{Mapped{"hemt", "amp", "plastic_grey"}}
                              : std::nullopt; // rt_amp is a rack B unit
    case ElementKind::Preamp:
        return e.variant == "jpa" ? std::optional<Mapped>{Mapped{"jpa", "amp", "niobium_film"}}
                                  : std::optional<Mapped>{Mapped{"twpa", "amp", "niobium_film"}};
    case ElementKind::CoaxSegment:
    case ElementKind::Chip:
        return std::nullopt;
    }
    return std::nullopt;
}
} // namespace

Status SceneBuilder::buildWiring(ComponentId root, const cryo::Wiring& wiring) {
    ComponentId wiringRoot = addGroup(root, "wiring", "Wiring", Group::Wiring);
    std::map<cryo::LineKind, int> total, seen;
    for (const auto& l : wiring.lines)
        ++total[l.kind];
    std::set<int> usedHoles;
    const double pi = glm::pi<double>();

    for (std::size_t li = 0; li < wiring.lines.size(); ++li) {
        const cryo::WiringLine& line = wiring.lines[li];
        const LineBundle* bundle = routing_.bundle(line.kind);
        const int index = seen[line.kind]++;
        const double t = (index + 0.5) / std::max(1, total[line.kind]);
        double theta = pi / 180.0 *
                       (bundle ? bundle->sectorStart_deg +
                                     t * (bundle->sectorEnd_deg - bundle->sectorStart_deg)
                               : 360.0 * t);
        auto radiusAt = [&](Stage s) {
            if (bundle) {
                auto it = bundle->radius_m.find(stageLayoutName(s));
                if (it != bundle->radius_m.end())
                    return it->second;
            }
            return 0.75 * stageRadius(s);
        };
        // snap the RT entry to a free feedthrough hole in the top plate ring
        double thetaRt = theta;
        if (routing_.feedthroughCount > 0) {
            int hole =
                static_cast<int>(std::lround((theta * 180.0 / pi - routing_.feedthroughStart_deg) /
                                             (360.0 / routing_.feedthroughCount)));
            for (int step = 0; step < routing_.feedthroughCount; ++step) {
                int probe =
                    ((hole + (step % 2 ? -1 : 1) * ((step + 1) / 2)) % routing_.feedthroughCount +
                     routing_.feedthroughCount) %
                    routing_.feedthroughCount;
                if (usedHoles.insert(probe).second) {
                    thetaRt =
                        pi / 180.0 *
                        (routing_.feedthroughStart_deg + 360.0 * probe / routing_.feedthroughCount);
                    break;
                }
            }
        }

        WiringRun run;
        run.lineId = line.id;
        run.lineIndex = li;
        run.kind = line.kind;
        std::map<int, ComponentId> stageGroup;
        std::map<SlotKey, double> stackBottom;
        auto groupFor = [&](Stage s) {
            int k = static_cast<int>(s);
            if (auto it = stageGroup.find(k); it != stageGroup.end())
                return it->second;
            ComponentId parent =
                s == Stage::RT ? topPlate_ : scene_.stageNodes()[static_cast<std::size_t>(s)];
            if (parent.value == 0)
                parent = wiringRoot;
            ComponentId g = addGroup(parent, std::format("line[{}].{}", li, stageLayoutName(s)),
                                     std::format("Line {}", line.id), Group::Wiring);
            stageGroup[k] = g;
            return g;
        };
        auto lineRadius = [&](Stage s, double inward) {
            double rMin =
                s == Stage::MXC ? 0.075 : 0.05; // clear of the magnetic shields / still line
            return std::clamp(radiusAt(s) - inward, rMin, stageRadius(s) - 0.012);
        };
        // World origin of a stage's plate (valid: the fridge transforms are flattened before
        // wiring).
        auto stageOrigin = [&](Stage s) {
            ComponentId plate =
                s == Stage::RT ? topPlate_ : scene_.stageNodes()[static_cast<std::size_t>(s)];
            if (const Node* n = scene_.node(plate))
                return glm::dvec3(n->world[3]);
            return layout_.fridgePosition_m + glm::dvec3(0.0, stageHeight(s), 0.0);
        };

        std::vector<Stop> stops;
        std::vector<std::pair<std::size_t, const cryo::Element*>>
            pendingCoax; // stop index before → element
        int attnIndex = 0, isoIndex = 0, segIndex = 0;

        for (const auto& e : line.elements) {
            if (e.kind == ElementKind::CoaxSegment) {
                pendingCoax.emplace_back(stops.size(), &e);
                continue;
            }
            if (e.kind ==
                ElementKind::Chip) { // the chip end of the line: the sample puck bulkheads
                const Node* puck = scene_.node(puck_);
                glm::dvec3 at = puck ? glm::dvec3(puck->world[3])
                                     : glm::dvec3(0.0, stageHeight(Stage::MXC) - 0.2, 0.0);
                double r = 0.026;
                stops.push_back({Stage::MXC,
                                 at + glm::dvec3(r * std::cos(theta), 0.012, r * std::sin(theta)),
                                 0.0});
                continue;
            }
            auto mapped = mapElement(e);
            if (!mapped)
                continue;
            const ComponentDescriptor* d = scene_.catalog().find(mapped->descriptor);
            if (!d)
                return fail(kErrScene, std::format("wiring element '{}' needs component '{}'", e.id,
                                                   mapped->descriptor));
            glm::dvec3 slot = routing_.slot(mapped->slot);
            double len = alongLength(d);
            SlotKey key{static_cast<int>(e.stage), mapped->slot};
            double plateOffset = -0.5 * plateThickness(e.stage);
            double topY = plateOffset + slot.y;
            if (auto it = stackBottom.find(key); it != stackBottom.end())
                topY = std::min(topY, it->second - 0.006);
            double centreY = topY - 0.5 * len;
            stackBottom[key] = centreY - 0.5 * len;
            double r = e.kind == ElementKind::Bulkhead ? routing_.feedthroughRadius_m
                                                       : lineRadius(e.stage, slot.x);
            double ang =
                (e.kind == ElementKind::Bulkhead ? thetaRt : theta) + slot.z / std::max(r, 0.02);
            if (e.kind == ElementKind::Bulkhead)
                centreY = 0.0; // the feedthrough sits in the plate

            NodeSpec spec;
            spec.descriptor = mapped->descriptor;
            spec.instance = std::format("{}.{}", line.id, e.id);
            spec.group = Group::Wiring;
            spec.material = mapped->material;
            spec.local = Transform::at(r * std::cos(ang), centreY, r * std::sin(ang));
            spec.params.setIndex("line", static_cast<long long>(li));
            spec.params.setToken("stage", std::string(stageLayoutName(e.stage)));
            switch (e.kind) {
            case ElementKind::Attenuator:
                spec.params.setIndex("k", attnIndex++);
                spec.params.setNumber("A_dB", e.attenuation_dB);
                spec.display = std::format("Attenuator {:.0f} dB ({})", e.attenuation_dB,
                                           cryo::stageName(e.stage));
                break;
            case ElementKind::Isolator:
            case ElementKind::Circulator:
                spec.params.setIndex("k", isoIndex++);
                spec.display = std::format("{} ({})", d->name, cryo::stageName(e.stage));
                break;
            case ElementKind::LowPassFilter:
            case ElementKind::RcFilter:
                spec.params.setNumber("f_c", e.cutoff_Hz);
                spec.display = e.cutoff_Hz >= 1e9
                                   ? std::format("Low-pass filter {:.0f} GHz ({})",
                                                 e.cutoff_Hz / 1e9, cryo::stageName(e.stage))
                                   : std::format("Low-pass filter {:.0f} kHz ({})",
                                                 e.cutoff_Hz / 1e3, cryo::stageName(e.stage));
                break;
            default:
                spec.display = std::format("{} ({})", d->name, cryo::stageName(e.stage));
                break;
            }
            QXL_TRY_ASSIGN(ComponentId node, addComponent(groupFor(e.stage), std::move(spec)));
            if (e.kind == ElementKind::Attenuator)
                run.attenuators.emplace_back(node, e.attenuation_dB);
            // Every attenuator hangs in a copper clamp block bolted to the plate underside; every
            // HEMT gets its bias loom down to the plate (fridge detail pass, spec 17 §3.2).
            if (e.kind == ElementKind::Attenuator && scene_.catalog().contains("thermal_clamp")) {
                const double blockH = std::max(0.006, plateOffset - topY);
                NodeSpec block;
                block.descriptor = "thermal_clamp";
                block.instance = std::format("{}.{}.clamp", line.id, e.id);
                block.display =
                    std::format("Attenuator clamp block ({})", cryo::stageName(e.stage));
                block.group = Group::Wiring;
                block.material = "copper";
                block.local =
                    Transform::at(r * std::cos(ang), plateOffset - 0.5 * blockH, r * std::sin(ang));
                block.overrides = {{"w_m", 0.008},
                                   {"h_m", blockH},
                                   {"d_m", 0.012}}; // narrow bracket: lines sit ≈ 11 mm apart
                block.params.setIndex("line", static_cast<long long>(li));
                block.params.setToken("stage", std::string(stageLayoutName(e.stage)));
                QXL_TRY(addComponent(groupFor(e.stage), std::move(block)));
            }
            if (e.kind == ElementKind::Amplifier && scene_.catalog().contains("dc_loom")) {
                glm::dvec3 at{r * std::cos(ang), centreY, r * std::sin(ang)};
                core::Json pts = core::Json::array();
                for (glm::dvec3 p :
                     {at + glm::dvec3(0.0, 0.5 * len, 0.0),
                      at + glm::dvec3(0.01, 0.5 * (plateOffset + centreY + 0.5 * len), 0.01),
                      glm::dvec3(0.85 * at.x, plateOffset, 0.85 * at.z)})
                    pts.push_back({p.x, p.y, p.z});
                NodeSpec bias;
                bias.descriptor = "dc_loom";
                bias.instance = std::format("{}.{}.bias_loom", line.id, e.id);
                bias.display = std::format("HEMT bias loom ({})", cryo::stageName(e.stage));
                bias.group = Group::Wiring;
                bias.material = "phosphor_bronze";
                bias.overrides = {
                    {"points_m", pts}, {"r_m", 0.0008}, {"bundle", 4}, {"bundle_pitch_m", 0.0018}};
                bias.params.setIndex("line", static_cast<long long>(li));
                bias.params.setToken("stage", std::string(stageLayoutName(e.stage)));
                bias.cacheMesh = false;
                QXL_TRY(addComponent(groupFor(e.stage), std::move(bias)));
            }
            if (run.stageAnchor[static_cast<std::size_t>(e.stage)].value == 0)
                run.stageAnchor[static_cast<std::size_t>(e.stage)] = node;
            // World position of the element, for the spline endpoints of the adjoining coax runs.
            stops.push_back(
                {e.stage,
                 stageOrigin(e.stage) + glm::dvec3(r * std::cos(ang), centreY, r * std::sin(ang)),
                 0.5 * len});
        }

        // ---- coax runs between consecutive stops
        for (const auto& [beforeIndex, element] : pendingCoax) {
            if (beforeIndex == 0 || beforeIndex >= stops.size())
                continue;
            const Stop& A = stops[beforeIndex - 1];
            const Stop& B = stops[beforeIndex];
            bool descending = B.world.y < A.world.y;
            double dir = descending ? -1.0 : 1.0;
            glm::dvec3 a = A.world + glm::dvec3(0.0, dir * A.halfLength, 0.0);
            glm::dvec3 b = B.world - glm::dvec3(0.0, dir * B.halfLength, 0.0);
            // The run crosses one plate on its way: the destination plate going down, its own plate
            // going up. Control points stay monotone in y so the tube never doubles back.
            Stage crossed = descending ? B.stage : A.stage;
            glm::dvec3 crossOrigin = stageOrigin(crossed);
            double crossY = crossOrigin.y + 0.5 * plateThickness(crossed) + 0.015;
            glm::dvec3 centre = stageOrigin(A.stage);
            double rA = std::hypot(a.x - centre.x, a.z - centre.z),
                   rB = std::hypot(b.x - centre.x, b.z - centre.z);
            double angA = std::atan2(a.z - centre.z, a.x - centre.x),
                   angB = std::atan2(b.z - centre.z, b.x - centre.x);
            double rMid = 0.5 * (rA + rB),
                   angMid = angA + 0.5 * std::remainder(angB - angA, 2.0 * pi);
            double yMid = 0.5 * (a.y + b.y);
            SplineInfo spline;
            auto push = [&](glm::dvec3 world, Stage s) {
                spline.points.push_back({world, static_cast<int>(s)});
            };
            auto atRadius = [&](double r, double ang, double y, glm::dvec3 origin) {
                return glm::dvec3(origin.x + r * std::cos(ang), y, origin.z + r * std::sin(ang));
            };
            push(a, A.stage);
            push(a + glm::dvec3(0.0, dir * 0.02, 0.0), A.stage);
            if (descending) {
                push(atRadius(rMid, angMid, yMid, centre), A.stage);
                push(atRadius(rB, angB, crossY, crossOrigin), crossed);
            } else {
                push(atRadius(rA, angA, crossY, crossOrigin), crossed);
                push(atRadius(rMid, angMid, yMid, centre), B.stage);
            }
            push(b, B.stage);

            std::string coaxId = element->coax.empty() ? "SS_086" : element->coax;
            bool loom = coaxId.rfind("DC", 0) == 0;
            const char* descriptor = loom ? "dc_loom" : "coax_segment";
            ComponentId parent = groupFor(B.stage);
            glm::dvec3 origin = stageOrigin(B.stage);
            core::Json pts = core::Json::array();
            for (auto& anchor : spline.points) {
                anchor.restLocal -= origin; // express the control points in the node's local frame
                pts.push_back({anchor.restLocal.x, anchor.restLocal.y, anchor.restLocal.z});
            }
            NodeSpec spec;
            spec.descriptor = descriptor;
            spec.instance = std::format("{}.{}[{}]", line.id, element->id, segIndex);
            spec.display = std::format("{} {}→{}", loom ? "DC loom" : "Coax",
                                       cryo::stageName(A.stage), cryo::stageName(B.stage));
            spec.group = Group::Wiring;
            spec.material = std::string(coaxMaterial(coaxId));
            spec.overrides = {{"points_m", pts}, {"r_m", coaxRadius_m(coaxId)}};
            core::Json splineGeometry = {{"r_m", coaxRadius_m(coaxId)}};
            if (loom) { // 12-way phosphor-bronze ribbon (spec 17 §3.2): the descriptor's bundle
                const ComponentDescriptor* ld = scene_.catalog().find("dc_loom");
                spec.overrides["bundle"] = splineGeometry["bundle"] =
                    ld ? ld->geometryNumber("bundle", 12) : 12;
                spec.overrides["bundle_pitch_m"] = splineGeometry["bundle_pitch_m"] =
                    ld ? ld->geometryNumber("bundle_pitch_m", 0.0026) : 0.0026;
            }
            spec.params.setIndex("line", static_cast<long long>(li));
            spec.params.setIndex("j", segIndex);
            spec.params.setToken("stage", std::string(stageLayoutName(B.stage)));
            spec.params.setNumber("length_m", element->length_m);
            spec.cacheMesh = false;
            spline.geometry = std::move(splineGeometry);
            spec.spline = std::move(spline);
            QXL_TRY_ASSIGN(ComponentId node, addComponent(parent, std::move(spec)));
            run.segments.push_back(node);
            if (run.stageAnchor[static_cast<std::size_t>(B.stage)].value == 0)
                run.stageAnchor[static_cast<std::size_t>(B.stage)] = node;
            ++segIndex;
        }

        // ---- kind-specific extras: a circulator ahead of a JPA, a bias tee on flux lines,
        // a directional coupler on pump lines
        bool jpa =
            std::any_of(line.elements.begin(), line.elements.end(), [](const cryo::Element& e) {
                return e.kind == ElementKind::Preamp && e.variant == "jpa";
            });
        if (jpa && scene_.catalog().contains("circulator")) {
            // A JPA reflects: a circulator separates its input from its output (spec 11 §4.6).
            glm::dvec3 slot = routing_.slot("iso");
            double r = lineRadius(Stage::MXC, slot.x);
            SlotKey key{static_cast<int>(Stage::MXC), "iso"};
            double topY = stackBottom.count(key) ? stackBottom[key] - 0.006
                                                 : -0.5 * plateThickness(Stage::MXC) + slot.y;
            NodeSpec spec;
            spec.descriptor = "circulator";
            spec.instance = std::format("{}.circulator", line.id);
            spec.display = "Circulator (MXC)";
            spec.group = Group::Wiring;
            spec.material = "stainless";
            spec.local = Transform::at(r * std::cos(theta), topY - 0.012, r * std::sin(theta));
            spec.params.setIndex("line", static_cast<long long>(li));
            spec.params.setIndex("k", isoIndex++);
            spec.params.setToken("stage", "mxc");
            QXL_TRY(addComponent(groupFor(Stage::MXC), std::move(spec)));
        }
        if (line.kind == cryo::LineKind::Flux && scene_.catalog().contains("bias_tee")) {
            glm::dvec3 slot = routing_.slot("filter");
            double r = lineRadius(Stage::MXC, slot.x - 0.03);
            SlotKey key{static_cast<int>(Stage::MXC), "filter"};
            double topY = stackBottom.count(key) ? stackBottom[key] - 0.006
                                                 : -0.5 * plateThickness(Stage::MXC) + slot.y;
            NodeSpec spec;
            spec.descriptor = "bias_tee";
            spec.instance = std::format("{}.bias_tee", line.id);
            spec.display = "Bias tee (MXC)";
            spec.group = Group::Wiring;
            spec.material = "stainless";
            spec.local = Transform::at(r * std::cos(theta), topY - 0.006, r * std::sin(theta));
            spec.params.setIndex("line", static_cast<long long>(li));
            spec.params.setToken("stage", "mxc");
            QXL_TRY(addComponent(groupFor(Stage::MXC), std::move(spec)));
        }
        if (line.kind == cryo::LineKind::Pump && scene_.catalog().contains("directional_coupler")) {
            glm::dvec3 slot = routing_.slot("iso");
            double r = lineRadius(Stage::MXC, slot.x);
            double y = -0.5 * plateThickness(Stage::MXC) + slot.y - 0.03;
            NodeSpec coupler;
            coupler.descriptor = "directional_coupler";
            coupler.instance = std::format("{}.directional_coupler", line.id);
            coupler.display = "Directional coupler (pump injection, MXC)";
            coupler.group = Group::Wiring;
            coupler.material = "stainless";
            coupler.local = Transform::at(r * std::cos(theta), y, r * std::sin(theta));
            coupler.params.setIndex("line", static_cast<long long>(li));
            coupler.params.setToken("stage", "mxc");
            QXL_TRY_ASSIGN(ComponentId couplerId,
                           addComponent(groupFor(Stage::MXC), std::move(coupler)));
            if (!stops.empty() &&
                scene_.catalog().contains("pump_line")) { // last element → coupler
                const Stop& last = stops.back();
                glm::dvec3 origin = stageOrigin(Stage::MXC);
                glm::dvec3 from = last.world - glm::dvec3(0.0, last.halfLength, 0.0) - origin;
                glm::dvec3 to = glm::dvec3(scene_.node(couplerId)->local.translation) +
                                glm::dvec3(0.0, 0.006, 0.0);
                core::Json pts = core::Json::array();
                for (glm::dvec3 p :
                     {from, glm::dvec3(0.5 * (from + to) + glm::dvec3(0.0, -0.01, 0.0)), to})
                    pts.push_back({p.x, p.y, p.z});
                NodeSpec tube;
                tube.descriptor = "pump_line";
                tube.instance = std::format("{}.pump_line", line.id);
                tube.display = "Pump line (MXC)";
                tube.group = Group::Wiring;
                tube.material = "nbti";
                tube.overrides = {{"points_m", pts}};
                tube.params.setIndex("line", static_cast<long long>(li));
                tube.cacheMesh = false;
                QXL_TRY(addComponent(groupFor(Stage::MXC), std::move(tube)));
            }
        }
        scene_.wiringRuns().push_back(std::move(run));
    }
    return {};
}

} // namespace qlab::lab
