#include "Report/Image.hpp"
#include "Data/Fidelity.hpp"

#include "Report/Font5x7.hpp"
#include <algorithm>
#include <cstdio>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

namespace qlab::report {
namespace {

// stb writes through the C stdio path; the file is written to a temporary and renamed so a reader
// never sees a half-written PNG (spec 23 §1 atomic writes).
Status renameOver(const std::filesystem::path& tmp, const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) return fail(ErrorCode::Io, "rename failed: " + ec.message());
    return {};
}

} // namespace

Image::Image(int w, int h, Rgba fill) : width(w), height(h) {
    if (w <= 0 || h <= 0) {
        width = height = 0;
        return;
    }
    rgba.resize(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < rgba.size(); i += 4)
        for (int k = 0; k < 4; ++k) rgba[i + static_cast<std::size_t>(k)] = fill[static_cast<std::size_t>(k)];
}

void Image::set(int x, int y, Rgba c) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    std::uint8_t* p = pixel(x, y);
    for (int k = 0; k < 4; ++k) p[k] = c[static_cast<std::size_t>(k)];
}

Rgba Image::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return {0, 0, 0, 0};
    const std::uint8_t* p = pixel(x, y);
    return {p[0], p[1], p[2], p[3]};
}

void Image::fillRect(int x, int y, int w, int h, Rgba c) {
    for (int j = std::max(0, y); j < std::min(height, y + h); ++j)
        for (int i = std::max(0, x); i < std::min(width, x + w); ++i) set(i, j, c);
}

int Image::drawText(int x, int y, std::string_view text, Rgba c, int scale) {
    const int s = std::max(1, scale);
    int pen = x;
    for (char ch : text) {
        const auto cols = font::glyph(ch);
        for (int col = 0; col < font::kGlyphWidth; ++col)
            for (int row = 0; row < font::kGlyphHeight; ++row)
                if ((cols[static_cast<std::size_t>(col)] >> row) & 1u)
                    fillRect(pen + col * s, y + row * s, s, s, c);
        pen += font::kAdvance * s;
    }
    return pen - x;
}

Result<Image> readPng(const std::filesystem::path& path) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* data = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
    if (!data) return fail(ErrorCode::Io, "cannot read PNG " + path.string());
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(data, data + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(data);
    return img;
}

Status writePng(const std::filesystem::path& path, const Image& img) {
    if (img.empty()) return fail(ErrorCode::InvalidArgument, "image is empty");
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    const int ok = stbi_write_png(tmp.string().c_str(), img.width, img.height, 4, img.rgba.data(), img.width * 4);
    if (!ok) {
        std::filesystem::remove(tmp, ec);
        return fail(ErrorCode::Io, "cannot write PNG " + path.string());
    }
    return renameOver(tmp, path);
}

std::vector<std::string> annotationLines(const Annotation& a) {
    std::vector<std::string> lines;
    if (!a.title.empty()) lines.push_back(a.title);
    std::string second;
    if (!a.device.empty()) second = "device " + a.device;
    const std::string when = a.timestamp.empty() ? core::isoNow() : a.timestamp;
    second += second.empty() ? when : "   " + when;
    lines.push_back(second);
    std::string badges;
    for (const auto& b : a.badges) {
        if (!badges.empty()) badges += "   ";
        badges += b.view + ": " + std::string(data::fidelityName(b.cls));
        if (b.simulatorOnly) badges += " (sim-only)";
    }
    if (!badges.empty()) lines.push_back(badges);
    return lines;
}

int annotationHeight(const Annotation& a) {
    const int s = std::max(1, a.scale);
    const int lines = static_cast<int>(annotationLines(a).size());
    const int lineHeight = (font::kGlyphHeight + 3) * s;
    return lines * lineHeight + 4 * s;
}

Image withAnnotation(const Image& img, const Annotation& a) {
    const int s = std::max(1, a.scale);
    const int strip = annotationHeight(a);
    Image out(img.width, img.height + strip, a.background);
    if (!img.empty())
        std::copy(img.rgba.begin(), img.rgba.end(), out.rgba.begin());
    // A one-pixel accent rule separates the capture from its provenance.
    out.fillRect(0, img.height, out.width, std::max(1, s / 2), a.accent);

    const int lineHeight = (font::kGlyphHeight + 3) * s;
    int y = img.height + 2 * s;
    const auto lines = annotationLines(a);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out.drawText(2 * s, y, lines[i], i == 0 ? a.accent : a.foreground, s);
        y += lineHeight;
    }
    return out;
}

Status writeViewportPng(const std::filesystem::path& path, const Image& img, const Annotation* annotation) {
    if (!annotation) return writePng(path, img);
    return writePng(path, withAnnotation(img, *annotation));
}

Status annotatePngFile(const std::filesystem::path& path, const Annotation& annotation) {
    QXL_TRY_ASSIGN(const Image img, readPng(path));
    return writePng(path, withAnnotation(img, annotation));
}

} // namespace qlab::report
