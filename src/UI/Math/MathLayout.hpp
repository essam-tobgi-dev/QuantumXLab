#pragma once
// Layout boxes, font/canvas interfaces, and the layout entry point (spec 20 §6–§7).
#include "UI/Math/MathAst.hpp"
#include <optional>
#include <string>
#include <vector>

namespace qlab::ui::math {

enum class GlyphStyle : std::uint8_t { Italic, Upright, Bold, Unknown };

struct GlyphMetrics { double advance = 0, ascent = 0, descent = 0; };

// Font metrics provider. Sizes are pixels. Implementations: ImGui font atlas (UI), monospace stub (tests).
class MathFont {
public:
    virtual ~MathFont() = default;
    virtual GlyphMetrics metrics(std::string_view utf8Glyph, double sizePx, GlyphStyle style) const = 0;
    virtual double xHeight(double sizePx) const { return 0.45 * sizePx; }
    virtual double axisHeight(double sizePx) const { return 0.25 * sizePx; }
    virtual double ruleThickness(double sizePx) const { return std::max(1.0, 0.05 * sizePx); }
};

// Drawing sink. Coordinates: x right, y down; `y` of drawGlyph is the baseline.
class MathCanvas {
public:
    virtual ~MathCanvas() = default;
    virtual void drawGlyph(std::string_view utf8, double x, double baselineY, double sizePx, GlyphStyle style) = 0;
    virtual void drawLine(double x0, double y0, double x1, double y1, double thickness) = 0;
    virtual void drawRect(double x, double y, double w, double h, bool filled) = 0;
};

enum class BoxKind : std::uint8_t { HBox, Glyph, Rule, Space, Scaled /*glyph stretched to height+depth*/ };

struct Box {
    BoxKind kind = BoxKind::HBox;
    double x = 0, y = 0;          // offset of this box's origin (baseline point) relative to parent origin
    double width = 0, height = 0, depth = 0; // TeX: height above baseline, depth below
    std::string text;             // glyph text for Glyph/Scaled
    double size = 0;              // glyph size px
    GlyphStyle style = GlyphStyle::Italic;
    double thickness = 0;         // Rule thickness
    std::optional<SourceRange> src; // hit region source span (symbols only)
    std::vector<Box> children;
};

struct MathStyle {
    double sizePx = 18.0;
    bool display = true;
};

struct LayoutResult {
    Box root;
    double width = 0, height = 0, depth = 0; // overall; total pixel height = height + depth
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

struct SymbolHit {
    SourceRange src;
    std::string text;
    double x = 0, y = 0, w = 0, h = 0; // absolute rect in the painted coordinate frame
};

// Layout the tree. Never throws; malformed input yields an empty box plus warnings.
LayoutResult layoutMath(const MathNode& root, const MathFont& font, const MathStyle& style);

// Paint a layout at (x, baselineY) — the root's baseline.
void paintMath(const LayoutResult& lr, MathCanvas& canvas, double x, double baselineY);
// Overload with font metrics so stretched delimiters are scaled exactly.
void paintMath(const LayoutResult& lr, MathCanvas& canvas, double x, double baselineY, const MathFont& font);

// Find the symbol under (px, py) given the paint origin.
std::optional<SymbolHit> hitTestMath(const LayoutResult& lr, double originX, double baselineY, double px, double py);

// All symbol hit rectangles (for term highlighting, spec 20 §7).
std::vector<SymbolHit> symbolRects(const LayoutResult& lr, double originX, double baselineY);

} // namespace qlab::ui::math
