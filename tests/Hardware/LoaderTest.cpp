// Spec 09 §1–§4, §10 — every shipped device loads, validates and cross-references.
#include "Hardware/Hardware.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>

using namespace qlab;
using namespace qlab::hw;
using Catch::Approx;

namespace {
struct Expect { std::size_t qubits, edges; Technology tech; };
const std::map<std::string, Expect> kShipped = {
    {"sc_fixed_5", {5, 4, Technology::TransmonFixed}},
    {"sc_heavyhex_27", {27, 28, Technology::TransmonFixed}},
    {"sc_heavyhex_127", {127, 144, Technology::TransmonFixed}},
    {"sc_tunable_grid_54", {147, 93, Technology::TransmonTunableCoupler}},
    {"ion_chain_11", {11, 1, Technology::IonChain}},
    {"ion_chain_32", {32, 1, Technology::IonChain}},
};
} // namespace

TEST_CASE("every shipped device loads with the generated counts") {
    auto ids = shippedDeviceIds();
    for (const auto& [id, exp] : kShipped) {
        INFO("device " << id);
        REQUIRE(std::find(ids.begin(), ids.end(), id) != ids.end());
        auto ld = loadShippedDevice(id);
        if (!ld) FAIL(ld.error().format());
        REQUIRE(ld->device.id == id);
        REQUIRE(ld->device.technology == exp.tech);
        REQUIRE(ld->device.qubitCount() == exp.qubits);
        REQUIRE(ld->device.edges.size() == exp.edges);
        REQUIRE(ld->calibration.device == id);
        REQUIRE(!ld->calibration.timestamp.empty());
        REQUIRE(ld->calibration.qubits.size() == exp.qubits);
    }
}

TEST_CASE("readout assignment matrices are row-stochastic") {
    for (const auto& [id, exp] : kShipped) {
        (void)exp;
        auto ld = loadShippedDevice(id);
        REQUIRE(ld);
        for (std::size_t q = 0; q < ld->calibration.qubits.size(); ++q) {
            const auto& m = ld->calibration.qubits[q].readoutAssignment;
            INFO(id << " qubit " << q);
            REQUIRE(m[0][0] + m[0][1] == Approx(1.0).margin(1e-12));
            REQUIRE(m[1][0] + m[1][1] == Approx(1.0).margin(1e-12));
            REQUIRE(m[0][0] >= 0.0);
            REQUIRE(m[1][1] >= 0.0);
        }
    }
}

TEST_CASE("coherence-time ordering holds on every shipped device") {
    for (const auto& [id, exp] : kShipped) {
        (void)exp;
        auto ld = loadShippedDevice(id);
        REQUIRE(ld);
        for (std::size_t q = 0; q < ld->calibration.qubits.size(); ++q) {
            const auto& c = ld->calibration.qubits[q];
            INFO(id << " qubit " << q);
            REQUIRE(c.t2star.value.v <= c.t2echo.value.v * (1.0 + 1e-9));
            REQUIRE(c.t2echo.value.v <= 2.0 * c.t1.value.v * (1.0 + 1e-9));
            REQUIRE(c.tphi().v > 0.0);
        }
    }
}

TEST_CASE("shipped transmon devices are free of spec 09 §6 collisions") {
    for (const auto& id : {"sc_fixed_5", "sc_heavyhex_27", "sc_heavyhex_127", "sc_tunable_grid_54"}) {
        auto ld = loadShippedDevice(id);
        REQUIRE(ld);
        FrequencyPlan plan(ld->device, ld->calibration);
        auto hard = plan.hardCollisions();
        INFO(id << " reported " << hard.size() << " hard collisions"
                << (hard.empty() ? "" : ": " + hard.front().message));
        REQUIRE(hard.empty());
    }
}

TEST_CASE("the advisory plan guideline is a warning, not a load failure") {
    // gencal's per-qubit scatter leaves three coupled pairs on the 127-qubit lattice below the
    // device's declared 50 MHz neighbour guideline; spec 09 §6 reports these with class Model.
    auto ld = loadShippedDevice("sc_heavyhex_127");
    REQUIRE(ld);
    FrequencyPlan plan(ld->device, ld->calibration);
    auto all = plan.check();
    REQUIRE(all.size() == 3);
    for (const auto& c : all) {
        REQUIRE(c.kind == CollisionKind::NeighbourDetuning);
        REQUIRE_FALSE(c.hard);
        REQUIRE(c.detuning.v < c.threshold.v);
    }
    REQUIRE(ld->warnings.size() >= 3);
}

