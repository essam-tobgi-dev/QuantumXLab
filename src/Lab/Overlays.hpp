#pragma once
// Spec 17 §8 — live overlays. Every overlay carries the fidelity class of what it shows
// (spec 00 §5) and hides itself when its binding has no value (spec 17 §5).
#include "Graphics/Colormap.hpp"
#include "Data/Fidelity.hpp"
#include "Lab/BindingRegistry.hpp"
#include "Lab/Scene.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace qlab::lab {

// Per-node material overrides the renderer applies (albedo lerp for the stage colormap, emissive
// strength for ring-up and excited population).
struct OverlayVisuals {
    std::unordered_map<std::uint32_t, glm::vec4> albedo;
    std::unordered_map<std::uint32_t, float> emissive;
    void clear() {
        albedo.clear();
        emissive.clear();
    }
};

struct StageTemperature {
    ComponentId node{0};
    cryo::Stage stage = cryo::Stage::RT;
    double T_K = 0.0;
    float colormapT = 0.0f; // log position in the legend range
    glm::vec3 color{1.0f};
    data::FidelityClass cls = data::FidelityClass::Numerical;
};

struct ColormapLegend {
    gfx::ColormapId colormap = gfx::ColormapId::Inferno;
    double min = 0.01, max = 300.0; // K, log scale (spec 17 §8)
    bool logScale = true;
    std::string title = "Stage temperature";
    std::vector<std::pair<double, std::string>> ticks;
    data::FidelityClass cls = data::FidelityClass::Numerical;
};

// Illustrative Gaussian packet travelling down a drive line at v = c/sqrt(eps_r), scaled by the
// time-dilation slider; the amplitude carries the attenuation the packet has already passed.
struct PulsePacket {
    std::size_t lineIndex = 0;
    ComponentId segment{0};
    glm::dvec3 position{0.0};
    double amplitude = 1.0;   // relative to the launched amplitude
    double sigma_m = 0.05;
    data::FidelityClass cls = data::FidelityClass::Illustrative;
};

// Simulator-only Bloch marker above a transmon pad (spec 00 §6, 17 §8).
struct BlochMarker {
    std::uint32_t qubit = 0;
    ComponentId node{0};
    glm::dvec3 center{0.0};  // world, 0.6 mm above the pad
    double radius_m = 2e-4;  // 0.4 mm sphere
    glm::dvec3 vector{0.0, 0.0, 1.0};
    double purity = 1.0;     // sphere opacity
    data::FidelityClass cls = data::FidelityClass::Exact;
    bool simulatorOnly = true;
};

struct ResonatorGlow {
    std::uint32_t qubit = 0;
    ComponentId node{0};
    double photons = 0.0;
    float intensity = 0.0f; // normalised emissive strength
    data::FidelityClass cls = data::FidelityClass::Illustrative;
    data::FidelityClass intensityCls = data::FidelityClass::Numerical;
};

class LabOverlays {
public:
    LabOverlays(const Scene& scene, const BindingRegistry& bindings);

    // Reads every overlay binding once per frame (spec 17 §5).
    void update(double timeS);

    const std::vector<StageTemperature>& stageTemperatures() const { return stages_; }
    const ColormapLegend& temperatureLegend() const { return legend_; }
    const std::vector<PulsePacket>& pulsePackets() const { return packets_; }
    const std::vector<BlochMarker>& blochMarkers() const { return bloch_; }
    const std::vector<ResonatorGlow>& resonatorGlows() const { return glows_; }
    const OverlayVisuals& visuals() const { return visuals_; }

    void setTimeDilation(double factor) { dilation_ = std::max(1.0, factor); }
    double timeDilation() const { return dilation_; }
    void setEnabled(bool temperature, bool pulses, bool bloch, bool glow);
    // The stage-temperature tint paints plates and shields with the inferno colormap, hiding
    // their finish; it is opt-in (the viewport's `Temp` chip), off by default.
    void setTemperatureTint(bool on) { onTemperature_ = on; }
    bool temperatureTint() const { return onTemperature_; }

    // Log position of a temperature in the legend range 10 mK … 300 K (spec 17 §8).
    static float temperatureToColormap(double T_K, double min = 0.01, double max = 300.0);
    // Coax phase velocity v = c / sqrt(eps_r) with PTFE dielectric (eps_r = 2.1).
    static double coaxVelocity_mps() { return 299792458.0 / std::sqrt(2.1); }

private:
    const Scene* scene_;
    const BindingRegistry* bindings_;
    std::vector<StageTemperature> stages_;
    std::vector<PulsePacket> packets_;
    std::vector<BlochMarker> bloch_;
    std::vector<ResonatorGlow> glows_;
    ColormapLegend legend_;
    OverlayVisuals visuals_;
    double dilation_ = 1e7; // spec 17 §8 default time-dilation slider
    bool onTemperature_ = false, onPulses_ = true, onBloch_ = true, onGlow_ = true;
};

} // namespace qlab::lab
