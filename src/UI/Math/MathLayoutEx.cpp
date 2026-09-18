#include "UI/Math/MathLayoutImpl.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::ui::math {
namespace detail {

Box Layouter::wrapDelims(Box content, std::string_view left, std::string_view right, Ctx c) const {
    double size = c.size();
    double axis = font_.axisHeight(size);
    double half = std::max(content.height - axis, content.depth + axis);
    half = std::max(half, 0.5 * size) + 0.04 * size;
    double h = axis + half, d = half - axis;
    std::vector<Box> kids;
    double x = 0;
    if (!left.empty()) { Box l = scaledGlyph(left, size, h, d); l.x = 0; x = l.width + 0.03 * size; kids.push_back(std::move(l)); }
    content.x = x;
    x += content.width;
    kids.push_back(std::move(content));
    if (!right.empty()) { Box r = scaledGlyph(right, size, h, d); r.x = x + 0.03 * size; x = r.x + r.width; kids.push_back(std::move(r)); }
    Box r = hbox(std::move(kids));
    r.width = x;
    return r;
}

Box Layouter::layoutDelim(const MathNode& n, Ctx c) {
    Box content = n.children.empty() || !n.children[0] ? space(0) : layout(*n.children[0], c);
    return wrapDelims(std::move(content), n.text, n.text2, c);
}

Box Layouter::layoutSqrt(const MathNode& n, Ctx c) {
    Box rad = n.children[0] ? layout(*n.children[0], c) : space(0);
    double size = c.size();
    double thick = font_.ruleThickness(size);
    double gap = 0.12 * size;
    double top = rad.height + gap + thick;
    Box sign = scaledGlyph("√", size, top, rad.depth);
    std::vector<Box> kids;
    double x = 0;
    if (n.children.size() > 1 && n.children[1]) {
        Box idx = layout(*n.children[1], c.with(MathStyleLevel::ScriptScript));
        idx.x = 0;
        idx.y = -(0.55 * (top + rad.depth) - idx.depth) ;
        x = idx.width - 0.3 * sign.width;
        if (x < 0) x = 0;
        kids.push_back(std::move(idx));
    }
    sign.x = x;
    x += sign.width;
    Box bar = rule(rad.width + 0.05 * size, thick);
    bar.x = x;
    bar.y = -(rad.height + gap);
    rad.x = x + 0.03 * size;
    double w = rad.x + rad.width + 0.05 * size;
    kids.push_back(std::move(sign));
    kids.push_back(std::move(bar));
    kids.push_back(std::move(rad));
    Box r = hbox(std::move(kids));
    r.width = w;
    return r;
}

Box Layouter::layoutAccent(const MathNode& n, Ctx c) {
    Box base = n.children[0] ? layout(*n.children[0], c) : space(0);
    double size = c.size();
    bool below = n.em > 0.5;
    std::vector<Box> kids;
    double w = base.width;
    Box acc;
    if (n.flag && (n.text == "¯" || n.text == "_")) {
        acc = rule(base.width, font_.ruleThickness(size));
        acc.x = 0;
        acc.y = below ? base.depth + 0.12 * size + acc.thickness : -(base.height + 0.12 * size);
    } else if (n.flag) { // wide glyph accents (braces, arrows): stretch horizontally by drawing at base width
        acc = glyph(n.text, size, GlyphStyle::Upright, std::nullopt);
        acc.kind = BoxKind::Scaled;
        acc.width = base.width;
        acc.x = 0;
        acc.y = below ? base.depth + acc.height + 0.05 * size : -(base.height + acc.depth + 0.05 * size);
    } else {
        acc = glyph(n.text, size * 0.9, GlyphStyle::Upright, std::nullopt);
        acc.x = (base.width - acc.width) / 2.0 + 0.05 * size;
        double xh = font_.xHeight(size);
        acc.y = -(std::max(base.height, xh) - xh * 0.9);
        if (n.text == "⃗") { acc.y = -(std::max(base.height, xh) + 0.02 * size); } // combining arrow sits high
    }
    kids.push_back(std::move(base));
    kids.push_back(std::move(acc));
    Box r = hbox(std::move(kids));
    r.width = w;
    return r;
}

Box Layouter::layoutBigOp(const MathNode& n, Ctx c) {
    double size = c.size();
    bool textOp = n.styleName == "text";
    Box op = textOp ? textRun(n.text, size, GlyphStyle::Upright, n.src)
                    : glyph(n.text, c.display() ? 1.45 * size : 1.1 * size, GlyphStyle::Upright, n.src);
    if (!textOp) { // centre the operator on the axis
        double axis = font_.axisHeight(size);
        op.y = (op.height - op.depth) / 2.0 - axis;
    }
    const MathNode* sub = n.children.size() > 0 ? n.children[0].get() : nullptr;
    const MathNode* sup = n.children.size() > 1 ? n.children[1].get() : nullptr;
    bool limits = n.flag && c.display();
    if (!limits) {
        double opY = op.y; op.y = 0;
        Box s = layoutScripts(std::move(op), sub, sup, c);
        s.y = opY;
        return hbox({std::move(s)});
    }
    Ctx sc = c.with(scriptStyle(c.level));
    std::vector<Box> kids;
    double w = op.width;
    std::optional<Box> subB, supB;
    if (sub) { subB = layout(*sub, sc); w = std::max(w, subB->width); }
    if (sup) { supB = layout(*sup, sc); w = std::max(w, supB->width); }
    double gap = 0.1 * size;
    centerHoriz(op, w);
    double opTop = -op.y + op.height; // distance above baseline of op top: op.y is baseline offset
    double opBottom = op.y + op.depth;
    if (supB) { centerHoriz(*supB, w); supB->y = -(opTop + gap + supB->depth); }
    if (subB) { centerHoriz(*subB, w); subB->y = opBottom + gap + subB->height; }
    kids.push_back(std::move(op));
    if (supB) kids.push_back(std::move(*supB));
    if (subB) kids.push_back(std::move(*subB));
    Box r = hbox(std::move(kids));
    r.width = w;
    return r;
}

Box Layouter::layoutMatrix(const MathNode& n, Ctx c) {
    const std::string& env = n.text;
    bool small = env == "smallmatrix";
    Ctx cc = c.with(small ? MathStyleLevel::Script : (c.display() && (env == "aligned" || env == "align" || env == "align*" || env == "gathered" || env == "gather" || env == "split" || env == "equation" || env == "equation*" || env == "cases") ? MathStyleLevel::Display : MathStyleLevel::Text));
    double size = c.size();
    int rows = std::max(1, n.rows), cols = std::max(1, n.cols);
    std::vector<Box> cells;
    cells.reserve(n.children.size());
    for (const auto& ch : n.children) cells.push_back(ch ? layout(*ch, cc) : space(0));
    std::vector<double> colW(static_cast<std::size_t>(cols), 0.0), rowH(static_cast<std::size_t>(rows), 0.0), rowD(static_cast<std::size_t>(rows), 0.0);
    for (int r = 0; r < rows; ++r)
        for (int k = 0; k < cols; ++k) {
            std::size_t i = static_cast<std::size_t>(r * cols + k);
            if (i >= cells.size()) break;
            colW[static_cast<std::size_t>(k)] = std::max(colW[static_cast<std::size_t>(k)], cells[i].width);
            rowH[static_cast<std::size_t>(r)] = std::max(rowH[static_cast<std::size_t>(r)], cells[i].height);
            rowD[static_cast<std::size_t>(r)] = std::max(rowD[static_cast<std::size_t>(r)], cells[i].depth);
        }
    bool alignEnv = env == "aligned" || env == "align" || env == "align*" || env == "split" || env == "alignat";
    bool leftAlign = env == "cases" || env == "gathered" || env == "gather";
    double colGap = alignEnv ? 0.0 : (small ? 0.35 * size : 0.7 * size);
    double rowGap = small ? 0.1 * size : 0.25 * size;
    double y = 0, totalW = 0;
    std::vector<Box> placed;
    for (int r = 0; r < rows; ++r) {
        double x = 0;
        y += rowH[static_cast<std::size_t>(r)];
        for (int k = 0; k < cols; ++k) {
            std::size_t i = static_cast<std::size_t>(r * cols + k);
            if (i >= cells.size()) break;
            Box b = std::move(cells[i]);
            double cw = colW[static_cast<std::size_t>(k)];
            if (alignEnv) b.x = x + ((k % 2 == 0) ? (cw - b.width) : 0.0);
            else if (leftAlign) b.x = x;
            else b.x = x + (cw - b.width) / 2.0;
            b.y = y;
            placed.push_back(std::move(b));
            x += cw + (k + 1 < cols ? colGap : 0.0);
        }
        totalW = std::max(totalW, x);
        y += rowD[static_cast<std::size_t>(r)] + (r + 1 < rows ? rowGap : 0.0);
    }
    double total = y;
    double axis = font_.axisHeight(size);
    double shift = -(total / 2.0 + axis); // centre the block on the axis
    for (auto& b : placed) b.y += shift;
    Box body = hbox(std::move(placed));
    body.width = totalW;
    std::string l, rgt;
    if (env == "pmatrix") { l = "("; rgt = ")"; }
    else if (env == "bmatrix") { l = "["; rgt = "]"; }
    else if (env == "Bmatrix") { l = "{"; rgt = "}"; }
    else if (env == "vmatrix") { l = "|"; rgt = "|"; }
    else if (env == "Vmatrix") { l = "‖"; rgt = "‖"; }
    else if (env == "cases") { l = "{"; rgt = ""; }
    if (l.empty() && rgt.empty()) return body;
    return wrapDelims(std::move(body), l, rgt, c);
}

} // namespace detail