TEST_CASE("frequency plan detects each hard collision rule") {
    auto ld = loadShippedDevice("sc_fixed_5");
    REQUIRE(ld);
    Calibration cal = ld->calibration;
    SECTION("degenerate neighbours") {
        cal.qubits[1].f01 = cal.qubits[0].f01; // edge 0-1 becomes degenerate
        FrequencyPlan plan(ld->device, cal);
        auto hard = plan.hardCollisions();
        REQUIRE(!hard.empty());
        bool found = false;
        for (const auto& c : hard)
            if (c.kind == CollisionKind::Degenerate) found = true;
        REQUIRE(found);
    }
    SECTION("cross-resonance detuning too large") {
        cal.qubits[1].f01.value = units::Frequency(cal.qubits[0].f01.value.v + 400e6);
        FrequencyPlan plan(ld->device, cal);
        bool found = false;
        for (const auto& c : plan.hardCollisions())
            if (c.kind == CollisionKind::CrTooSlow) found = true;
        REQUIRE(found);
    }
    SECTION("straddling the |0>-|2>/2 transition") {
        // f_j = f_i + alpha_i/2 puts the pair on the straddle resonance.
        cal.qubits[1].f01.value =
            units::Frequency(cal.qubits[0].f01.value.v + 0.5 * cal.qubits[0].anharmonicity.value.v);
        FrequencyPlan plan(ld->device, cal);
        bool found = false;
        for (const auto& c : plan.hardCollisions())
            if (c.kind == CollisionKind::Straddle) found = true;
        REQUIRE(found);
    }
}

TEST_CASE("calibration triples carry sigma and provenance") {
    auto ld = loadShippedDevice("sc_fixed_5");
    REQUIRE(ld);
    const auto& q0 = ld->calibration.qubits[0];
    REQUIRE(q0.f01.value.v == Approx(4.8e9).epsilon(1e-6));
    REQUIRE(q0.f01.sigma.v > 0.0);
    REQUIRE(q0.f01.source == "qubit_spectroscopy");
    REQUIRE(q0.t1.source == "t1");
    REQUIRE(q0.gateError1q.source == "rb_1q");
    REQUIRE(q0.anharmonicity.value.v < 0.0);
    REQUIRE(q0.duration1q.value.v == Approx(32e-9));
    REQUIRE(q0.coherentErrorFraction1q.isDefault());
}

TEST_CASE("tunable-grid couplers and edge coupler references load") {
    auto ld = loadShippedDevice("sc_tunable_grid_54");
    REQUIRE(ld);
    const Device& d = ld->device;
    REQUIRE(d.dataQubitCount() == 54);
    std::size_t couplers = 0;
    for (const auto& q : d.qubits)
        if (q.kind == QubitKind::Coupler) ++couplers;
    REQUIRE(couplers == 93);
    REQUIRE(d.isCoupler(54));
    REQUIRE_FALSE(d.isCoupler(0));
    for (const auto& e : d.edges) REQUIRE(e.coupler.has_value());
    const auto* ec = ld->calibration.edge(d.edges[0].a, d.edges[0].b);
    REQUIRE(ec);
    REQUIRE(ec->nativeGate == "cz");
    REQUIRE(ec->coupler.has_value());
    REQUIRE(ec->extraGates.count("siswap") == 1);
    REQUIRE(ec->extraGates.at("siswap").second.value.v == Approx(32e-9));
}

TEST_CASE("ion devices expose all-to-all coupling and the motional block") {
    auto ld = loadShippedDevice("ion_chain_11");
    REQUIRE(ld);
    const Device& d = ld->device;
    REQUIRE(d.allToAll);
    REQUIRE(d.adjacent(0, 7));
    REQUIRE(d.distance(0, 10) == 1);
    REQUIRE(d.quditDimension == 2);
    REQUIRE(d.ion.has_value());
    REQUIRE(d.ion->species == "171Yb+");
    REQUIRE(d.motionalModes.has_value());
    REQUIRE(ld->calibration.motional.has_value());
    const auto& mo = *ld->calibration.motional;
    REQUIRE(mo.equilibriumPositions.size() == 11);
    REQUIRE(mo.axialModes.size() == 11);
    REQUIRE(mo.axialModes[0].v == Approx(0.3e6).epsilon(1e-6));
    REQUIRE(ld->calibration.qubits[0].lambDicke.has_value());
    // all-to-all: every distinct pair has a calibration entry
    REQUIRE(ld->calibration.edges.size() == 55);
}

TEST_CASE("loaders name missing required fields and reject bad cross-references") {
    core::Json d = {{"id", "x"}, {"technology", "transmon_fixed"}};
    auto bad = parseDevice(d);
    REQUIRE_FALSE(bad);
    const std::string msg = bad.error().format();
    INFO(msg);
    REQUIRE(msg.find("qubits") != std::string::npos);

    auto ld = loadShippedDevice("sc_fixed_5");
    REQUIRE(ld);
    core::Json cal = calibrationToJson(ld->calibration);
    cal["qubits"]["99"] = cal["qubits"]["0"];
    auto r = parseCalibration(cal, ld->device);
    REQUIRE_FALSE(r);
    REQUIRE(r.error().format().find("99") != std::string::npos);

    REQUIRE_FALSE(loadShippedDevice("no_such_device").has_value());
}

TEST_CASE("unknown fields are tolerated") {
    auto ld = loadShippedDevice("sc_fixed_5");
    REQUIRE(ld);
    core::Json d = deviceToJson(ld->device);
    d["future_field"] = 42;
    d["qubits"][0]["mystery"] = "ok";
    REQUIRE(parseDevice(d).has_value());
}
