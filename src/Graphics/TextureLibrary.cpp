// Spec 18 §4 (textures) — texture set loading, packing and the neutral fallbacks.
#include "Graphics/TextureLibrary.hpp"
#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include "Graphics/Caps.hpp"
#include <algorithm>
#include <cmath>
#include <stb/stb_image.h>

namespace qlab::gfx {

namespace {
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

float srgbToLinear(std::uint8_t v) {
    const float c = static_cast<float>(v) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

Texture2D makeTexture(const ImageRgba8& img, TexFormat format, float maxAnisotropy) {
    TexDesc d;
    d.width = img.width;
    d.height = img.height;
    d.format = format;
    d.mipmaps = img.width > 1 || img.height > 1;
    d.linear = true;
    d.clampToEdge = false;   // tiling
    Texture2D t(d, img.rgba.data());
    if (d.mipmaps && maxAnisotropy > 1.0f) {
        glBindTexture(GL_TEXTURE_2D, t.id());
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAnisotropy);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    return t;
}

// Packs three grey maps into one RGBA image (R roughness, G metallic, B ao); an absent map is
// white. Maps of different sizes are sampled with nearest lookup into the largest.
ImageRgba8 packOrm(const ImageRgba8& rough, const ImageRgba8& metal, const ImageRgba8& ao) {
    int w = 1, h = 1;
    for (const ImageRgba8* m : {&rough, &metal, &ao})
        if (m->valid()) { w = std::max(w, m->width); h = std::max(h, m->height); }
    ImageRgba8 out;
    out.width = w; out.height = h;
    out.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 255);
    auto fetch = [](const ImageRgba8& m, int x, int y, int w2, int h2) -> std::uint8_t {
        if (!m.valid()) return 255;
        const int mx = static_cast<int>(static_cast<long long>(x) * m.width / w2);
        const int my = static_cast<int>(static_cast<long long>(y) * m.height / h2);
        return m.rgba[(static_cast<std::size_t>(my) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(mx)) * 4];
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4];
            p[0] = fetch(rough, x, y, w, h);
            p[1] = fetch(metal, x, y, w, h);
            p[2] = fetch(ao, x, y, w, h);
            p[3] = 255;
        }
    return out;
}
} // namespace

ImageRgba8 ImageRgba8::solid(std::uint8_t r, std::uint8_t g, std::uint8_t b, int w, int h) {
    ImageRgba8 img;
    img.width = w; img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) { img.rgba[i] = r; img.rgba[i + 1] = g; img.rgba[i + 2] = b; img.rgba[i + 3] = 255; }
    return img;
}

void TextureSet::bind() const {
    albedo.bind(kTexUnitAlbedo);
    normal.bind(kTexUnitNormal);
    orm.bind(kTexUnitOrm);
}

TextureSet TextureSet::build(std::string name, const TextureSetSource& src, float maxAnisotropy) {
    TextureSet s;
    s.name = std::move(name);
    s.hasAlbedo = src.albedo.valid();
    s.hasNormal = src.normal.valid();
    s.hasRoughness = src.roughness.valid();
    s.hasMetallic = src.metallic.valid();
    s.hasAo = src.ao.valid();
    const ImageRgba8 whiteTexel = ImageRgba8::solid(255, 255, 255);
    const ImageRgba8 flatNormal = ImageRgba8::solid(128, 128, 255);
    s.albedo = makeTexture(s.hasAlbedo ? src.albedo : whiteTexel, TexFormat::SRGBA8, maxAnisotropy);
    s.normal = makeTexture(s.hasNormal ? src.normal : flatNormal, TexFormat::RGBA8, maxAnisotropy);
    s.orm = makeTexture(packOrm(src.roughness, src.metallic, src.ao), TexFormat::RGBA8, maxAnisotropy);
    if (s.hasAlbedo) {
        // Linear per-channel mean of the map: the renderer divides by it when the material asks
        // for normalised maps, so the table's calibrated baseColor (F0 for metals) is the mean
        // colour of the textured surface and the map carries only the variation.
        glm::dvec3 sum(0.0);
        const std::size_t n = static_cast<std::size_t>(src.albedo.width) * static_cast<std::size_t>(src.albedo.height);
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t* p = &src.albedo.rgba[i * 4];
            sum += glm::dvec3(srgbToLinear(p[0]), srgbToLinear(p[1]), srgbToLinear(p[2]));
        }
        s.albedoMean = glm::max(glm::vec3(sum / static_cast<double>(n)), glm::vec3(1e-3f));
    }
    if (s.hasRoughness) {
        double sum = 0.0;
        const std::size_t n = static_cast<std::size_t>(src.roughness.width) * static_cast<std::size_t>(src.roughness.height);
        for (std::size_t i = 0; i < n; ++i) sum += src.roughness.rgba[i * 4] / 255.0;
        s.roughnessMean = std::max(static_cast<float>(sum / static_cast<double>(n)), 1e-3f);
    }
    return s;
}

