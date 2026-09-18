#include "Lab/Types.hpp"
#include <array>
#include <charconv>
#include <glm/gtc/constants.hpp>

namespace qlab::lab {

Transform Transform::rotated(const glm::dvec3& p, const glm::dvec3& axis, double deg) {
    Transform t = at(p);
    t.rotation = glm::angleAxis(deg * glm::pi<double>() / 180.0, glm::normalize(axis));
    return t;
}

std::optional<long long> InstanceParams::index(std::string_view key) const {
    const std::string* s = token(key);
    if (!s || s->empty()) return std::nullopt;
    long long v = 0;
    auto [p, ec] = std::from_chars(s->data(), s->data() + s->size(), v);
    if (ec != std::errc{} || p != s->data() + s->size()) return std::nullopt;
    return v;
}

namespace {
constexpr std::array<std::string_view, kGroupCount> kGroupNames{
    "room", "fridge_exterior", "fridge_interior", "wiring", "rack",
    "ghs",  "chip",            "chip_micro",      "overlay"};
} // namespace

std::string_view groupName(Group g) {
    auto i = static_cast<std::size_t>(g);
    return i < kGroupNames.size() ? kGroupNames[i] : std::string_view{"?"};
}

bool groupFromName(std::string_view s, Group& out) {
    for (std::size_t i = 0; i < kGroupNames.size(); ++i)
        if (kGroupNames[i] == s) {
            out = static_cast<Group>(i);
            return true;
        }
    // ion_lab_11 layer ids (spec 17 §3.6) map onto the shared groups.
    if (s == "chamber" || s == "optics") {
        out = Group::FridgeInterior;
        return true;
    }
    if (s == "trap_micro") {
        out = Group::ChipMicro;
        return true;
    }
    return false;
}

std::string_view detailName(Detail d) {
    switch (d) {
    case Detail::Full: return "full";
    case Detail::Simple: return "simple";
    case Detail::Hidden: return "hidden";
    }
    return "?";
}

bool detailFromName(std::string_view s, Detail& out) {
    // "capsule", "box" and "impostor" are the simplified levels of the spec 17 §4 example.
    if (s == "full") {
        out = Detail::Full;
        return true;
    }
    if (s == "simple" || s == "capsule" || s == "box" || s == "impostor") {
        out = Detail::Simple;
        return true;
    }
    if (s == "hidden") {
        out = Detail::Hidden;
        return true;
    }
    return false;
}

} // namespace qlab::lab
