#pragma once
// Spec 18 §6 — colormaps as CPU tables + 1D textures. twilight is cyclic (phase, spec 21 §2.4).
#include "Graphics/GlObjects.hpp"
#include <glm/glm.hpp>
#include <string_view>
#include <vector>

namespace qlab::gfx {

enum class ColormapId { Viridis, Inferno, Coolwarm, Twilight };

class Colormap {
  public:
    static const Colormap& get(ColormapId id);
    static ColormapId byName(std::string_view name); // "viridis" ... ; unknown -> Viridis
    glm::vec3 sample(float t) const;                 // t clamped to [0,1] (twilight wraps)
    glm::vec4 sample4(float t) const { return glm::vec4(sample(t), 1.0f); }
    // 256-entry LUT (spec 21 §2.4 phaseWheel is Twilight's LUT).
    std::vector<glm::vec3> lut(int n = 256) const;
    bool cyclic() const { return cyclic_; }
    const std::vector<float>& table() const { return rgb_; }
    int controlPoints() const { return static_cast<int>(rgb_.size() / 3); }

  private:
    Colormap(const float* t, int n, bool cyclic) : rgb_(t, t + n * 3), cyclic_(cyclic) {}
    std::vector<float> rgb_;
    bool cyclic_ = false;
};

// GPU-side LUT texture set, one per colormap; bound on demand.
class ColormapTextures {
  public:
    void ensure(); // creates textures on first use (needs GL context)
    void bind(ColormapId id, GLuint unit) const;

  private:
    std::vector<Texture1D> tex_;
};

} // namespace qlab::gfx
