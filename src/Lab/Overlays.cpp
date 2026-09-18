// Spec 17 §8 — live overlays: stage-temperature colormap with legend, pulse packets on the coax,
// Bloch mini-spheres (Simulator-only), resonator ring-up and excited population.
#include "Lab/MeshOps.hpp"
#include "Data/Fidelity.hpp"
#include "Lab/Overlays.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::lab {

namespace {
constexpr double kBlochLift_m = 6e-4;      // 0.6 mm above the pad (spec 17 §8)
constexpr double kPhotonReference = 20.0;  // ring-up normalisation (Illustrative)

// World polyline of a wiring spline node.
std::vector<glm::dvec3> worldPath(const Scene& scene, ComponentId id) {
    std::vector<glm::dvec3> out;
    const Node* n = scene.node(id);
    if (!n || !n->spline) return out;
    std::vector<glm::dvec3> control;
    for (const auto& a : n->spline->points) control.push_back(glm::dvec3(n->world * glm::dvec4(a.restLocal, 1.0)));
    return mesh::catmullRom(control, 6);
}
} // namespace

LabOverlays::LabOverlays(const Scene& scene, const BindingRegistry& bindings) : scene_(&scene), bindings_(&bindings) {
    legend_.ticks = {{0.01, "10 mK"}, {0.1, "100 mK"}, {1.0, "1 K"}, {10.0, "10 K"}, {100.0, "100 K"}, {300.0, "300 K"}};
}

void LabOverlays::setEnabled(bool temperature, bool pulses, bool bloch, bool glow) {
    onTemperature_ = temperature;
    onPulses_ = pulses;
    onBloch_ = bloch;
    onGlow_ = glow;
}

float LabOverlays::temperatureToColormap(double T_K, double min, double max) {
    if (!(T_K > 0.0)) return 0.0f;
    double t = (std::log10(T_K) - std::log10(min)) / (std::log10(max) - std::log10(min));
    return static_cast<float>(std::clamp(t, 0.0, 1.0));
}

