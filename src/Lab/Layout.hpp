#pragma once
// Spec 17 §11 — Assets/Lab/Layouts/<id>/layout.json (kind "lab.layout") and routing.json
// (kind "lab.routing"): room, fridge stages, device, wiring, racks, GHS, props, bookmarks, layers,
// and the per-kind wiring bundles with stage slots.
#include "Core/Error.hpp"
#include "Cryo/Stages.hpp"
#include "Cryo/Wiring.hpp"
#include "Graphics/Camera.hpp"
#include "Lab/Types.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace qlab::lab {

struct StageSpec {
    std::string name;      // layout id: rt, s50, s4, still, cp, mxc
    cryo::Stage stage = cryo::Stage::RT;
    double height_m = 0.0; // plate centre
    double radius_m = 0.2;
};

struct RackSpec {
    std::string id;
    glm::dvec3 position_m{0.0};
    int heightU = 42;
    std::vector<std::pair<std::string, int>> units; // "mw_generator:4" → ("mw_generator", 4)
};

struct PropSpec {
    std::string id;
    glm::dvec3 position_m{0.0};
    int count = 1;
    std::optional<glm::dvec2> size_m;
};

struct LaserSpec {
    std::string id;
    double wavelength_nm = 369.0;
    std::string role;
};

// Spec 17 §11 `lights[]` — a ceiling luminaire: an emissive panel of `size_m` (w, d) flush with the
// ceiling at `position_m`, and one renderer point light of luminous flux `lumens`.
struct LightSpec {
    glm::dvec3 position_m{0.0, 3.0, 0.0};
    glm::dvec2 size_m{1.2, 0.6};
    glm::vec3 color{1.0f, 0.98f, 0.92f};
    double lumens = 4000.0;
};

struct LayoutBookmark {
    std::string name;
    gfx::Bookmark view;
    std::string scaleIsland; // non-empty: anchored to that island (spec 17 §1), e.g. "chip_micro"
};

struct LayoutSpec {
    std::string id;
    std::filesystem::path dir;
    double roomWidth_m = 7.0, roomDepth_m = 6.0, roomHeight_m = 3.4;
    std::string fridgeModel;
    glm::dvec3 fridgePosition_m{0.0};
    std::vector<StageSpec> stages; // top to bottom
    bool shields = true, magShield = true;
    double frameWidth_m = 1.4, frameDepth_m = 1.4, frameHeight_m = 2.6;
    std::string device;
    std::filesystem::path wiringFile, routingFile;
    std::vector<RackSpec> racks;
    std::optional<glm::dvec3> ghs, compressor;
    bool compressorOutside = false;
    std::vector<PropSpec> props;
    std::optional<glm::dvec3> chamberPosition_m; // ion lab (spec 17 §3.6)
    double chamberRadius_m = 0.12;
    std::vector<LaserSpec> lasers;
    std::vector<LayoutBookmark> bookmarks;
    std::vector<std::string> layers;
    std::vector<LightSpec> lights;
    bool hasFridge() const { return !stages.empty(); }
};

struct LineBundle {
    cryo::LineKind kind = cryo::LineKind::XY;
    double sectorStart_deg = 0.0, sectorEnd_deg = 360.0;
    std::map<std::string, double, std::less<>> radius_m; // per stage name
};

struct RoutingSpec {
    std::vector<std::string> stageOrder;
    double feedthroughRadius_m = 0.24;
    int feedthroughCount = 96;
    double feedthroughStart_deg = 0.0;
    std::vector<LineBundle> bundles;
    std::map<std::string, glm::dvec3, std::less<>> slots; // attn, clamp, filter, iso, amp
    std::map<std::string, double, std::less<>> segmentLengths_m;
    double rackToFridgeCable_m = 6.0;
    const LineBundle* bundle(cryo::LineKind k) const;
    glm::dvec3 slot(std::string_view name) const; // zero offset for unknown slots
};

// "sc_lab_standard" or a directory; relative ids resolve under core::assetDir()/Lab/Layouts.
std::filesystem::path resolveLayoutDir(const std::filesystem::path& dirOrId);
// Layout files name assets repo-relative ("Assets/Devices/x/wiring.json"); maps them onto assetDir().
std::filesystem::path resolveAssetPath(std::string_view path);

Result<LayoutSpec> loadLayout(const std::filesystem::path& dirOrId);
Result<RoutingSpec> loadRouting(const std::filesystem::path& file);

// Layout stage id ↔ cryo stage and descriptor id (spec 17 §3.1): rt → RT / top_plate_300K, …
bool stageFromLayoutName(std::string_view name, cryo::Stage& out);
std::string_view stageLayoutName(cryo::Stage s);
std::string_view stageDescriptorId(cryo::Stage s);

} // namespace qlab::lab
