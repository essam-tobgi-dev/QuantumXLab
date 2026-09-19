#include "Graphics/SdfFont.hpp"
#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace qlab::gfx {

std::vector<unsigned> decodeUtf8(std::string_view s) {
    std::vector<unsigned> out;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp;
        int n;
        if (c < 0x80) {
            cp = c;
            n = 1;
        } else if ((c >> 5) == 6) {
            cp = c & 0x1F;
            n = 2;
        } else if ((c >> 4) == 14) {
            cp = c & 0x0F;
            n = 3;
        } else if ((c >> 3) == 30) {
            cp = c & 0x07;
            n = 4;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (i + static_cast<std::size_t>(n) > s.size()) {
            out.push_back(0xFFFD);
            break;
        }
        bool ok = true;
        for (int k = 1; k < n; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            if ((cc >> 6) != 2) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        out.push_back(ok ? cp : 0xFFFD);
        i += static_cast<std::size_t>(n);
    }
    return out;
}

namespace {
std::vector<unsigned> charset() {
    std::vector<unsigned> cs;
    for (unsigned c = 32; c < 127; ++c)
        cs.push_back(c);
    for (unsigned c = 0x391; c <= 0x3C9; ++c)
        cs.push_back(c); // Greek
    for (unsigned c :
         {0x00B0u, 0x00B1u, 0x00B5u, 0x00D7u, 0x2013u, 0x2014u, 0x2019u, 0x2022u, 0x2026u,
          0x2032u, 0x2033u, 0x2190u, 0x2191u, 0x2192u, 0x2193u, 0x2194u, 0x2202u, 0x2207u,
          0x2208u, 0x221Au, 0x221Eu, 0x2248u, 0x2260u, 0x2264u, 0x2265u, 0x27E8u, 0x27E9u,
          0x210Fu, 0x2113u, 0x00B2u, 0x00B3u, 0x2080u, 0x2081u, 0x2082u, 0x2083u, 0x2084u,
          0x2205u, 0x22C5u, 0x2295u, 0x2297u, 0x03D5u, 0x03F5u})
        cs.push_back(c);
    return cs;
}

// Dead-reckoning-free brute-force SDF on a padded bitmap (glyph sizes are small; fine at startup).
void makeSdf(const std::vector<std::uint8_t>& bin, int w, int h, float spread,
             std::vector<std::uint8_t>& out) {
    out.assign(static_cast<std::size_t>(w * h), 0);
    int R = static_cast<int>(std::ceil(spread));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            bool inside = bin[static_cast<std::size_t>(y * w + x)] > 127;
            float best = spread * spread;
            for (int dy = -R; dy <= R; ++dy) {
                int yy = y + dy;
                if (yy < 0 || yy >= h) {
                    if (inside) {
                        float d = static_cast<float>(dy * dy);
                        best = std::min(best, d);
                    }
                    continue;
                }
                for (int dx = -R; dx <= R; ++dx) {
                    int xx = x + dx;
                    bool other = (xx < 0 || xx >= w)
                                     ? false
                                     : bin[static_cast<std::size_t>(yy * w + xx)] > 127;
                    if (other != inside)
                        best = std::min(best, static_cast<float>(dx * dx + dy * dy));
                }
            }
            float d = std::sqrt(best);
            float v = inside ? d : -d;
            out[static_cast<std::size_t>(y * w + x)] =
                static_cast<std::uint8_t>(std::clamp(128.0f + v / spread * 127.0f, 0.0f, 255.0f));
        }
}
} // namespace

Result<SdfAtlasData> buildSdfAtlas(const std::filesystem::path& ttf, int px, float spread) {
    FT_Library lib;
    if (FT_Init_FreeType(&lib))
        return fail(ErrorCode::Gfx_ + 20, "FreeType init failed");
    FT_Face face;
    if (FT_New_Face(lib, ttf.string().c_str(), 0, &face)) {
        FT_Done_FreeType(lib);
        return fail(ErrorCode::Gfx_ + 21, "cannot load font " + ttf.string());
    }
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));
    SdfAtlasData a;
    a.nominalPx = px;
    a.spread = spread;
    a.ascent = static_cast<float>(face->size->metrics.ascender) / 64.0f;
    a.descent = static_cast<float>(-face->size->metrics.descender) / 64.0f;
    a.lineHeight = static_cast<float>(face->size->metrics.height) / 64.0f;
    int pad = static_cast<int>(std::ceil(spread)) + 1;
    struct Cell {
        unsigned cp;
        int w, h;
        std::vector<std::uint8_t> sdf;
        float bx, by, adv;
    };
    std::vector<Cell> cells;
    for (unsigned cp : charset()) {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER))
            continue;
        FT_Bitmap& bm = face->glyph->bitmap;
        int w = static_cast<int>(bm.width) + 2 * pad, h = static_cast<int>(bm.rows) + 2 * pad;
        std::vector<std::uint8_t> bin(static_cast<std::size_t>(w * h), 0);
        for (unsigned y = 0; y < bm.rows; ++y)
            for (unsigned x = 0; x < bm.width; ++x)
                bin[static_cast<std::size_t>((static_cast<int>(y) + pad) * w + static_cast<int>(x) +
                                             pad)] =
                    bm.buffer[y * static_cast<unsigned>(bm.pitch) + x];
        Cell c{cp,
               w,
               h,
               {},
               static_cast<float>(face->glyph->bitmap_left) - static_cast<float>(pad),
               static_cast<float>(face->glyph->bitmap_top) + static_cast<float>(pad),
               static_cast<float>(face->glyph->advance.x) / 64.0f};
        makeSdf(bin, w, h, spread, c.sdf);
        cells.push_back(std::move(c));
    }
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    // shelf packing
    int W = 1024, x = 0, y = 0, rowH = 0;
    std::vector<std::pair<int, int>> pos;
    for (auto& c : cells) {
        if (x + c.w > W) {
            x = 0;
            y += rowH;
            rowH = 0;
        }
        pos.emplace_back(x, y);
        x += c.w;
        rowH = std::max(rowH, c.h);
    }
    int H = y + rowH;
    a.width = W;
    a.height = H;
    a.pixels.assign(static_cast<std::size_t>(W * H), 0);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        auto& c = cells[i];
        auto [px0, py0] = pos[i];
        for (int yy = 0; yy < c.h; ++yy)
            std::memcpy(&a.pixels[static_cast<std::size_t>((py0 + yy) * W + px0)],
                        &c.sdf[static_cast<std::size_t>(yy * c.w)], static_cast<std::size_t>(c.w));
        Glyph g;
        g.uv0 = {static_cast<float>(px0) / W, static_cast<float>(py0) / H};
        g.uv1 = {static_cast<float>(px0 + c.w) / W, static_cast<float>(py0 + c.h) / H};
        g.size = {static_cast<float>(c.w), static_cast<float>(c.h)};
        g.bearing = {c.bx, c.by};
        g.advance = c.adv;
        a.glyphs[c.cp] = g;
    }
    return a;
}