TextureLibrary::TextureLibrary(std::filesystem::path root, float maxAnisotropy)
    : root_(root.empty() ? core::assetDir() / "Textures" : std::move(root)) {
    if (maxAnisotropy <= 0.0f) {
        const Caps caps = Caps::query();
        maxAnisotropy = caps.anisotropic ? caps.maxAnisotropy : 1.0f;
    }
    maxAnisotropy_ = std::clamp(maxAnisotropy, 1.0f, 16.0f);
    neutral_ = TextureSet::build("neutral", TextureSetSource{}, maxAnisotropy_);
}

Result<ImageRgba8> TextureLibrary::loadImage(const std::filesystem::path& file) {
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load(file.string().c_str(), &w, &h, &n, 4);
    if (!px) return fail(ErrorCode::Io, "cannot decode " + file.string() + ": " + (stbi_failure_reason() ? stbi_failure_reason() : ""));
    ImageRgba8 img;
    img.width = w; img.height = h;
    img.rgba.assign(px, px + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(px);
    return img;
}

TextureSetSource TextureLibrary::loadSource(const std::filesystem::path& setDir) {
    TextureSetSource src;
    auto read = [&](const char* map, ImageRgba8& into) {
        for (const char* ext : {".jpg", ".png", ".jpeg"}) {
            const auto file = setDir / (std::string(map) + ext);
            if (!std::filesystem::exists(file)) continue;
            auto img = loadImage(file);
            if (img) into = std::move(*img);
            else QXL_LOG_WARN(Gfx, "texture {}: {}", file.string(), img.error().message);
            return;
        }
    };
    read("albedo", src.albedo);
    read("normal", src.normal);
    read("roughness", src.roughness);
    read("metallic", src.metallic);
    read("ao", src.ao);
    return src;
}

bool TextureLibrary::existsOnDisk(std::string_view set) const {
    const auto dir = root_ / std::string(set);
    return std::filesystem::exists(dir / "albedo.jpg") || std::filesystem::exists(dir / "albedo.png");
}

const TextureSet& TextureLibrary::add(std::string name, const TextureSetSource& src) {
    auto set = std::make_unique<TextureSet>(TextureSet::build(name, src, maxAnisotropy_));
    const TextureSet& ref = *set;
    sets_[name] = std::move(set);
    return ref;
}

const TextureSet& TextureLibrary::get(std::string_view set) {
    if (set.empty()) return neutral_;
    if (auto it = sets_.find(set); it != sets_.end()) return *it->second;
    const auto dir = root_ / std::string(set);
    TextureSetSource src;
    if (std::filesystem::is_directory(dir)) src = loadSource(dir);
    else QXL_LOG_WARN(Gfx, "texture set '{}' not found under {}; using neutral maps", set, root_.string());
    const TextureSet& ref = add(std::string(set), src);
    QXL_LOG_INFO(Gfx, "texture set '{}': albedo {} normal {} roughness {} metallic {} ao {} ({}x{})", set,
                 ref.hasAlbedo, ref.hasNormal, ref.hasRoughness, ref.hasMetallic, ref.hasAo, ref.albedo.width(), ref.albedo.height());
    return ref;
}

} // namespace qlab::gfx
