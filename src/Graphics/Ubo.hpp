#pragma once
// Spec 18 §4 — std140 uniform block layouts. Binding points are fixed: 0 Frame, 1 Lights,
// 2 Material, 3 Object, 4 Selection. The GLSL mirrors live in Assets/Shaders/ubo.glsl.
#include <cstdint>
#include <glm/glm.hpp>

namespace qlab::gfx {

constexpr unsigned kUboFrame = 0, kUboLights = 1, kUboMaterial = 2, kUboObject = 3, kUboSelection = 4;
constexpr int kMaxPointLights = 8;

struct alignas(16) FrameUbo {
    glm::mat4 view;            // camera-relative view (double-precision origin subtracted on CPU)
    glm::mat4 proj;
    glm::mat4 viewProj;
    glm::mat4 lightViewProj;   // shadow map matrix
    glm::vec4 cameraPos;       // xyz relative to origin, w = time (s)
    glm::vec4 viewport;        // w, h, 1/w, 1/h
    glm::vec4 params;          // x = exposure, y = shadow depth bias (NDC z), z = shadow texel (uv), w = ortho flag
    glm::vec4 shadow;          // x = normal-offset (world m, 1.5 texels), y = Poisson radius (texels), z = texel world size, w unused
    glm::vec4 post;            // x = environment intensity, y = IBL flag, z = SSAO strength, w = bloom strength
};

struct alignas(16) DirLight { glm::vec4 direction; glm::vec4 color; }; // color.w = intensity
struct alignas(16) PointLight { glm::vec4 position; glm::vec4 color; }; // position.w = radius

struct alignas(16) LightsUbo {
    DirLight sun;
    PointLight points[kMaxPointLights];
    glm::vec4 ambient;         // rgb, w = point light count
};

struct alignas(16) MaterialUbo {
    glm::vec4 baseColor;       // rgba (a = opacity)
    glm::vec4 emissive;        // rgb, w = emissive strength
    glm::vec4 props;           // metallic, roughness, ao, colormap-mix (0 = none)
    glm::vec4 misc;            // x = colormap value, y = flags (bit0 unlit, bit1 wireframe tint), z,w unused
};

struct alignas(16) ObjectUbo {
    glm::mat4 model;
    glm::mat4 normal;          // inverse transpose (mat3 padded)
    glm::uvec4 id;             // x = ComponentId, y = flags (bit0 selected, bit1 hovered)
};

struct alignas(16) SelectionUbo {
    glm::uvec4 selected;       // x = selected id, y = hovered id
    glm::vec4 outlineColor;
    glm::vec4 hoverColor;
    glm::vec4 params;          // x = outline width px
};

static_assert(sizeof(FrameUbo) % 16 == 0);
static_assert(sizeof(LightsUbo) % 16 == 0);
static_assert(sizeof(MaterialUbo) % 16 == 0);
static_assert(sizeof(ObjectUbo) % 16 == 0);
static_assert(sizeof(SelectionUbo) % 16 == 0);

} // namespace qlab::gfx
