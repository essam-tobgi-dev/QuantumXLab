// Spec 17 §4/§12 — the shipped component catalog loads, lints and resolves its references.
#include "Lab/Catalog.hpp"
#include "Core/Paths.hpp"
#include "Data/Fidelity.hpp"
#include "Lab/Generators.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <set>

using namespace qlab;
using namespace qlab::lab;

namespace {
const ComponentCatalog& catalog() {
    static ComponentCatalog cat = [] {
        auto c = ComponentCatalog::load();
        REQUIRE(c.has_value());
        return std::move(*c);
    }();
    return cat;
}
} // namespace

TEST_CASE("every shipped component descriptor loads and passes the spec 17 §4 lint") {
    const auto& cat = catalog();
    // one descriptor per Assets/Lab/Components/<id>/component.json
    std::size_t dirs = 0;
    for (const auto& e :
         std::filesystem::directory_iterator(core::assetDir() / "Lab" / "Components"))
        if (std::filesystem::exists(e.path() / "component.json"))
            ++dirs;
    REQUIRE(dirs == 108); // 97 + seven of the fridge detail pass (spec 17 §3.1 amended) + four of
                          // the rack detail pass (cable_loom, microscope, wire_bonder, sample_box)
    REQUIRE(cat.size() == dirs);
    for (const auto& d : cat.all()) {
        INFO(d.id);
        REQUIRE(ComponentCatalog::validate(d).has_value());
        REQUIRE_FALSE(d.name.empty());
        REQUIRE_FALSE(d.category.empty());
        REQUIRE_FALSE(d.physicsSummary.empty());
        REQUIRE_FALSE(d.specSheet.empty());
        REQUIRE_FALSE(d.lod.empty());
        REQUIRE(utf8Length(d.tooltip) <= 90);
        // spec 17 §1: every descriptor names a generator this module can build
        REQUIRE(hasGenerator(d.generator));
        // lod rules are sorted and end in a catch-all level
        for (std::size_t i = 1; i < d.lod.size(); ++i)
            REQUIRE(d.lod[i - 1].maxDistance_m <= d.lod[i].maxDistance_m);
    }
    // spec 17 §3 ids exist (sample across the five tables)
    for (const char* id : {"fridge_frame",  "top_plate_300K",    "ovc",        "stage_mxc",
                           "mag_shield_al", "coax_segment",      "attenuator", "hemt",
                           "twpa",          "mw_generator",      "digitizer",  "vna",
                           "ghs_cabinet",   "turbo_pump",        "substrate",  "transmon_pad",
                           "junction",      "readout_resonator", "feedline",   "airbridge",
                           "bond_pad",      "vacuum_chamber",    "trap_chip"})
        REQUIRE(cat.contains(id));
}

TEST_CASE("descriptor references resolve to equations.json and docs/theory") {
    auto problems =
        catalog().lintReferences(core::assetDir() / "Theory" / "equations.json",
                                 std::filesystem::path(QXL_SOURCE_DIR) / "docs" / "theory");
    for (const auto& p : problems)
        WARN(p);
    REQUIRE(problems.empty());
}

TEST_CASE("attenuator descriptor carries the spec 17 §4 example fields") {
    const ComponentDescriptor* d = catalog().find("attenuator");
    REQUIRE(d != nullptr);
    CHECK(d->category == "wiring");
    CHECK(d->generator == "CylinderSma");
    CHECK(d->geometryNumber("length_mm", 0.0) == 25.0);
    CHECK(d->equationIds.size() == 3);
    CHECK(d->specSheet.size() == 5);
    CHECK(d->specSheet[0].binding.value_or("") == "static.A_dB");
    CHECK(d->specSheet[0].unit == "dB");
    CHECK(d->specSheet[0].typical->second == 30.0);
    CHECK(d->firstLiveRow() == &d->specSheet[0]);
    CHECK_FALSE(d->simulatorOnly);
    // spec 17 §10: LOD by camera distance
    CHECK(d->detailAt(1.0) == Detail::Full);
    CHECK(d->detailAt(5.0) == Detail::Simple);
    CHECK(d->detailAt(100.0) == Detail::Hidden);
}

TEST_CASE("probe-only rows mark their descriptor Simulator-only (spec 00 §6)") {
    const ComponentDescriptor* pad = catalog().find("transmon_pad");
    REQUIRE(pad != nullptr);
    REQUIRE(pad->simulatorOnly);
    bool foundBloch = false;
    for (const auto& r : pad->specSheet)
        if (r.binding && r.binding->find(".bloch") != std::string::npos) {
            foundBloch = true;
            CHECK(r.simulatorOnly);
            CHECK(r.cls == data::FidelityClass::Exact);
        } else {
            CHECK_FALSE(r.simulatorOnly);
        }
    CHECK(foundBloch);
    // a fridge stage plate has no Simulator-only quantity
    CHECK_FALSE(catalog().find("stage_mxc")->simulatorOnly);
}

TEST_CASE("descriptor lint rejects malformed component.json") {
    core::Json j{{"id", "x"},
                 {"name", "X"},
                 {"category", "wiring"},
                 {"function", "too short"},
                 {"physics", {{"summary", "s"}}},
                 {"spec_sheet", core::Json::array({{{"field", "f"}, {"unit", "dB"}}})},
                 {"tooltip", "t"},
                 {"geometry", {{"generator", "Box"}}},
                 {"lod", core::Json::array({{{"max_m", 1.0}, {"detail", "full"}}})}};
    auto d = ComponentCatalog::parse(j, "test");
    REQUIRE(d.has_value());
    auto st = ComponentCatalog::validate(*d);
    REQUIRE_FALSE(st.has_value()); // 'function' too short and no theory anchor
    CHECK(st.error().code == kErrDescriptor);
    CHECK(st.error().notes.size() >= 2);
    j["geometry"] = core::Json::object(); // generator missing
    CHECK_FALSE(ComponentCatalog::parse(j, "test").has_value());
    CHECK(utf8Length("Φ0 µm ³He") == 9);
}
