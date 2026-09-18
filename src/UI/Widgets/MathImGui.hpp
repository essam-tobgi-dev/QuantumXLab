#pragma once
// Spec 20 §1/§5 — the ImGui backend of the fallback LaTeX engine: `math::MathFont` reads its
// metrics from a rasterised ImGui face and `math::MathCanvas` paints into an `ImDrawList`, so an
// equation can be laid out once (`BasicMathRenderer`, cached) and drawn anywhere a panel draws.
//
// The engine is font-agnostic; `Assets/Lang/theme.json` pins Inter for math because STIX Two Math
// is not vendored (noted there and in SPEC_DEVIATIONS.md). Glyphs the face does not carry fall back
// to the '?' box of the atlas, which is visible rather than silently blank.
#include "UI/Fonts.hpp"
#include "UI/Math/BasicMathRenderer.hpp"
#include "UI/Math/MathLayout.hpp"
#include <imgui.h>
#include <memory>

namespace qlab::ui {

// Metrics of the theme's math face. Italic is the default style of a maths variable; the vendored
// Inter has no italic, so `Italic` and `Upright` share the regular face and `Bold` uses SemiBold —
// the distinction survives as weight, which is what the layout engine needs for spacing.
class ImGuiMathFont final : public math::MathFont {
public:
    // `regular` and `bold` must outlive this object (they belong to the atlas).
    ImGuiMathFont(ImFont* regular, ImFont* bold) : regular_(regular), bold_(bold) {}
    explicit ImGuiMathFont(const FontSet& set)
        : regular_(set.get(FontRole::Body)), bold_(set.get(FontRole::Strong)) {}

    math::GlyphMetrics metrics(std::string_view utf8Glyph, double sizePx, math::GlyphStyle style) const override;
    double xHeight(double sizePx) const override;
    double axisHeight(double sizePx) const override;
    double ruleThickness(double sizePx) const override;

    ImFont* face(math::GlyphStyle style) const { return style == math::GlyphStyle::Bold && bold_ != nullptr ? bold_ : regular_; }
    bool valid() const { return regular_ != nullptr; }

private:
    ImFont* regular_ = nullptr;
    ImFont* bold_ = nullptr;
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
