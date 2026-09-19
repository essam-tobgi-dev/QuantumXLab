#pragma once
// Spec 18 §3 — signed-distance-field glyph atlas built with FreeType; cached on disk.
#include "Core/Error.hpp"
#include "Graphics/GlObjects.hpp"
#include <filesystem>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace qlab::gfx {

struct Glyph {
    glm::vec2 uv0, uv1; // atlas texcoords
    glm::vec2 size;     // quad size in font units (pixels at nominal size)
    glm::vec2 bearing;  // offset from pen to quad top-left
    float advance = 0;
};

struct SdfAtlasData {
    int width = 0, height = 0;
    int nominalPx = 48; // glyph size the SDF was rasterised at
    float spread = 8;   // SDF spread in pixels
    float ascent = 0, descent = 0, lineHeight = 0;
    std::vector<std::uint8_t> pixels; // R8
    std::map<unsigned, Glyph> glyphs; // codepoint -> glyph
};

// CPU-only builder (no GL): rasterises ASCII 32..126 + Greek + common math symbols.
Result<SdfAtlasData> buildSdfAtlas(const std::filesystem::path& ttf, int nominalPx = 48,
                                   float spread = 8);
Result<SdfAtlasData> loadAtlasCache(const std::filesystem::path& cache);
Status saveAtlasCache(const SdfAtlasData& a, const std::filesystem::path& cache);

class SdfFont {
  public:
    // Builds or loads from cache under core::userDataDir()/fontcache. Needs a GL context.
    static Result<std::unique_ptr<SdfFont>> load(const std::filesystem::path& ttf,
                                                 int nominalPx = 48);
    const SdfAtlasData& atlas() const { return data_; }
    const Texture2D& texture() const { return tex_; }
    const Glyph* glyph(unsigned cp) const;
    glm::vec2 measure(std::string_view utf8, float sizePx) const; // width, height
  private:
    SdfAtlasData data_;
    Texture2D tex_;
};

// Decodes UTF-8 into codepoints (invalid bytes -> U+FFFD).
std::vector<unsigned> decodeUtf8(std::string_view s);

} // namespace qlab::gfx
