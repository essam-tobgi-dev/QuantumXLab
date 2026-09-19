// Spec 17 §8 — live overlays read their bindings, carry a fidelity class and hide when unbound.
#include "Data/Fidelity.hpp"
#include "Lab/Lab.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <map>

using namespace qlab;
using namespace qlab::lab;

namespace {
Scene build() {
    auto s = buildScene("sc_lab_standard");
    if (!s)
        FAIL(s.error().format());
    return std::move(*s);
}

// Nominal stage temperatures of spec 11 §1 served as a cryo snapshot would be.
std::optional<BindingValue> cryoProvider(std::string_view path) {
    static const std::map<std::string, double, std::less<>> kT{
        {"stage.rt.T", 293.0},   {"stage.s50.T", 45.0}, {"stage.s4.T", 3.5},
        {"stage.still.T", 0.85}, {"stage.cp.T", 0.10},  {"stage.mxc.T", 0.015}};
    if (auto it = kT.find(path); it != kT.end())
        return BindingValue::number(it->second, "K", data::FidelityClass::Numerical);
    return std::nullopt;
}
} // namespace

TEST_CASE("the stage colormap is a log scale over 10 mK … 300 K") {
    CHECK(LabOverlays::temperatureToColormap(0.01) == Catch::Approx(0.0));
    CHECK(LabOverlays::temperatureToColormap(300.0) == Catch::Approx(1.0));
    CHECK(LabOverlays::temperatureToColormap(std::sqrt(0.01 * 300.0)) ==
          Catch::Approx(0.5).margin(1e-6));
    CHECK(LabOverlays::temperatureToColormap(0.001) == Catch::Approx(0.0)); // clamped
    CHECK(LabOverlays::temperatureToColormap(1e4) == Catch::Approx(1.0));
    double previous = -1.0;
    for (double T : {0.015, 0.1, 0.85, 3.5, 45.0, 293.0}) {
        double t = LabOverlays::temperatureToColormap(T);
        CHECK(t > previous);
        previous = t;
    }
}

TEST_CASE("stage plates and shields tint from cryo.stage.*.T with the Numerical class") {
    Scene scene = build();
    BindingRegistry registry;
    LabOverlays overlays(scene, registry);
    overlays.setTemperatureTint(true); // opt-in (spec 17 §8)
    overlays.update(0.0);
    CHECK(overlays.stageTemperatures().empty()); // no provider: no tint (spec 17 §5)
    CHECK(LabOverlays(scene, registry).temperatureTint() == false); // and off by default
    CHECK(overlays.visuals().albedo.empty());

    registry.registerProvider(BindingRoot::Cryo, cryoProvider);
    overlays.update(0.0);
    REQUIRE(overlays.stageTemperatures().size() >= 6);
    // Only parts AT a stage temperature are tinted: the thermometry bridge on the frame shows the
    // MXC reading on its spec sheet but sits at room temperature (it rendered inferno-purple).
    if (const ComponentId bridge = scene.findByInstance("rt_electronics_box"); bridge.value != 0)
        for (const auto& st : overlays.stageTemperatures())
            CHECK(st.node != bridge);
    for (const auto& st : overlays.stageTemperatures()) {
        const Node* tinted = scene.node(st.node);
        REQUIRE(tinted != nullptr);
        const ComponentDescriptor* d = scene.descriptor(*tinted);
        REQUIRE(d != nullptr);
        bool own = false;
        for (const SpecRow& row : d->specSheet)
            if (row.binding && (row.field == "temperature" || row.field == "nominal temperature"))
                own = true;
        CHECK(own);
    }
    std::map<cryo::Stage, double> byStage;
    for (const auto& st : overlays.stageTemperatures()) {
        INFO(scene.node(st.node)->instanceName);
        // the displayed class is the weaker of the descriptor row's and the provider's
        // (spec 00 §5): the cold plates declare Numerical, the RT plate only Model
        const SpecRow* row = nullptr;
        for (const auto& r : scene.descriptor(*scene.node(st.node))->specSheet)
            if (r.binding && r.binding->ends_with(".T"))
                row = &r;
        REQUIRE(row != nullptr);
        CHECK(st.cls == data::weakest(row->cls, data::FidelityClass::Numerical));
        CHECK(overlays.visuals().albedo.count(st.node.value) == 1);
        byStage[st.stage] = st.T_K;
        CHECK(st.colormapT == Catch::Approx(LabOverlays::temperatureToColormap(st.T_K)));
    }
    CHECK(byStage[cryo::Stage::MXC] == Catch::Approx(0.015));
    CHECK(byStage[cryo::Stage::RT] == Catch::Approx(293.0));
    for (const auto& st : overlays.stageTemperatures())
        if (st.stage == cryo::Stage::MXC && scene.node(st.node)->descriptorId == "stage_mxc")
            CHECK(st.cls == data::FidelityClass::Numerical);
    // the plates, their shields and the cold finger all take the tint of their stage
    ComponentId mxc = scene.stageNodes()[5];
    CHECK(overlays.visuals().albedo.count(mxc.value) == 1);
    CHECK(overlays.visuals().albedo.count(scene.findByDescriptor("shield_4K").front().value) == 1);
    CHECK(overlays.visuals().albedo.count(scene.findByDescriptor("cold_finger").front().value) ==
          1);
    // the mixing chamber is the cold end of the colormap
    glm::vec4 cold = overlays.visuals().albedo.at(mxc.value);
    glm::vec4 warm = overlays.visuals().albedo.at(scene.stageNodes()[0].value);
    CHECK(cold != warm);
    const auto& legend = overlays.temperatureLegend();
    CHECK(legend.colormap == gfx::ColormapId::Inferno);
    CHECK(legend.logScale);
    CHECK(legend.min == Catch::Approx(0.01));
    CHECK(legend.max == Catch::Approx(300.0));
    CHECK(legend.ticks.size() == 6);
    CHECK(legend.cls == data::FidelityClass::Numerical);
}

