#pragma once
// Spec 18 §4 (textures, amended 2026-09-18) — PBR texture sets. A set is a directory
// Assets/Textures/<set>/{albedo,normal,roughness[,metallic,ao]}.jpg (any subset); the library
// loads each set once, on first use, and hands back GPU textures: albedo sRGB, everything else
// linear, mipmapped, repeating, anisotropic where the context allows. Roughness, metallic and
// ao are packed on the CPU into one RGB texture (R, G, B) so the TEXTURED permutation samples
// three maps. A map missing on disk keeps a 1×1 neutral texel (white albedo, flat normal, white
// roughness/metallic/ao), so a material may use any subset and the shader has no branches.
#include "Core/Error.hpp"
#include "Graphics/GlObjects.hpp"
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::gfx {

// Texture units of the TEXTURED permutation (5–9 carry the shadow map, colormap and IBL).
constexpr unsigned kTexUnitAlbedo = 10, kTexUnitNormal = 11, kTexUnitOrm = 12;

// A decoded 8-bit RGBA image (row 0 at the top, as on disk).
struct ImageRgba8 {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    bool valid() const { return width > 0 && height > 0 && rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4; }
    static ImageRgba8 solid(std::uint8_t r, std::uint8_t g, std::uint8_t b, int w = 1, int h = 1);
};

// The CPU side of a set: any image may be left empty (invalid) and falls back to neutral.
// Grey maps (roughness, metallic, ao) use their red channel.
struct TextureSetSource {
    ImageRgba8 albedo, normal, roughness, metallic, ao;
};

struct TextureSet {
    std::string name;
    Texture2D albedo;   // SRGBA8
    Texture2D normal;   // RGBA8 linear, OpenGL convention (+Y up), z in blue
    Texture2D orm;      // RGBA8 linear: R roughness, G metallic, B ao
    bool hasAlbedo = false, hasNormal = false, hasRoughness = false, hasMetallic = false, hasAo = false;
    glm::vec3 albedoMean{1.0f};  // linear per-channel mean of the albedo map (1 when absent)
    float roughnessMean = 1.0f;  // mean of the roughness map (1 when absent)
    // Binds albedo/normal/orm to the TEXTURED units.
    void bind() const;
    // Builds the GPU textures; `maxAnisotropy` ≤ 1 disables anisotropic filtering.
    static TextureSet build(std::string name, const TextureSetSource& src, float maxAnisotropy);
};

class TextureLibrary {
public:
    // `root` empty → Assets/Textures (core::assetDir()); `maxAnisotropy` 0 → from Caps::query().
    explicit TextureLibrary(std::filesystem::path root = {}, float maxAnisotropy = 0.0f);
    // The set named `set`, loaded on first use. An unknown set logs once and returns the
    // neutral set (every map 1×1), so a material never fails to draw.
    const TextureSet& get(std::string_view set);
    const TextureSet& neutral() const { return neutral_; }
    // Registers a set built from memory under `name` (tests, generated content); replaces any
    // loaded set of that name.
    const TextureSet& add(std::string name, const TextureSetSource& src);
    // True when `<root>/<set>/albedo.jpg` (or .png) exists — pure filesystem query, no GL.
    bool existsOnDisk(std::string_view set) const;
    std::size_t loadedCount() const { return sets_.size(); }
    const std::filesystem::path& root() const { return root_; }
    float maxAnisotropy() const { return maxAnisotropy_; }

    // Decodes a JPG/PNG file to RGBA8 (stb_image). Pure: no GL, usable headless.
    static Result<ImageRgba8> loadImage(const std::filesystem::path& file);
    // Reads `<root>/<set>/<map>.{jpg,png}` for every map; missing files stay invalid.
    static TextureSetSource loadSource(const std::filesystem::path& setDir);

private:
    std::filesystem::path root_;
    float maxAnisotropy_ = 1.0f;
    TextureSet neutral_;
    std::map<std::string, std::unique_ptr<TextureSet>, std::less<>> sets_;
};

} // namespace qlab::gfx
