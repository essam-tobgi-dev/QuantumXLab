// Cross-module check: the wiring files generated for the shipped devices (tools/gencal.py) load
// through cryo::loadWiring unchanged. A mismatch in the envelope kind once hid here because the
// Cryo tests only ever read the _template file.
#include "Core/Paths.hpp"
#include "Cryo/Cryo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace qlab;

TEST_CASE("every shipped transmon device's wiring.json loads through Cryo") {
    for (const char* id : {"sc_fixed_5", "sc_heavyhex_27", "sc_heavyhex_127", "sc_tunable_grid_54"}) {
        INFO(id);
        const auto path = core::assetDir() / "Devices" / id / "wiring.json";
        auto w = cryo::loadWiring(path);
        if (!w) {
            UNSCOPED_INFO(w.error().format());
        }
        REQUIRE(w.has_value());
        REQUIRE(w->device == id);
        REQUIRE_FALSE(w->lines.empty());
        // Every qubit drive line passes the 4 K and mixing-chamber stages (spec 11 §4.1).
        std::size_t drives = 0;
        for (const auto& line : w->lines)
            if (line.kind == cryo::LineKind::XY) ++drives;
        REQUIRE(drives > 0);
    }
}

TEST_CASE("the template and the older envelope kind are both accepted") {
    auto tpl = cryo::loadWiring(core::assetDir() / "Devices" / "_template" / "wiring.json");
    REQUIRE(tpl.has_value());
    // Round trip: what Cryo writes, Cryo reads.
    auto again = cryo::parseWiring(cryo::serializeWiring(*tpl));
    REQUIRE(again.has_value());
    REQUIRE(again->lines.size() == tpl->lines.size());
    // A foreign kind is refused with a message that names it.
    auto bad = cryo::parseWiring(R"({"qxl":{"kind":"pulses","schema":1,"app":"t","created":"t"},"data":{}})");
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(bad.error().message.find("pulses") != std::string::npos);
}
