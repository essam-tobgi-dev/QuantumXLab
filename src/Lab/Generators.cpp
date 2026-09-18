// Spec 17 §6 — generator parameters and dispatch.
#include "Lab/Generators.hpp"
#include "Lab/MeshOps.hpp"
#include <array>
#include <cmath>
#include <format>

namespace qlab::lab {

GenParams::GenParams(core::Json object) : j_(std::move(object)) {
    if (!j_.is_object()) j_ = core::Json::object();
}

GenParams GenParams::merged(const core::Json& base, const core::Json& overrides) {
    core::Json j = base.is_object() ? base : core::Json::object();
    if (!overrides.is_object()) return GenParams(std::move(j));
    static constexpr std::array<std::string_view, 3> kSuffix{"_m", "_mm", "_um"};
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        // A length override replaces the descriptor's value whatever unit suffix it was written
        // with ("w_um": 8272 must displace "w_m": 0.008).
        for (std::string_view suffix : kSuffix) {
            if (!it.key().ends_with(suffix)) continue;
            std::string stem = it.key().substr(0, it.key().size() - suffix.size());
            for (std::string_view other : kSuffix)
                if (other != suffix) j.erase(stem + std::string(other));
            break;
        }
        j[it.key()] = it.value();
    }
    return GenParams(std::move(j));
}

GenParams& GenParams::set(const std::string& key, core::Json value) {
    j_[key] = std::move(value);
    return *this;
}

bool GenParams::has(std::string_view key) const { return j_.contains(key); }

double GenParams::number(std::string_view key, double fallback) const {
    auto it = j_.find(key);
    return it != j_.end() && it->is_number() ? it->get<double>() : fallback;
}

int GenParams::integer(std::string_view key, int fallback) const {
    auto it = j_.find(key);
    if (it == j_.end()) return fallback;
    if (it->is_number()) return static_cast<int>(std::lround(it->get<double>()));
    if (it->is_array()) return static_cast<int>(it->size()); // e.g. "ports": ["signal", "pump"]
    return fallback;
}

bool GenParams::boolean(std::string_view key, bool fallback) const {
    auto it = j_.find(key);
    return it != j_.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

std::string GenParams::string(std::string_view key, std::string_view fallback) const {
    auto it = j_.find(key);
    return it != j_.end() && it->is_string() ? it->get<std::string>() : std::string(fallback);
}

std::vector<std::string> GenParams::strings(std::string_view key) const {
    std::vector<std::string> out;
    auto it = j_.find(key);
    if (it == j_.end()) return out;
    if (it->is_string()) out.push_back(it->get<std::string>());
    if (it->is_array())
        for (const auto& e : *it)
            if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}

std::optional<double> GenParams::length(std::string_view stem) const {
    static constexpr std::array<std::pair<std::string_view, double>, 3> kSuffix{
        {{"_m", 1.0}, {"_mm", 1e-3}, {"_um", 1e-6}}};
    for (const auto& [suffix, factor] : kSuffix) {
        std::string key = std::string(stem) + std::string(suffix);
        auto it = j_.find(key);
        if (it != j_.end() && it->is_number()) return it->get<double>() * factor;
    }
    return std::nullopt;
}

double GenParams::length(std::string_view stem, double fallback) const { return length(stem).value_or(fallback); }

namespace {
std::optional<glm::dvec3> toPoint(const core::Json& e, double f) {
    if (!e.is_array() || e.size() < 2 || !e[0].is_number() || !e[1].is_number()) return std::nullopt;
    double z = e.size() >= 3 && e[2].is_number() ? e[2].get<double>() : 0.0;
    return glm::dvec3(e[0].get<double>(), e[1].get<double>(), z) * f;
}
const core::Json* findScaled(const core::Json& j, std::string_view stem, double& factor) {
    for (auto [suffix, f] : {std::pair<const char*, double>{"_m", 1.0}, {"_um", 1e-6}, {"_mm", 1e-3}}) {
        auto it = j.find(std::string(stem) + suffix);
        if (it != j.end() && it->is_array()) {
            factor = f;
            return &*it;
        }
    }
    return nullptr;
}
} // namespace

std::optional<glm::dvec3> GenParams::point3(std::string_view stem) const {
    double f = 1.0;
    const core::Json* a = findScaled(j_, stem, f);
    return a ? toPoint(*a, f) : std::nullopt;
}

std::vector<glm::dvec3> GenParams::points3(std::string_view stem) const {
    std::vector<glm::dvec3> out;
    double f = 1.0;
    if (const core::Json* a = findScaled(j_, stem, f))
        for (const auto& e : *a)
            if (auto p = toPoint(e, f)) out.push_back(*p);
    return out;
}

std::vector<glm::dvec2> GenParams::points2(std::string_view stem) const {
    std::vector<glm::dvec2> out;
    for (const auto& p : points3(stem)) out.emplace_back(p.x, p.y);
    return out;
}

std::string GenParams::cacheKey(std::string_view generator, Detail detail, double unitScale) const {
    return std::format("{}|{}|{:g}|{}", generator, detailName(detail), unitScale, j_.dump());
}

double quarterWaveLength_m(double f_r_Hz, double epsEff) {
    constexpr double c = 299792458.0;
    return f_r_Hz > 0.0 ? c / (4.0 * f_r_Hz * std::sqrt(epsEff)) : 0.0;
}

namespace {
struct Entry {
    std::string_view name;
    GeneratorFn fn;
};
constexpr std::array<Entry, 24> kGenerators{{
    {"Box", &gen::box},           {"Plate", &gen::plate},         {"PlateWithHoles", &gen::plateWithHoles},
    {"RackUnit", &gen::rackUnit}, {"Cylinder", &gen::cylinder},   {"CylinderSma", &gen::cylinderSma},
    {"SmaConnector", &gen::smaConnector}, {"Tube", &gen::tube},   {"Can", &gen::can},
    {"Torus", &gen::torus},       {"Sphere", &gen::sphere},       {"Frame", &gen::frame},
    {"Octagon", &gen::octagon},   {"ExtrudedU", &gen::extrudedU}, {"Spiral", &gen::spiral},
    {"Cpw", &gen::cpw},           {"Meander", &gen::meander},     {"Xmon", &gen::xmon},
    {"Junction", &gen::junction}, {"SquidLoop", &gen::squidLoop}, {"Airbridge", &gen::airbridge},
    {"WirebondArc", &gen::wirebondArc}, {"TrapChip", &gen::trapChip}, {"Post", &gen::post},
}};
constexpr auto kNames = [] {
    std::array<std::string_view, kGenerators.size()> n{};
    for (std::size_t i = 0; i < kGenerators.size(); ++i) n[i] = kGenerators[i].name;
    return n;
}();
} // namespace

std::span<const std::string_view> generatorNames() { return kNames; }

bool hasGenerator(std::string_view name) {
    for (const auto& e : kGenerators)
        if (e.name == name) return true;
    return false;
}

Result<gfx::MeshData> generateMesh(std::string_view generator, const GenParams& params, const GenContext& ctx) {
    if (ctx.detail == Detail::Hidden) return gfx::MeshData{};
    for (const auto& e : kGenerators) {
        if (e.name != generator) continue;
        QXL_TRY_ASSIGN(gfx::MeshData m, e.fn(params, ctx));
        mesh::orientToNormals(m);
        if (std::string why = mesh::validate(m); !why.empty())
            return fail(kErrGeometry, std::format("generator '{}' produced an invalid mesh: {}", generator, why));
        return m;
    }
    return fail(kErrGeometry, std::format("unknown geometry generator '{}'", generator));
}

} // namespace qlab::lab
