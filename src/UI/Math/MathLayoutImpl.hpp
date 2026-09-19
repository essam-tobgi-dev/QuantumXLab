#pragma once
// Private: the layouter shared by MathLayout.cpp and MathLayoutEx.cpp.
#include "UI/Math/MathLayout.hpp"
#include "UI/Math/MathSymbols.hpp"

namespace qlab::ui::math::detail {

struct Ctx {
    MathStyleLevel level;
    double base; // base size px
    double size() const { return base * styleScale(level); }
    bool display() const { return level == MathStyleLevel::Display; }
    Ctx with(MathStyleLevel l) const { return {l, base}; }
};

class Layouter {
  public:
    Layouter(const MathFont& f, std::vector<std::string>& warnings)
        : font_(f), warnings_(warnings) {}
    Box layout(const MathNode& n, Ctx c);

    Box glyph(std::string_view text, double size, GlyphStyle st,
              std::optional<SourceRange> src) const;
    Box textRun(std::string_view text, double size, GlyphStyle st,
                std::optional<SourceRange> src) const;
    Box scaledGlyph(std::string_view text, double size, double height, double depth) const;
    Box rule(double width, double thickness) const;
    Box space(double width) const;
    static Box hbox(std::vector<Box> children);        // children already positioned via x/y
    static void extend(Box& parent, const Box& child); // grow parent bounds by a positioned child
    static double totalHeight(const Box& b) { return b.height + b.depth; }
    static void centerHoriz(Box& b, double width) { b.x += (width - b.width) / 2.0; }

    Box layoutRow(const MathNode& n, Ctx c);
    Box layoutSymbol(const MathNode& n, Ctx c, std::string_view styleName);
    Box layoutFrac(const MathNode& n, Ctx c);
    Box layoutScripts(Box base, const MathNode* sub, const MathNode* sup, Ctx c);
    Box layoutSqrt(const MathNode& n, Ctx c);
    Box layoutAccent(const MathNode& n, Ctx c);
    Box layoutBigOp(const MathNode& n, Ctx c);
    Box layoutDelim(const MathNode& n, Ctx c);
    Box layoutMatrix(const MathNode& n, Ctx c);
    Box wrapDelims(Box content, std::string_view left, std::string_view right, Ctx c) const;

    std::string styleName_; // active math alphabet (mathbf/mathbb/mathcal) or empty
    const MathFont& font_;
    std::vector<std::string>& warnings_;
    int depth_ = 0;
};

GlyphStyle glyphStyleFor(std::string_view text, std::string_view styleName);
double atomSpacing(AtomClass left, AtomClass right, bool scriptLevel);

} // namespace qlab::ui::math::detail
