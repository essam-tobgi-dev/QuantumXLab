#include "UI/Math/MathLayoutImpl.hpp"
#include "UI/Math/MathTokenizer.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::ui::math {
namespace detail {

GlyphStyle glyphStyleFor(std::string_view text, std::string_view styleName) {
    if (styleName == "mathrm" || styleName == "text" || styleName == "textrm" ||
        styleName == "operatorname" || styleName == "mathsf" || styleName == "mathtt")
        return GlyphStyle::Upright;
    if (styleName == "mathbf" || styleName == "boldsymbol" || styleName == "textbf")
        return GlyphStyle::Bold;
    std::size_t i = 0;
    char32_t cp = utf8Decode(text, i);
    bool latinLetter = (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    bool greekLower = cp >= 0x3B1 && cp <= 0x3C9;
    return (latinLetter || greekLower) ? GlyphStyle::Italic : GlyphStyle::Upright;
}

// TeX spacing table (thin=3/18 em, med=4/18, thick=5/18), simplified.
double atomSpacing(AtomClass l, AtomClass r, bool scriptLevel) {
    using A = AtomClass;
    if (scriptLevel)
        return 0.0;
    if (l == A::Rel || r == A::Rel) {
        if ((l == A::Rel && r == A::Rel) || l == A::Open || r == A::Close || l == A::Punct)
            return 0.0;
        return 5.0 / 18.0;
    }
    if (l == A::Bin || r == A::Bin) {
        if (l == A::Open || r == A::Close || l == A::Bin || (l == A::Punct))
            return 0.0;
        return 4.0 / 18.0;
    }
    if (l == A::Punct)
        return 3.0 / 18.0;
    if (l == A::Op || r == A::Op)
        return 3.0 / 18.0;
    if (l == A::Inner || r == A::Inner)
        return 3.0 / 18.0;
    return 0.0;
}

Box Layouter::glyph(std::string_view text, double size, GlyphStyle st,
                    std::optional<SourceRange> src) const {
    GlyphMetrics m = font_.metrics(text, size, st);
    Box b;
    b.kind = BoxKind::Glyph;
    b.text = std::string(text);
    b.size = size;
    b.style = st;
    b.width = m.advance;
    b.height = m.ascent;
    b.depth = m.descent;
    b.src = src;
    return b;
}
Box Layouter::textRun(std::string_view text, double size, GlyphStyle st,
                      std::optional<SourceRange> src) const {
    return glyph(text, size, st, src);
}
Box Layouter::scaledGlyph(std::string_view text, double size, double height, double depth) const {
    Box b = glyph(text, size, GlyphStyle::Upright, std::nullopt);
    b.kind = BoxKind::Scaled;
    double natural = b.height + b.depth;
    double target = height + depth;
    if (natural > 1e-9 && target > natural)
        b.width *= std::min(1.6, 1.0 + 0.15 * (target / natural - 1.0));
    b.height = height;
    b.depth = depth;
    return b;
}
Box Layouter::rule(double width, double thickness) const {
    Box b;
    b.kind = BoxKind::Rule;
    b.width = width;
    b.thickness = thickness;
    b.height = thickness;
    b.depth = 0;
    return b;
}
Box Layouter::space(double width) const {
    Box b;
    b.kind = BoxKind::Space;
    b.width = width;
    return b;
}
void Layouter::extend(Box& p, const Box& c) {
    p.width = std::max(p.width, c.x + c.width);
    p.height = std::max(p.height, c.height - c.y);
    p.depth = std::max(p.depth, c.depth + c.y);
}
Box Layouter::hbox(std::vector<Box> children) {
    Box b;
    b.kind = BoxKind::HBox;
    for (auto& c : children)
        extend(b, c);
    b.children = std::move(children);
    return b;
}

Box Layouter::layout(const MathNode& n, Ctx c) {
    if (++depth_ > 200) {
        --depth_;
        return space(0);
    }
    Box r;
    switch (n.kind) {
    case NodeKind::Row:
        r = layoutRow(n, c);
        break;
    case NodeKind::Symbol:
        r = layoutSymbol(n, c, styleName_);
        break;
    case NodeKind::Text: {
        GlyphStyle st = (n.styleName == "textbf") ? GlyphStyle::Bold
                        : (n.styleName == "textit" || n.styleName == "mathit")
                            ? GlyphStyle::Italic
                            : GlyphStyle::Upright;
        r = textRun(n.text, c.size(), st, n.src);
        break;
    }
    case NodeKind::Unknown:
        r = textRun(n.text, c.size(), GlyphStyle::Unknown, n.src);
        break;
    case NodeKind::Frac:
        r = layoutFrac(n, c);
        break;
    case NodeKind::Scripts: {
        Box base = n.children[0] ? layout(*n.children[0], c) : space(0);
        r = layoutScripts(std::move(base), n.children[1].get(), n.children[2].get(), c);
        break;
    }
    case NodeKind::Sqrt:
        r = layoutSqrt(n, c);
        break;
    case NodeKind::Accent:
        r = layoutAccent(n, c);
        break;
    case NodeKind::Style: {
        std::string saved = styleName_;
        styleName_ = n.styleName;
        r = n.children.empty() || !n.children[0] ? space(0) : layout(*n.children[0], c);
        styleName_ = saved;
        break;
    }
    case NodeKind::BigOp:
        r = layoutBigOp(n, c);
        break;
    case NodeKind::Delim:
        r = layoutDelim(n, c);
        break;
    case NodeKind::Matrix:
        r = layoutMatrix(n, c);
        break;
    case NodeKind::Space:
        r = space(n.em * c.size());
        break;
    }
    --depth_;
    return r;
}

Box Layouter::layoutSymbol(const MathNode& n, Ctx c, std::string_view styleName) {
    std::string text = n.text;
    if (!styleName.empty() && text.size() == 1)
        text = styledLetter(styleName, text[0]);
    GlyphStyle st = glyphStyleFor(n.text, styleName);
    if (styleName == "mathbb" || styleName == "mathcal" || styleName == "mathfrak")
        st = GlyphStyle::Upright;
    // `em` on a Symbol is a manual size factor (\big, \Big, … — spec 20 §3).
    double size = n.em > 0.0 ? c.size() * n.em : c.size();
    return glyph(text, size, st, n.src);
}

Box Layouter::layoutRow(const MathNode& n, Ctx c) {
    std::vector<Box> kids;
    double x = 0;
    AtomClass prev = AtomClass::Open; // no space before first
    bool first = true;
    bool scriptLevel = c.level == MathStyleLevel::Script || c.level == MathStyleLevel::ScriptScript;
    for (const auto& child : n.children) {
        if (!child)
            continue;
        AtomClass cls = child->cls;
        if (child->kind == NodeKind::Frac || child->kind == NodeKind::Sqrt ||
            child->kind == NodeKind::Delim || child->kind == NodeKind::Matrix)
            cls = AtomClass::Inner;
        if (child->kind == NodeKind::Scripts && child->children[0])
            cls = child->children[0]->cls;
        if (child->kind == NodeKind::BigOp)
            cls = AtomClass::Op;
        if (child->kind == NodeKind::Space)
            cls = prev; // spacing commands do not change class context
        Box b = layout(*child, c);
        if (child->kind != NodeKind::Space) {
            if (!first)
                x += atomSpacing(prev, cls, scriptLevel) * c.size();
            first = false;
            prev = cls;
        }
        b.x = x;
        x += b.width;
        kids.push_back(std::move(b));
    }
    Box r = hbox(std::move(kids));
    r.width = std::max(r.width, x);
    return r;
}

Box Layouter::layoutFrac(const MathNode& n, Ctx c) {
    Ctx fc = c.with(n.flag ? scriptStyle(MathStyleLevel::Text) : crampedFracStyle(c.level));
    if (n.flag)
        fc = c.with(MathStyleLevel::Script);
    Box num = n.children[0] ? layout(*n.children[0], fc) : space(0);
    Box den = n.children[1] ? layout(*n.children[1], fc) : space(0);
    double size = c.size();
    double axis = font_.axisHeight(size);
    double thick = (n.text == "binom") ? 0.0 : font_.ruleThickness(size);
    double gap = c.display() ? 0.18 * size : 0.10 * size;
    double pad = 0.08 * size;
    double w = std::max(num.width, den.width) + 2 * pad;
    centerHoriz(num, w);
    centerHoriz(den, w);
    num.y = -(axis + thick / 2 + gap + num.depth);
    den.y = -axis + thick / 2 + gap + den.height;
    std::vector<Box> kids;
    kids.push_back(std::move(num));
    if (thick > 0) {
        Box r = rule(w, thick);
        r.x = 0;
        r.y = -axis + thick / 2;
        kids.push_back(std::move(r));
    }
    kids.push_back(std::move(den));
    Box r = hbox(std::move(kids));
    r.width = w;
    if (n.text == "binom")
        return wrapDelims(std::move(r), "(", ")", c);
    return r;
}

Box Layouter::layoutScripts(Box base, const MathNode* sub, const MathNode* sup, Ctx c) {
    Ctx sc = c.with(scriptStyle(c.level));
    double size = c.size();
    double kern = 0.04 * size;
    std::vector<Box> kids;
    double x = base.width + kern;
    double supY = 0, subY = 0;
    std::optional<Box> supB, subB;
    if (sup) {
        supB = layout(*sup, sc);
        supY = -std::max(0.42 * size, base.height - 0.32 * size);
    }
    if (sub) {
        subB = layout(*sub, sc);
        subY = std::max(0.20 * size, base.depth + 0.12 * size);
    }
    if (supB && subB) {
        double supBottom = supY + supB->depth, subTop = subY - subB->height;
        double minGap = 0.12 * size;
        if (subTop - supBottom < minGap)
            subY += (minGap - (subTop - supBottom));
    }
    double w = base.width;
    kids.push_back(std::move(base));
    if (supB) {
        supB->x = x;
        supB->y = supY;
        w = std::max(w, x + supB->width);
        kids.push_back(std::move(*supB));
    }
    if (subB) {
        subB->x = x;
        subB->y = subY;
        w = std::max(w, x + subB->width);
        kids.push_back(std::move(*subB));
    }
    Box r = hbox(std::move(kids));
    r.width = w + 0.02 * size;
    return r;
}

} // namespace detail

LayoutResult layoutMath(const MathNode& root, const MathFont& font, const MathStyle& style) {
    LayoutResult lr;
    detail::Layouter L(font, lr.warnings);
    detail::Ctx c{style.display ? MathStyleLevel::Display : MathStyleLevel::Text, style.sizePx};
    lr.root = L.layout(root, c);
    lr.width = lr.root.width;
    lr.height = lr.root.height;
    lr.depth = lr.root.depth;
    return lr;
}

} // namespace qlab::ui::math
