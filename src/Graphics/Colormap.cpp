#include "Graphics/Colormap.hpp"
#include <algorithm>
#include <cmath>
namespace qlab::gfx {
#include "Graphics/ColormapTables.inc"
const Colormap& Colormap::get(ColormapId id) {
    static const Colormap maps[4] = {Colormap(k_viridis, 33, false), Colormap(k_inferno, 33, false),
                                     Colormap(k_coolwarm, 33, false), Colormap(k_twilight, 33, true)};
    return maps[static_cast<int>(id)];
}
ColormapId Colormap::byName(std::string_view n) {
    if (n == "inferno") return ColormapId::Inferno;
    if (n == "coolwarm") return ColormapId::Coolwarm;
    if (n == "twilight") return ColormapId::Twilight;
    return ColormapId::Viridis;
}
glm::vec3 Colormap::sample(float t) const {
    int n = controlPoints();
    if (cyclic_) t = t - std::floor(t); else t = std::clamp(t, 0.0f, 1.0f);
    float x = t * static_cast<float>(n - 1);
    int i = std::min(n - 2, static_cast<int>(x));
    float f = x - static_cast<float>(i);
    const float* a = &rgb_[static_cast<std::size_t>(i) * 3];
    const float* b = &rgb_[static_cast<std::size_t>(i + 1) * 3];
    return {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
}
std::vector<glm::vec3> Colormap::lut(int n) const {
    std::vector<glm::vec3> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) out[static_cast<std::size_t>(i)] = sample(static_cast<float>(i) / static_cast<float>(cyclic_ ? n : n - 1));
    return out;
}
void ColormapTextures::ensure() {
    if (!tex_.empty()) return;
    for (int i = 0; i < 4; ++i) {
        auto l = Colormap::get(static_cast<ColormapId>(i)).lut(256);
        std::vector<float> flat;
        for (auto& c : l) { flat.push_back(c.r); flat.push_back(c.g); flat.push_back(c.b); }
        tex_.emplace_back(std::span<const float>(flat), 256);
    }
}
void ColormapTextures::bind(ColormapId id, GLuint unit) const {
    if (tex_.empty()) return;
    tex_[static_cast<std::size_t>(id)].bind(unit);
}
} // namespace qlab::gfx
