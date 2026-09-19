#pragma once
// Shared helpers for the graphics oracles: hidden window, screenshot → luminance image, cube
// readback.
#include "Graphics/Graphics.hpp"
#include <cmath>
#include <filesystem>
#include <memory>
#include <stb/stb_image.h>
#include <string>
#include <vector>

namespace gfxtest {
using namespace qlab;
using namespace qlab::gfx;

inline std::unique_ptr<Window> makeHidden(int w = 320, int h = 240) {
    WindowDesc d;
    d.visible = false;
    d.width = w;
    d.height = h;
    d.vsync = false;
    auto win = Window::create(d);
    return win ? std::move(*win) : nullptr;
}

// The renderer's post image as luminance in [0, 1], row 0 at the TOP (pixel coordinates as pick()).
struct LumImage {
    int w = 0, h = 0;
    std::vector<float> lum;
    float at(int x, int y) const {
        return lum[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                   static_cast<std::size_t>(x)];
    }
};
inline LumImage grab(const Renderer& r, const std::string& name) {
    const auto out = std::filesystem::temp_directory_path() / name;
    if (!r.screenshot(out))
        return {};
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(out.string().c_str(), &w, &h, &n, 4);
    if (!px)
        return {};
    LumImage img;
    img.w = w;
    img.h = h;
    img.lum.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned char* p =
                px + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                      static_cast<std::size_t>(x)) *
                         4;
            img.lum[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                    static_cast<std::size_t>(x)] =
                (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
        }
    stbi_image_free(px);
    return img;
}

struct Stats {
    double mean = 0, stddev = 0;
    std::size_t n = 0;
};
inline Stats stats(const std::vector<float>& v) {
    Stats s;
    s.n = v.size();
    if (v.empty())
        return s;
    double sum = 0;
    for (float x : v)
        sum += x;
    s.mean = sum / static_cast<double>(v.size());
    double var = 0;
    for (float x : v)
        var += (x - s.mean) * (x - s.mean);
    s.stddev = std::sqrt(var / static_cast<double>(v.size()));
    return s;
}

// Luminance of every texel of one mip level over all six faces (RGB16F/RGBA16F cubemaps).
inline std::vector<float> cubeLuminance(const TextureCube& c, int level, int face = -1) {
    std::vector<float> out;
    for (int f = 0; f < 6; ++f) {
        if (face >= 0 && f != face)
            continue;
        auto px = c.readFace(f, level);
        for (std::size_t i = 0; i + 3 < px.size(); i += 4)
            out.push_back(0.2126f * px[i] + 0.7152f * px[i + 1] + 0.0722f * px[i + 2]);
    }
    return out;
}

// Full-resolution readback of a single-channel or RG float texture (R8 / RG16F).
inline std::vector<float> readTexture(const Texture2D& t, GLenum format, int channels) {
    std::vector<float> px(static_cast<std::size_t>(t.width()) *
                          static_cast<std::size_t>(t.height()) *
                          static_cast<std::size_t>(channels));
    glBindTexture(GL_TEXTURE_2D, t.id());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, format, GL_FLOAT, px.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return px;
}
} // namespace gfxtest
