// Spec 18 §4 (textures) — the laboratory material table: every textured material names a set
// that ships under Assets/Textures with at least an albedo map, every set is recorded as CC0 in
// SOURCES.md, and the renderer-side tints keep the texture assignment. Headless (no GL).
#include "Lab/Materials.hpp"
#include "Core/Paths.hpp"
#include "Graphics/TextureLibrary.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace qlab;
using namespace qlab::lab;

TEST_CASE("every material's texture set exists on disk with an albedo map and sane parameters") {
    const auto root = core::assetDir() / "Textures";
    REQUIRE(std::filesystem::is_directory(root));
    std::set<std::string> used;
    int textured = 0;
    for (std::string_view name : labMaterialNames()) {
        INFO(name);
        REQUIRE(isKnownMaterial(name));
        const gfx::Material m = labMaterial(name);
        if (!m.textured()) {
            // Only the chip films, the transparent finishes, the emitters and the instrument
            // faceplates/screens (engraved features that a normal map would hatch over) stay
            // constant.
            CHECK((name == "niobium_film" || name == "silicon" || name == "sapphire" ||
                   name == "glass" || name == "glow" || name == "light_panel" ||
                   name == "panel_flat" || name == "screen_glass"));
            continue;
        }
        ++textured;
        used.insert(m.textureSet);
        const auto dir = root / m.textureSet;
        REQUIRE(std::filesystem::exists(dir / "albedo.jpg"));
        CHECK(std::filesystem::exists(dir / "normal.jpg"));
        CHECK(std::filesystem::exists(dir / "roughness.jpg"));
        auto img = gfx::TextureLibrary::loadImage(dir / "albedo.jpg");
        REQUIRE(img.has_value());
        CHECK(img->width >= 512);
        CHECK(img->width == img->height);
        CHECK(m.triplanar); // generated meshes: world-space projection
        CHECK(m.uvScale > 0.0f);
        CHECK(m.normalStrength >= 0.2f); // `vertex` (rack cabinets, props) is the subtlest
        CHECK(m.normalStrength <= 0.8f); // a clean laboratory, not a rusty yard
        CHECK(m.normalizeMaps);          // the table's colour stays the surface's mean
    }
    CHECK(textured >= 18);
    CHECK(used.size() <= 12);
    // the metals keep their calibrated F0 and roughness (spec 18 §4 table)
    const gfx::Material gold = labMaterial("gold_plated_cu");
    CHECK(gold.baseColor == glm::vec4(1.0f, 0.71f, 0.29f, 1.0f));
    CHECK(gold.metallic == 1.0f);
    CHECK(gold.roughness == 0.28f);
    CHECK(gold.textureSet == "gold_plated");
    CHECK(labMaterial("floor").textureSet == "epoxy_floor");
    CHECK(labMaterial("wall").textureSet == "painted_wall");
    CHECK(labMaterial("rubber").textureSet == "rubber");
    CHECK(labMaterial("vertex").textureSet == "powder_coated");
    CHECK_FALSE(labMaterial("glass").textured());
}

TEST_CASE("SOURCES.md records every shipped set with a CC0 licence line") {
    const auto root = core::assetDir() / "Textures";
    std::ifstream in(root / "SOURCES.md");
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    std::set<std::string> onDisk;
    for (const auto& e : std::filesystem::directory_iterator(root))
        if (e.is_directory() && std::filesystem::exists(e.path() / "albedo.jpg"))
            onDisk.insert(e.path().filename().string());
    CHECK(onDisk.size() >= 10);
    for (const std::string& set : onDisk) {
        INFO(set);
        bool found = false;
        std::istringstream lines(text);
        for (std::string line; std::getline(lines, line);)
            if (line.find("`" + set + "`") != std::string::npos &&
                line.find("CC0") != std::string::npos)
                found = true;
        CHECK(found);
    }
    // every set a material references is on disk
    for (std::string_view name : labMaterialNames()) {
        const gfx::Material m = labMaterial(name);
        if (m.textured())
            CHECK(onDisk.count(m.textureSet) == 1);
    }
}

TEST_CASE("renderer tints keep the texture assignment") {
    gfx::Material m = labMaterial("gold_plated_cu");
    m.baseColor =
        glm::mix(m.baseColor, glm::vec4(0.2f, 0.1f, 0.6f, 1.0f), 0.65f); // stage temperature tint
    m.baseColor.a = 0.12f;                                               // X-ray fade
    CHECK(m.textured());
    CHECK(m.transparent());
    CHECK(m.toUbo().tex.w == 1.0f);
    CHECK(m.toUbo(glm::vec3(2.0f), 0.5f).texGain == glm::vec4(2.0f, 2.0f, 2.0f, 0.5f));
    m.normalizeMaps = false;
    CHECK(m.toUbo(glm::vec3(2.0f), 0.5f).texGain == glm::vec4(1.0f));
}