TEST_CASE("Bloch mini-spheres are Simulator-only and hide when the probe is off") {
    Scene scene = build();
    BindingRegistry registry;
    LabOverlays overlays(scene, registry);
    overlays.update(0.0);
    CHECK(overlays.blochMarkers().empty()); // probes disabled → the overlay hides (spec 17 §8)

    registry.registerProvider(
        BindingRoot::Device, [](std::string_view path) -> std::optional<BindingValue> {
            // q[0] is fully polarised along +z, q[1] is a partly mixed state, q[2]'s probe reads
            // NaN
            if (path == "qubit[0].bloch[0]" || path == "qubit[0].bloch[1]")
                return BindingValue::number(0.0, {}, data::FidelityClass::Exact);
            if (path == "qubit[0].bloch[2]")
                return BindingValue::number(1.0, {}, data::FidelityClass::Exact);
            if (path == "qubit[1].bloch[0]")
                return BindingValue::number(0.3, {}, data::FidelityClass::Numerical);
            if (path == "qubit[1].bloch[1]")
                return BindingValue::number(0.0, {}, data::FidelityClass::Numerical);
            if (path == "qubit[1].bloch[2]")
                return BindingValue::number(0.4, {}, data::FidelityClass::Numerical);
            if (path == "qubit[1].purity")
                return BindingValue::number(0.62);
            if (path == "qubit[2].bloch[0]")
                return BindingValue::number(std::numeric_limits<double>::quiet_NaN());
            if (path == "qubit[0].pop_e")
                return BindingValue::number(0.75);
            if (path == "res[3].n_photons")
                return BindingValue::number(4.0, {}, data::FidelityClass::Numerical);
            if (path == "res[4].n_photons")
                return BindingValue::number(0.0, {}, data::FidelityClass::Numerical);
            return std::nullopt;
        });
    overlays.update(0.0);
    REQUIRE(overlays.blochMarkers().size() == 2); // q[2] stays hidden: its binding reads NaN
    const BlochMarker& a = overlays.blochMarkers()[0];
    CHECK(a.qubit == 0);
    CHECK(a.simulatorOnly);
    CHECK(a.cls == data::FidelityClass::Exact);
    CHECK(a.vector.z == Catch::Approx(1.0));
    CHECK(a.purity == Catch::Approx(1.0));
    CHECK(a.radius_m == Catch::Approx(2e-4)); // 0.4 mm sphere
    const Node* pad = scene.node(scene.qubitNodes()[0]);
    CHECK(a.center.y - pad->world[3].y == Catch::Approx(6e-4)); // 0.6 mm above the pad
    CHECK(glm::length(glm::dvec2(a.center.x - pad->world[3].x, a.center.z - pad->world[3].z)) <
          1e-9);
    const BlochMarker& b = overlays.blochMarkers()[1];
    CHECK(b.cls == data::FidelityClass::Numerical); // a Lindblad probe is weaker than Exact
    CHECK(b.purity == Catch::Approx(0.62));

    // excited population drives the pad's emissive intensity (physical class, probes off)
    CHECK(overlays.visuals().emissive.at(scene.qubitNodes()[0].value) == Catch::Approx(3.0 * 0.75));
    // resonator ring-up: Illustrative glow with a Numerical intensity
    REQUIRE(overlays.resonatorGlows().size() == 2);
    const ResonatorGlow* glow = nullptr;
    for (const auto& g : overlays.resonatorGlows())
        if (g.qubit == 3)
            glow = &g;
    REQUIRE(glow != nullptr);
    CHECK(glow->cls == data::FidelityClass::Illustrative);
    CHECK(glow->intensityCls == data::FidelityClass::Numerical);
    CHECK(glow->photons == Catch::Approx(4.0));
    CHECK(glow->intensity == Catch::Approx(std::log10(5.0) / std::log10(21.0)));
    CHECK(overlays.visuals().emissive.at(glow->node.value) > 0.0f);
    for (const auto& g : overlays.resonatorGlows())
        if (g.qubit == 4)
            CHECK(g.intensity == Catch::Approx(0.0)); // an empty resonator does not glow
}

