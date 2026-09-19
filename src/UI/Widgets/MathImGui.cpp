// Spec 20 §1/§5 — ImGui backend of the LaTeX layout engine (see MathImGui.hpp).
#include "UI/Widgets/MathImGui.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::ui {
namespace {

// First code point of a UTF-8 run and how many bytes it spans (the engine hands one glyph at a
// time, but a whole ASCII run for `\text`). {0, 0} when the text is empty or malformed. Decoded
// here rather than with ImGui's `ImTextCharFromUtf8`, which lives in imgui_internal.h.
struct Decoded {
    unsigned int cp = 0;
    std::size_t bytes = 0;
};
Decoded decodeUtf8(std::string_view s) {
    if (s.empty())
        return {};
    const auto b0 = static_cast<unsigned char>(s[0]);
    std::size_t n = b0 < 0x80             ? 1
                    : (b0 & 0xE0) == 0xC0 ? 2
                    : (b0 & 0xF0) == 0xE0 ? 3
                    : (b0 & 0xF8) == 0xF0 ? 4
                                          : 0;
    if (n == 0 || n > s.size())
        return {};
    unsigned int cp = n == 1 ? b0 : b0 & (0xFFu >> (n + 1));
    for (std::size_t i = 1; i < n; ++i) {
        const auto b = static_cast<unsigned char>(s[i]);
        if ((b & 0xC0) != 0x80)
            return {};
        cp = (cp << 6) | (b & 0x3Fu);
    }
    return {cp, n};
}

float faceScale(const ImFont* font, double sizePx) {
    return font == nullptr || font->FontSize <= 0.0f ? 1.0f
                                                     : static_cast<float>(sizePx) / font->FontSize;
}

} // namespace

char32_t ImGuiMathFont::variant(char32_t cp, math::GlyphStyle style) {
    using math::GlyphStyle;
    if (style == GlyphStyle::Italic) {
        if (cp == U'h')
            return 0x210E; // PLANCK CONSTANT is the italic h
        if (cp >= U'A' && cp <= U'Z')
            return 0x1D434 + (cp - U'A');
        if (cp >= U'a' && cp <= U'z')
            return 0x1D44E + (cp - U'a');
        if (cp >= 0x391 && cp <= 0x3A9)
            return 0x1D6E2 + (cp - 0x391); // Α…Ω
        if (cp >= 0x3B1 && cp <= 0x3C9)
            return 0x1D6FC + (cp - 0x3B1); // α…ω (ς at 1D70D)
        if (cp == 0x3D5)
            return 0x1D719; // ϕ
        if (cp == 0x3F5)
            return 0x1D716; // ϵ
        if (cp == 0x2202)
            return 0x1D715; // ∂
        return cp;
    }
    if (style == GlyphStyle::Bold) {
        if (cp >= U'A' && cp <= U'Z')
            return 0x1D400 + (cp - U'A');
        if (cp >= U'a' && cp <= U'z')
            return 0x1D41A + (cp - U'a');
        if (cp >= U'0' && cp <= U'9')
            return 0x1D7CE + (cp - U'0');
        if (cp >= 0x391 && cp <= 0x3A9)
            return 0x1D6A8 + (cp - 0x391);
        if (cp >= 0x3B1 && cp <= 0x3C9)
            return 0x1D6C2 + (cp - 0x3B1);
        return cp;
    }
    return cp;
}