namespace {
void paintBox(const Box& b, MathCanvas& cv, double ox, double oy, const MathFont* font) {
    double ax = ox + b.x, ay = oy + b.y;
    switch (b.kind) {
    case BoxKind::Glyph: cv.drawGlyph(b.text, ax, ay, b.size, b.style); break;
    case BoxKind::Scaled: {
        // Stretch the glyph vertically so its natural extent matches height+depth.
        double natural = b.size, scale = 1.0;
        if (font) { GlyphMetrics m = font->metrics(b.text, b.size, GlyphStyle::Upright); natural = m.ascent + m.descent; if (natural > 1e-9) scale = (b.height + b.depth) / natural; }
        double sz = b.size * std::max(1.0, scale);
        double descent = 0;
        if (font) descent = font->metrics(b.text, sz, GlyphStyle::Upright).descent;
        cv.drawGlyph(b.text, ax, ay + b.depth - descent, sz, GlyphStyle::Upright);
        break;
    }
    case BoxKind::Rule: cv.drawLine(ax, ay - b.thickness / 2, ax + b.width, ay - b.thickness / 2, b.thickness); break;
    case BoxKind::Space: break;
    case BoxKind::HBox: for (const auto& c : b.children) paintBox(c, cv, ax, ay, font); break;
    }
}
void collectHits(const Box& b, double ox, double oy, std::vector<SymbolHit>& out) {
    double ax = ox + b.x, ay = oy + b.y;
    if ((b.kind == BoxKind::Glyph || b.kind == BoxKind::Scaled) && b.src)
        out.push_back({*b.src, b.text, ax, ay - b.height, b.width, b.height + b.depth});
    for (const auto& c : b.children) collectHits(c, ax, ay, out);
}
} // namespace

void paintMath(const LayoutResult& lr, MathCanvas& canvas, double x, double baselineY) {
    paintBox(lr.root, canvas, x, baselineY, nullptr);
}
void paintMath(const LayoutResult& lr, MathCanvas& canvas, double x, double baselineY, const MathFont& font) {
    paintBox(lr.root, canvas, x, baselineY, &font);
}
std::vector<SymbolHit> symbolRects(const LayoutResult& lr, double ox, double oy) {
    std::vector<SymbolHit> out;
    collectHits(lr.root, ox, oy, out);
    return out;
}
std::optional<SymbolHit> hitTestMath(const LayoutResult& lr, double ox, double oy, double px, double py) {
    for (const auto& h : symbolRects(lr, ox, oy))
        if (px >= h.x && px <= h.x + h.w && py >= h.y && py <= h.y + h.h) return h;
    return std::nullopt;
}

} // namespace qlab::ui::math