void LabOverlays::update(double timeS) {
    stages_.clear();
    packets_.clear();
    bloch_.clear();
    glows_.clear();
    visuals_.clear();

    // ---- stage temperature tint: every node whose OWN temperature is cryo.stage.<s>.T (plates,
    // shields, finger — the row is called "temperature"). A part that merely DISPLAYS a stage
    // temperature (the thermometry bridge on the frame reads the MXC) keeps its own colour.
    if (onTemperature_) {
        const gfx::Colormap& map = gfx::Colormap::get(gfx::ColormapId::Inferno);
        for (const Node& n : scene_->nodes()) {
            for (const Binding& b : n.bindings) {
                if (b.root != BindingRoot::Cryo || !b.path.starts_with("stage.") || !b.path.ends_with(".T")) continue;
                if (b.field != "temperature" && b.field != "nominal temperature") continue;
                auto value = bindings_->resolve(b);
                if (!value || !value->available() || !value->isNumber()) break;
                StageTemperature st;
                st.node = n.id;
                std::string name = b.path.substr(6, b.path.size() - 8);
                cryo::Stage stage = cryo::Stage::RT;
                stageFromLayoutName(name, stage);
                st.stage = stage;
                st.T_K = value->asNumber();
                st.colormapT = temperatureToColormap(st.T_K, legend_.min, legend_.max);
                st.color = map.sample(st.colormapT);
                st.cls = value->cls;
                visuals_.albedo[n.id.value] = glm::vec4(st.color, 1.0f);
                stages_.push_back(st);
                break;
            }
        }
    }

    // ---- pulse packets along the input lines (Illustrative)
    if (onPulses_)
        for (const auto& run : scene_->wiringRuns()) {
            if (run.kind == cryo::LineKind::ReadoutOut || run.kind == cryo::LineKind::DC) continue;
            std::string base = std::format("schedule.line[{}]", run.lineIndex);
            auto t = bindings_->query(BindingRoot::Run, base + ".t_s");
            auto amp = bindings_->query(BindingRoot::Run, base + ".amp");
            if (!t || !t->available() || !amp || !amp->available()) continue;
            double travelled = coaxVelocity_mps() * t->asNumber() / dilation_;
            if (travelled < 0.0) continue;
            double consumed = 0.0;
            bool placed = false;
            for (ComponentId seg : run.segments) {
                std::vector<glm::dvec3> path = worldPath(*scene_, seg);
                double length = mesh::polylineLength(path);
                if (travelled > consumed + length) {
                    consumed += length;
                    continue;
                }
                PulsePacket p;
                p.lineIndex = run.lineIndex;
                p.segment = seg;
                p.position = mesh::pointAt(path, travelled - consumed);
                double attenuation = 0.0; // every attenuator the packet has passed (spec 11 §4.1)
                for (const auto& [node, dB] : run.attenuators)
                    if (const Node* an = scene_->node(node); an && an->world[3].y > p.position.y) attenuation += dB;
                p.amplitude = amp->asNumber() * std::pow(10.0, -attenuation / 20.0);
                if (auto sigma = bindings_->query(BindingRoot::Run, base + ".sigma_s"); sigma && sigma->available())
                    p.sigma_m = std::clamp(coaxVelocity_mps() * sigma->asNumber(), 0.005, 0.2);
                packets_.push_back(p);
                placed = true;
                break;
            }
            (void)placed;
        }

    // ---- Bloch mini-spheres and excited population on the pads
    for (std::size_t q = 0; q < scene_->qubitNodes().size(); ++q) {
        const Node* pad = scene_->node(scene_->qubitNodes()[q]);
        if (!pad) continue;
        if (onBloch_) {
            std::string base = std::format("qubit[{}]", q);
            auto x = bindings_->query(BindingRoot::Device, base + ".bloch[0]");
            auto y = bindings_->query(BindingRoot::Device, base + ".bloch[1]");
            auto z = bindings_->query(BindingRoot::Device, base + ".bloch[2]");
            // Simulator-only: with probes disabled the binding reads NaN and the overlay hides.
            if (x && y && z && x->available() && y->available() && z->available()) {
                BlochMarker m;
                m.qubit = static_cast<std::uint32_t>(q);
                m.node = pad->id;
                m.center = glm::dvec3(pad->world[3]) + glm::dvec3(0.0, kBlochLift_m, 0.0);
                m.vector = {x->asNumber(), y->asNumber(), z->asNumber()};
                // the marker is only as strong as the weakest component (spec 00 §5)
                m.cls = data::weakest(data::weakest(x->cls, y->cls), z->cls);
                auto purity = bindings_->query(BindingRoot::Device, base + ".purity");
                m.purity = purity && purity->available() ? std::clamp(purity->asNumber(), 0.0, 1.0)
                                                         : std::clamp(glm::length(m.vector), 0.0, 1.0);
                bloch_.push_back(m);
            }
        }
        auto pop = bindings_->query(BindingRoot::Device, std::format("qubit[{}].pop_e", q));
        if (pop && pop->available())
            visuals_.emissive[pad->id.value] = static_cast<float>(3.0 * std::clamp(pop->asNumber(), 0.0, 1.0));
    }

    // ---- resonator ring-up
    if (onGlow_)
        for (std::size_t q = 0; q < scene_->resonatorNodes().size(); ++q) {
            const Node* res = scene_->node(scene_->resonatorNodes()[q]);
            if (!res) continue;
            auto n = bindings_->query(BindingRoot::Device, std::format("res[{}].n_photons", q));
            if (!n || !n->available()) continue;
            ResonatorGlow g;
            g.qubit = static_cast<std::uint32_t>(q);
            g.node = res->id;
            g.photons = std::max(0.0, n->asNumber());
            g.intensity = static_cast<float>(std::clamp(std::log10(1.0 + g.photons) / std::log10(1.0 + kPhotonReference), 0.0, 1.0));
            g.intensityCls = n->cls;
            glows_.push_back(g);
            visuals_.emissive[res->id.value] = 5.0f * g.intensity;
        }
    (void)timeS;
}

} // namespace qlab::lab
