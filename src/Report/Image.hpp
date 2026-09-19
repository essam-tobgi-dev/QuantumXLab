#pragma once
// Spec 23 §8 — images. The GL capture itself belongs to `gfx::Renderer::screenshot`; this file owns
// the file format (PNG through stb_image_write) and the optional annotation strip that is baked
// under a captured viewport: title, device, time and the fidelity badges of the visible views.
#include "Data/Fidelity.hpp"
#include "Report/Types.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace qlab::report {

using Rgba = std::array<std::uint8_t, 4>;

// 8-bit RGBA, row 0 at the top (the order `stb_image_write` and `gfx::Renderer::screenshot` use).
struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;

    Image() = default;
    Image(int w, int h, Rgba fill = {0, 0, 0, 255});
    bool empty() const { return width <= 0 || height <= 0; }
    std::uint8_t* pixel(int x, int y) {
        return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    }
    const std::uint8_t* pixel(int x, int y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    }
    void set(int x, int y, Rgba c);
    Rgba get(int x, int y) const;
    void fillRect(int x, int y, int w, int h, Rgba c);
    // 5×7 bitmap text at `scale` (spec 23 §8 annotation); returns the advance in pixels.
    int drawText(int x, int y, std::string_view text, Rgba c, int scale = 1);
};

Result<Image> readPng(const std::filesystem::path& path);
Status writePng(const std::filesystem::path& path, const Image& img);

// Spec 23 §8: the badge of a view that appears in the strip.
struct ViewBadge {
    std::string view; // "Bloch", "Q-sphere", "Density"
    data::FidelityClass cls = data::FidelityClass::Exact;
    bool simulatorOnly = false;
};

struct Annotation {
    std::string title;
    std::string device;
    std::string timestamp; // empty = now
    std::vector<ViewBadge> badges;
    int scale = 2; // text scale; 2 is legible at viewport resolutions
    Rgba background{18, 20, 26, 255};
    Rgba foreground{236, 239, 244, 255};
    Rgba accent{126, 200, 227, 255};
};

// The lines the strip shows, in order (title/device/time, then the badges).
std::vector<std::string> annotationLines(const Annotation& a);
// Height in pixels the strip needs for `a` at its scale.
int annotationHeight(const Annotation& a);
// A copy of `img` with the strip baked underneath.
Image withAnnotation(const Image& img, const Annotation& a);

// Spec 23 §8 viewport export: write `img` (optionally annotated) as PNG.
Status writeViewportPng(const std::filesystem::path& path, const Image& img,
                        const Annotation* annotation = nullptr);
// The same for a PNG already written by `gfx::Renderer::screenshot`: read it back, bake the strip
// in and write it again. Keeps this module free of GL.
Status annotatePngFile(const std::filesystem::path& path, const Annotation& annotation);

} // namespace qlab::report