std::string ImGuiMathFont::styled(std::string_view utf8Glyph, math::GlyphStyle style) const {
    if (math_ == nullptr || style == math::GlyphStyle::Upright)
        return std::string(utf8Glyph);
    const Decoded d = decodeUtf8(utf8Glyph);
    if (d.cp == 0 || d.bytes != utf8Glyph.size())
        return std::string(utf8Glyph); // a run: upright text
    const char32_t v = variant(d.cp, style);
    if (v == d.cp || math_->FindGlyphNoFallback(static_cast<ImWchar>(v)) == nullptr)
        return std::string(utf8Glyph);
    std::string out;
    if (v < 0x80)
        out += static_cast<char>(v);
    else if (v < 0x800) {
        out += static_cast<char>(0xC0 | (v >> 6));
        out += static_cast<char>(0x80 | (v & 0x3F));
    } else if (v < 0x10000) {
        out += static_cast<char>(0xE0 | (v >> 12));
        out += static_cast<char>(0x80 | ((v >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (v & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (v >> 18));
        out += static_cast<char>(0x80 | ((v >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((v >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (v & 0x3F));
    }
    return out;
}

math::GlyphMetrics ImGuiMathFont::metrics(std::string_view glyphIn, double sizePx,
                                          math::GlyphStyle style) const {
    math::GlyphMetrics m;
    const ImFont* font = face(style);
    if (font == nullptr || glyphIn.empty() || sizePx <= 0.0)
        return m;
    const std::string mapped = styled(glyphIn, style);
    const std::string_view utf8Glyph = mapped;
    const float scale = faceScale(font, sizePx);

    // A single code point takes its exact ink box (TeX needs the per-glyph height and depth); a run
    // of several takes the measured advance with the face's line metrics.
    const Decoded d = decodeUtf8(utf8Glyph);
    if (d.cp != 0 && d.bytes == utf8Glyph.size()) {
        if (const ImFontGlyph* g = const_cast<ImFont*>(font)->FindGlyph(static_cast<ImWchar>(d.cp));
            g != nullptr) {
            m.advance = static_cast<double>(g->AdvanceX) * scale;
            // ImGui glyph boxes are y-down from the baseline-at-Ascent origin: convert to TeX.
            m.ascent = std::max(0.0, static_cast<double>(font->Ascent - g->Y0) * scale);
            m.descent = std::max(0.0, static_cast<double>(g->Y1 - font->Ascent) * scale);
            if (m.advance > 0.0 || m.ascent > 0.0 || m.descent > 0.0)
                return m;
        }
    }
    const ImVec2 size = const_cast<ImFont*>(font)->CalcTextSizeA(
        static_cast<float>(sizePx), FLT_MAX, 0.0f, utf8Glyph.data(),
        utf8Glyph.data() + utf8Glyph.size());
    m.advance = size.x;
    m.ascent = static_cast<double>(font->Ascent) * scale;
    m.descent = static_cast<double>(-font->Descent) * scale;
    return m;
}

double ImGuiMathFont::xHeight(double sizePx) const {
    const ImFont* font = face(math::GlyphStyle::Upright);
    if (font == nullptr)
        return math::MathFont::xHeight(sizePx);
    if (const ImFontGlyph* g = const_cast<ImFont*>(font)->FindGlyph(static_cast<ImWchar>('x'));
        g != nullptr)
        return static_cast<double>(font->Ascent - g->Y0) * faceScale(font, sizePx);
    return math::MathFont::xHeight(sizePx);
}

double ImGuiMathFont::axisHeight(double sizePx) const {
    // TeX's axis: the height of the fraction bar, half the x-height of the face.
    return 0.5 * xHeight(sizePx);
}

double ImGuiMathFont::ruleThickness(double sizePx) const {
    return std::max(1.0, 0.045 * sizePx);
}

// ---------------------------------------------------------------- canvas

void ImGuiMathCanvas::drawGlyph(std::string_view utf8In, double x, double baselineY, double sizePx,
                                math::GlyphStyle style) {
    if (dl_ == nullptr || utf8In.empty() || sizePx <= 0.0)
        return;
    ImFont* font = font_->face(style);
    if (font == nullptr)
        return;
    const std::string mapped = font_->styled(utf8In, style);
    const std::string_view utf8 = mapped;
    // AddText positions the LINE BOX; the baseline sits Ascent below its top.
    const float top = static_cast<float>(baselineY) - font->Ascent * faceScale(font, sizePx);
    dl_->AddText(font, static_cast<float>(sizePx),
                 ImVec2(origin_.x + static_cast<float>(x), origin_.y + top), color_, utf8.data(),
                 utf8.data() + utf8.size());
}

void ImGuiMathCanvas::drawLine(double x0, double y0, double x1, double y1, double thickness) {
    if (dl_ == nullptr)
        return;
    dl_->AddLine(ImVec2(origin_.x + static_cast<float>(x0), origin_.y + static_cast<float>(y0)),
                 ImVec2(origin_.x + static_cast<float>(x1), origin_.y + static_cast<float>(y1)),
                 color_, std::max(1.0f, static_cast<float>(thickness)));
}

void ImGuiMathCanvas::drawRect(double x, double y, double w, double h, bool filled) {
    if (dl_ == nullptr)
        return;
    const ImVec2 a(origin_.x + static_cast<float>(x), origin_.y + static_cast<float>(y));
    const ImVec2 b(a.x + static_cast<float>(w), a.y + static_cast<float>(h));
    if (filled)
        dl_->AddRectFilled(a, b, color_);
    else
        dl_->AddRect(a, b, color_);
}

// ---------------------------------------------------------------- renderer cache

void MathRenderers::rebind(const FontSet& set) {
    ImFont* regular = set.get(FontRole::Body);
    const float size = set.size(FontRole::Body);
    if (renderer_ != nullptr && regular == boundRegular_ && size == boundSize_)
        return;
    font_ = std::make_unique<ImGuiMathFont>(set);
    renderer_ = std::make_unique<BasicMathRenderer>(*font_);
    boundRegular_ = regular;
    boundSize_ = size;
}

} // namespace qlab::ui
