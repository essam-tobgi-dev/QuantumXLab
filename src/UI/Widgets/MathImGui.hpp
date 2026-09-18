#pragma once
// Spec 20 §1/§5 — the ImGui backend of the fallback LaTeX engine: `math::MathFont` reads its
// metrics from a rasterised ImGui face and `math::MathCanvas` paints into an `ImDrawList`, so an
// equation can be laid out once (`BasicMathRenderer`, cached) and drawn anywhere a panel draws.
//
// The engine is font-agnostic; the theme's math face is Latin Modern Math (the LaTeX typeface,
// GUST font licence, `Assets/Fonts`). Glyphs the face does not carry fall back to the '?' box of
// the atlas, which is visible rather than silently blank.
#include "UI/Fonts.hpp"
#include "UI/Math/BasicMathRenderer.hpp"
#include "UI/Math/MathLayout.hpp"
#include <imgui.h>
#include <memory>
#include <string>

namespace qlab::ui {

// Metrics of the theme's math face. One OpenType math font carries every style as its own code
// points: an italic variable `x` is U+1D465 (MATHEMATICAL ITALIC SMALL X), bold is U+1D431, and
// the operators, relations, big operators and accents are the face's upright repertoire — so
// `Italic` and `Bold` are code-point mappings (`variant`), not separate faces, exactly as TeX
// does it with a math font. Without the math face (tests, a missing asset) the UI body face is
// used unmapped, and the styles collapse to weight.
class ImGuiMathFont final : public math::MathFont {
public:
    // `math` (or `regular`) and `bold` must outlive this object (they belong to the atlas).
    ImGuiMathFont(ImFont* regular, ImFont* bold) : regular_(regular), bold_(bold) {}
    explicit ImGuiMathFont(const FontSet& set)
        : regular_(set.get(FontRole::Body)), bold_(set.get(FontRole::Strong)), math_(set.get(FontRole::Math)) {}

    math::GlyphMetrics metrics(std::string_view utf8Glyph, double sizePx, math::GlyphStyle style) const override;
    double xHeight(double sizePx) const override;
    double axisHeight(double sizePx) const override;
    double ruleThickness(double sizePx) const override;

    // The face a style draws with: the math face for every style when it is loaded.
    ImFont* face(math::GlyphStyle style) const {
        if (math_ != nullptr) return math_;
        return style == math::GlyphStyle::Bold && bold_ != nullptr ? bold_ : regular_;
    }
    bool valid() const { return regular_ != nullptr || math_ != nullptr; }
    bool hasMathFace() const { return math_ != nullptr; }
    // The code point a single letter, digit or Greek letter takes in `style` (spec 20 §1): the
    // Mathematical Alphanumeric Symbols block, or `cp` itself for an upright or unmapped glyph.
    static char32_t variant(char32_t cp, math::GlyphStyle style);
    // `utf8Glyph` re-encoded through `variant` when it is one code point and the face has the
    // variant; otherwise unchanged.
    std::string styled(std::string_view utf8Glyph, math::GlyphStyle style) const;

private:
    ImFont* regular_ = nullptr;
    ImFont* bold_ = nullptr;
    ImFont* math_ = nullptr;
};

// Paints a laid-out equation into a draw list. Coordinates handed to the engine are relative to
// `origin`; the canvas adds it so a cached layout can be drawn at any screen position.
class ImGuiMathCanvas final : public math::MathCanvas {
public:
    ImGuiMathCanvas(ImDrawList* drawList, const ImGuiMathFont& font, ImVec2 origin, ImU32 color)
        : dl_(drawList), font_(&font), origin_(origin), color_(color) {}

    void drawGlyph(std::string_view utf8, double x, double baselineY, double sizePx, math::GlyphStyle style) override;
    void drawLine(double x0, double y0, double x1, double y1, double thickness) override;
    void drawRect(double x, double y, double w, double h, bool filled) override;

    void setColor(ImU32 c) { color_ = c; }

private:
    ImDrawList* dl_;
    const ImGuiMathFont* font_;
    ImVec2 origin_;
    ImU32 color_;
};

// One renderer (with its LRU layout cache, spec 20 §7) per font set. The cache is invalidated when
// the DPI or the theme changes, which is exactly when the font set is rebuilt.
class MathRenderers {
public:
    // Rebinds to `set`, dropping the cache when the face or the DPI changed (spec 20 §4, §7).
    void rebind(const FontSet& set);
    bool ready() const { return renderer_ != nullptr && font_ && font_->valid(); }
    const ImGuiMathFont& font() const { return *font_; }
    BasicMathRenderer& renderer() { return *renderer_; }

private:
    std::unique_ptr<ImGuiMathFont> font_;
    std::unique_ptr<BasicMathRenderer> renderer_;
    ImFont* boundRegular_ = nullptr;
    float boundSize_ = 0.0f;
};

} // namespace qlab::ui