TEST_CASE("pulse packets travel down the drive line at the coax velocity") {
    Scene scene = build();
    BindingRegistry registry;
    LabOverlays overlays(scene, registry);
    overlays.setTimeDilation(1e7);
    // v = c / sqrt(2.1) for a PTFE-dielectric coax
    const double v = LabOverlays::coaxVelocity_mps();
    CHECK(v == Catch::Approx(299792458.0 / std::sqrt(2.1)));
    // dilated wall-clock time for the packet to reach 0.30 m from the bulkhead
    const double wall = 0.30 / v * 1e7;
    registry.registerProvider(
        BindingRoot::Run, [wall](std::string_view path) -> std::optional<BindingValue> {
            if (path == "schedule.line[0].t_s")
                return BindingValue::number(wall, "s", data::FidelityClass::Illustrative);
            if (path == "schedule.line[0].amp")
                return BindingValue::number(1.0, {}, data::FidelityClass::Illustrative);
            if (path == "schedule.line[0].sigma_s")
                return BindingValue::number(2e-8);
            return std::nullopt;
        });
    overlays.update(0.0);
    REQUIRE(overlays.pulsePackets().size() == 1);
    const PulsePacket& p = overlays.pulsePackets().front();
    CHECK(p.lineIndex == 0);
    CHECK(p.cls == data::FidelityClass::Illustrative);
    // 0.30 m into a line whose first runs are RT→50 K (0.25 m) and 50 K→4 K: the packet is below
    // the 50 K plate and has passed no attenuator yet (the first one sits at 4 K)
    const Node* rt = scene.node(scene.stageNodes()[0]);
    const Node* s50 = scene.node(scene.stageNodes()[1]);
    const Node* s4 = scene.node(scene.stageNodes()[2]);
    CHECK(p.position.y < rt->world[3].y);
    CHECK(p.position.y < s50->world[3].y);
    CHECK(p.position.y > s4->world[3].y);
    CHECK(p.amplitude == Catch::Approx(1.0));
    CHECK(p.sigma_m == Catch::Approx(0.2)); // clamped: a 20 ns pulse is 4 m of coax
    // further down, the packet has passed the 20 dB attenuator at 4 K
    BindingRegistry late;
    double deep = 0.60 / v * 1e7;
    late.registerProvider(BindingRoot::Run,
                          [deep](std::string_view path) -> std::optional<BindingValue> {
                              if (path == "schedule.line[0].t_s")
                                  return BindingValue::number(deep);
                              if (path == "schedule.line[0].amp")
                                  return BindingValue::number(1.0);
                              return std::nullopt;
                          });
    LabOverlays deeper(scene, late);
    deeper.update(0.0);
    REQUIRE(deeper.pulsePackets().size() == 1);
    CHECK(deeper.pulsePackets().front().amplitude == Catch::Approx(0.1)); // −20 dB
    CHECK(deeper.pulsePackets().front().position.y < p.position.y);
    // no schedule, no packets
    BindingRegistry idle;
    LabOverlays quiet(scene, idle);
    quiet.update(0.0);
    CHECK(quiet.pulsePackets().empty());
}
