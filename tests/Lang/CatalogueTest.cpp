// Spec 13 §8 — the diagnostic catalogue is complete and consistent:
//   * every "QLnnnn" literal in the modules that emit diagnostics is a catalogue entry,
//   * the shipped Assets/Lang/diagnostics.json matches the in-code catalogue id for id,
//   * ids sit in the range their class requires (1xxx lexical … 5xxx runtime).
#include "Core/Json.hpp"
#include "Lang/Diagnostics.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <regex>
#include <set>
#include <string>

using namespace qlab;
namespace fs = std::filesystem;

namespace {
std::set<std::string> catalogueIds() {
    std::set<std::string> ids;
    for (const auto& d : lang::Diagnostics::all())
        ids.insert(std::string(d.id));
    return ids;
}
} // namespace

TEST_CASE("every diagnostic id emitted by the sources is catalogued") {
    const auto ids = catalogueIds();
    REQUIRE(ids.size() >= 40);
    const std::regex lit("\"(QL[0-9]{4})\"");
    const fs::path root = fs::path(QXL_SOURCE_DIR) / "src";
    // Modules that raise language, compile or runtime diagnostics. Core only mentions an id in a
    // comment as an example, so it is not scanned.
    std::set<std::string> missing;
    std::size_t scanned = 0;
    for (const char* module : {"Lang", "IR", "Compiler", "Runtime", "Pulse"}) {
        const fs::path dir = root / module;
        if (!fs::exists(dir))
            continue;
        for (const auto& e : fs::recursive_directory_iterator(dir)) {
            if (!e.is_regular_file())
                continue;
            const auto ext = e.path().extension();
            if (ext != ".cpp" && ext != ".hpp")
                continue;
            auto text = core::readTextFile(e.path());
            REQUIRE(text.has_value());
            ++scanned;
            for (std::sregex_iterator it(text->begin(), text->end(), lit), end; it != end; ++it)
                if (!ids.contains((*it)[1].str()))
                    missing.insert((*it)[1].str() + " in " + e.path().filename().string());
        }
    }
    REQUIRE(scanned > 20);
    for (const auto& m : missing)
        UNSCOPED_INFO("uncatalogued: " << m);
    REQUIRE(missing.empty());
}

TEST_CASE("the shipped diagnostics.json matches the in-code catalogue") {
    const fs::path file = fs::path(QXL_ASSET_DIR) / "Lang" / "diagnostics.json";
    auto text = core::readTextFile(file);
    REQUIRE(text.has_value());
    core::Json j = core::Json::parse(*text, nullptr, false);
    REQUIRE_FALSE(j.is_discarded());
    const core::Json& data = j.contains("data") ? j["data"] : j;
    const core::Json& list =
        data.is_object() && data.contains("diagnostics") ? data["diagnostics"] : data;
    REQUIRE(list.is_array());
    std::set<std::string> shipped;
    for (const auto& d : list) {
        REQUIRE(d.contains("id"));
        REQUIRE(d.contains("severity"));
        REQUIRE(d.contains("message"));
        shipped.insert(d["id"].get<std::string>());
    }
    REQUIRE(shipped == catalogueIds());
}

TEST_CASE("diagnostic ids sit in the range of their class") {
    const std::regex shape("QL[1-5][0-9]{3}");
    std::set<std::string_view> seen;
    for (const auto& d : lang::Diagnostics::all()) {
        INFO(d.id);
        REQUIRE(std::regex_match(std::string(d.id), shape));
        REQUIRE(seen.insert(d.id).second); // no duplicates
        REQUIRE_FALSE(d.message.empty());
    }
    // An unknown id falls back to a generic entry instead of failing.
    REQUIRE_FALSE(lang::Diagnostics::info("QL9999").message.empty());
}
