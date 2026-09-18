#pragma once
// A 5×7 bitmap font for the baked annotation strip of spec 23 §8. The strip is written by a
// headless code path (no GL context, no `gfx::SdfFont`), so the glyphs live here as data.
// Printable ASCII 0x20–0x5F; lowercase is rendered as uppercase.
#include <cstdint>
#include <span>
#include <string_view>

namespace qlab::report::font {

inline constexpr int kGlyphWidth = 5;
inline constexpr int kGlyphHeight = 7;
inline constexpr int kAdvance = 6;   // glyph width plus one column of spacing

// Five column bitmaps; bit k of a column is row k, row 0 at the top.
std::span<const std::uint8_t> glyph(char c);
// Pixel width of `text` at `scale` (no trailing spacing).
int textWidth(std::string_view text, int scale = 1);

} // namespace qlab::report::font
