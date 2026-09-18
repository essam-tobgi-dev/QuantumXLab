#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Cryo/Cryo.hpp"
#include "Cryo/Physics.hpp"
#include "Core/Paths.hpp"
#include <cmath>
using namespace qlab;
using namespace qlab::cryo;
using Catch::Approx;

namespace {
const MaterialCatalog& mats() {
    static MaterialCatalog m = [] { auto r = MaterialCatalog::load(); REQUIRE(r); return *r; }();
    return m;
}
Wiring referenceWiring() {
    auto w = loadWiring(core::assetDir() / "Devices/_template/wiring.json");
    REQUIRE(w);
    return *w;
}
} // namespace

TEST_CASE("material catalog loads and conductivity integrals are physical") {
    const auto& m = mats();
    REQUIRE(m.size() >= 10);
    auto ss = m.get("stainless_304"); REQUIRE(ss);
    // T08 §4 table: stainless 304 Θ(4 K → 300 K) ≈ 3.06e3 W/m (NIST fit); accept ±25 %.
    double theta = (*ss)->conductivityIntegral(4.0, 300.0);
    REQUIRE(theta == Approx(3060).epsilon(0.25));
    auto cu = m.get("Cu_OFHC_RRR100"); REQUIRE(cu);
    REQUIRE((*cu)->conductivityIntegral(4.0, 300.0) > 50 * theta); // copper ≫ stainless
    REQUIRE((*cu)->specificHeat(300.0) == Approx(385).epsilon(0.15));
    REQUIRE((*cu)->specificHeat(4.0) < 1.0);
}

TEST_CASE("coax conduction load per metre matches the T08 table order") {
    CoaxCatalog cc;
    auto ss = cc.get("SS_086"); REQUIRE(ss);
    auto q = conductionLoad(**ss, 1.0, 4.0, 300.0, mats());
    REQUIRE(q);
    // T08 §4.1: 0.86 mm stainless coax 300→4 K per metre ≈ 1–2 mW (inner+outer+PTFE); accept 0.5–4 mW.
    REQUIRE(q->total() > 0.5e-3);
    REQUIRE(q->total() < 4e-3);
    auto nbti = cc.get("NbTi_086"); REQUIRE(nbti);
    auto q2 = conductionLoad(**nbti, 0.1, 0.015, 0.1, mats());
    REQUIRE(q2);
    REQUIRE(q2->total() < 1e-7); // superconducting NbTi between CP and MXC is negligible
}

TEST_CASE("attenuator dissipation and thermal photon helpers") {
    REQUIRE(phys::attenuatorDissipation(1e-6, 20.0) == Approx(0.99e-6));
    REQUIRE(thermalPhotons(5e9, 293.0) == Approx(1220).epsilon(0.03)); // T07 §9 quotes 1250 for 300 K
    REQUIRE(thermalPhotons(5e9, 0.1) == Approx(0.0996).epsilon(0.02));
    REQUIRE(effectiveTemperature(5e9, thermalPhotons(5e9, 0.153)) == Approx(0.153).epsilon(1e-6));
}

TEST_CASE("reference wiring loads, validates, and has the standard chains") {
    auto w = referenceWiring();
    REQUIRE(validateWiring(w));
    auto counts = w.countByKind();
    REQUIRE(counts[LineKind::XY] == 27);
    REQUIRE(counts[LineKind::ReadoutIn] == 4);
    REQUIRE(counts[LineKind::ReadoutOut] == 4);
    const WiringLine* d = w.find("drive_q0"); REQUIRE(d);
    REQUIRE(d->totalAttenuation_dB() == Approx(40.0));
    REQUIRE(d->attenuationAt(Stage::PT2) == Approx(20.0));
    REQUIRE(d->attenuationAt(Stage::MXC) == Approx(20.0));
    auto ro = ChainCatalog::instantiate("readout_in_std", "ri", "m[0]"); REQUIRE(ro);
    REQUIRE(ro->totalAttenuation_dB() == Approx(70.0));
    auto d60 = ChainCatalog::instantiate("drive_60dB", "d60", "d[9]"); REQUIRE(d60);
    REQUIRE(d60->attenuationAt(Stage::CP) == Approx(20.0));
    REQUIRE(d60->totalAttenuation_dB() == Approx(60.0));
    // Round trip through the serializer.
    auto back = parseWiring(serializeWiring(w));
    REQUIRE(back);
    REQUIRE(back->lines.size() == w.lines.size());
}

TEST_CASE("wiring validation rejects misplaced elements") {
    auto w = referenceWiring();
    // Isolator on an input line.
    WiringLine bad = *w.find("drive_q1");
    Element iso; iso.kind = ElementKind::Isolator; iso.stage = Stage::MXC; iso.id = "iso_bad";
    bad.elements.insert(bad.elements.end() - 1, iso);
    Wiring w2; w2.lines = {bad};
    REQUIRE_FALSE(validateWiring(w2));
    // Attenuator on an output line.
    WiringLine out = *w.find("readout_out_f0");
    Element att; att.kind = ElementKind::Attenuator; att.stage = Stage::PT2; att.attenuation_dB = 20; att.id = "att_bad";
    out.elements.push_back(att);
    Wiring w3; w3.lines = {out};
    REQUIRE_FALSE(validateWiring(w3));
    // Non-standard attenuation value.
    WiringLine odd = *w.find("drive_q2");
    for (auto& e : odd.elements) if (e.kind == ElementKind::Attenuator) e.attenuation_dB = 17;
    Wiring w4; w4.lines = {odd};
    REQUIRE_FALSE(validateWiring(w4));
    // Duplicate channel.
    Wiring w5; w5.lines = {*w.find("drive_q3"), *w.find("drive_q3")};
    REQUIRE_FALSE(validateWiring(w5));
}