Status saveAtlasCache(const SdfAtlasData& a, const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    if (!f)
        return fail(ErrorCode::Io, "cannot write " + p.string());
    const char magic[8] = {'Q', 'X', 'L', 'S', 'D', 'F', '0', '2'};
    f.write(magic, 8);
    auto wr = [&](const auto& v) { f.write(reinterpret_cast<const char*>(&v), sizeof v); };
    wr(a.width);
    wr(a.height);
    wr(a.nominalPx);
    wr(a.spread);
    wr(a.ascent);
    wr(a.descent);
    wr(a.lineHeight);
    std::uint32_t n = static_cast<std::uint32_t>(a.glyphs.size());
    wr(n);
    for (auto& [cp, g] : a.glyphs) {
        wr(cp);
        wr(g);
    }
    f.write(reinterpret_cast<const char*>(a.pixels.data()),
            static_cast<std::streamsize>(a.pixels.size()));
    return {};
}
Result<SdfAtlasData> loadAtlasCache(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return fail(ErrorCode::Io, "no cache");
    char magic[8];
    f.read(magic, 8);
    if (std::memcmp(magic, "QXLSDF02", 8) != 0)
        return fail(ErrorCode::Parse, "bad cache magic");
    SdfAtlasData a;
    auto rd = [&](auto& v) { f.read(reinterpret_cast<char*>(&v), sizeof v); };
    rd(a.width);
    rd(a.height);
    rd(a.nominalPx);
    rd(a.spread);
    rd(a.ascent);
    rd(a.descent);
    rd(a.lineHeight);
    std::uint32_t n = 0;
    rd(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        unsigned cp;
        Glyph g;
        rd(cp);
        rd(g);
        a.glyphs[cp] = g;
    }
    a.pixels.resize(static_cast<std::size_t>(a.width) * static_cast<std::size_t>(a.height));
    f.read(reinterpret_cast<char*>(a.pixels.data()), static_cast<std::streamsize>(a.pixels.size()));
    if (!f)
        return fail(ErrorCode::Parse, "truncated cache");
    return a;
}

Result<std::unique_ptr<SdfFont>> SdfFont::load(const std::filesystem::path& ttf, int px) {
    auto cache = core::userDataDir() / "fontcache" /
                 (ttf.stem().string() + "_" + std::to_string(px) + ".sdf");
    Result<SdfAtlasData> data = loadAtlasCache(cache);
    if (!data) {
        data = buildSdfAtlas(ttf, px);
        if (!data)
            return std::unexpected(data.error());
        if (auto s = saveAtlasCache(*data, cache); !s)
            QXL_LOG_WARN(Gfx, "font cache not written: {}", s.error().message);
    }
    auto font = std::unique_ptr<SdfFont>(new SdfFont());
    font->data_ = std::move(*data);
    TexDesc d;
    d.width = font->data_.width;
    d.height = font->data_.height;
    d.format = TexFormat::R8;
    d.linear = true;
    font->tex_ = Texture2D(d, font->data_.pixels.data());
    return font;
}
const Glyph* SdfFont::glyph(unsigned cp) const {
    auto it = data_.glyphs.find(cp);
    if (it == data_.glyphs.end())
        it = data_.glyphs.find('?');
    return it == data_.glyphs.end() ? nullptr : &it->second;
}
glm::vec2 SdfFont::measure(std::string_view s, float sizePx) const {
    float scale = sizePx / static_cast<float>(data_.nominalPx), w = 0;
    for (unsigned cp : decodeUtf8(s))
        if (auto* g = glyph(cp))
            w += g->advance * scale;
    return {w, data_.lineHeight * scale};
}
} // namespace qlab::gfx
