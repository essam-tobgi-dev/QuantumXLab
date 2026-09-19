#pragma once
// Spec 17 §1 — shared value types of the laboratory scene: transforms (double precision so the
// metre-scale room and the micrometre-scale chip share one graph), visibility groups, LOD detail
// levels, exploded-view assemblies and the per-instance parameter pack that binding paths are
// substituted from.
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include "Graphics/Camera.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace qlab::lab {

// Error block of spec 04 §2 owned by this module (ErrorCode::Lab_ + n).
inline constexpr ErrorCode kErrDescriptor = ErrorCode::Lab_ + 1; // component.json invalid
inline constexpr ErrorCode kErrLayout = ErrorCode::Lab_ + 2;   // layout.json / routing.json invalid
inline constexpr ErrorCode kErrGeometry = ErrorCode::Lab_ + 3; // unknown or unusable generator
inline constexpr ErrorCode kErrScene = ErrorCode::Lab_ + 4;    // scene graph inconsistency
inline constexpr ErrorCode kErrChip = ErrorCode::Lab_ + 5;     // chip placement failed
inline constexpr ErrorCode kErrBinding = ErrorCode::Lab_ + 6;  // binding path malformed

// Spec 17 §1 — TRS relative to the parent. World units are metres, +Y up, +Z toward the lab door;
// the ChipMicro subtree is authored in micrometres under a 1e-6 scale (spec 17 §1, §6).
struct Transform {
    glm::dvec3 translation{0.0};
    glm::dquat rotation{1.0, 0.0, 0.0, 0.0};
    glm::dvec3 scale{1.0};

    glm::dmat4 matrix() const {
        glm::dmat4 m = glm::mat4_cast(rotation);
        m[0] *= scale.x;
        m[1] *= scale.y;
        m[2] *= scale.z;
        m[3] = glm::dvec4(translation, 1.0);
        return m;
    }
    static Transform at(const glm::dvec3& p) {
        Transform t;
        t.translation = p;
        return t;
    }
    static Transform at(double x, double y, double z) { return at(glm::dvec3{x, y, z}); }
    // Rotation of `deg` degrees about `axis`, then translation to `p`.
    static Transform rotated(const glm::dvec3& p, const glm::dvec3& axis, double deg);
    static Transform scaled(const glm::dvec3& p, double s) {
        Transform t = at(p);
        t.scale = glm::dvec3(s);
        return t;
    }
};

// Spec 17 §1/§7.7 — visibility group; also the layer-toggle unit.
enum class Group : std::uint8_t {
    Room,
    FridgeExterior,
    FridgeInterior,
    Wiring,
    Rack,
    GasHandling,
    Chip,
    ChipMicro,
    Overlay,
    Count
};
inline constexpr int kGroupCount = static_cast<int>(Group::Count);
std::string_view groupName(Group g); // "room", "fridge_exterior", ... (layout.json layer ids)
bool groupFromName(std::string_view s, Group& out);

// Spec 17 §4 `lod[].detail`.
enum class Detail : std::uint8_t { Full, Simple, Hidden };
std::string_view detailName(Detail d);
bool detailFromName(std::string_view s, Detail& out);

// Spec 17 §7.4 exploded-view assemblies.
enum class Assembly : std::uint8_t { None = 0, FridgeStages = 1, ChipPackage = 2, RackUnits = 3 };
inline constexpr int kAssemblyCount = 4;

// Spec 17 §4 — `$line`, `$k`, `$j`, `$i`, `$q`, `$e`, `$stage` are substituted into binding paths
// from the node instance; `numbers` additionally serves the node's `static.*` rows (spec 17 §5).
struct InstanceParams {
    std::map<std::string, std::string, std::less<>> tokens; // "line" -> "3", "stage" -> "mxc"
    std::map<std::string, double, std::less<>> numbers;     // "A_dB" -> 20

    void setToken(std::string key, std::string value) { tokens[std::move(key)] = std::move(value); }
    void setIndex(std::string key, long long value) {
        tokens[std::move(key)] = std::to_string(value);
    }
    void setNumber(std::string key, double value) { numbers[std::move(key)] = value; }
    const std::string* token(std::string_view key) const {
        auto it = tokens.find(key);
        return it == tokens.end() ? nullptr : &it->second;
    }
    std::optional<long long> index(std::string_view key) const;
    std::optional<double> number(std::string_view key) const {
        auto it = numbers.find(key);
        return it == numbers.end() ? std::optional<double>{} : it->second;
    }
};

// An AABB that contains nothing (`valid()` is false) and the union helpers used for subtree bounds.
inline gfx::Aabb emptyAabb() {
    const double inf = std::numeric_limits<double>::infinity();
    return gfx::Aabb{glm::dvec3(inf), glm::dvec3(-inf)};
}
inline void mergeInto(gfx::Aabb& into, const gfx::Aabb& b) {
    if (!b.valid())
        return;
    into.expand(b.min);
    into.expand(b.max);
}
inline glm::dvec3 aabbSize(const gfx::Aabb& b) {
    return b.valid() ? b.max - b.min : glm::dvec3(0.0);
}

} // namespace qlab::lab
