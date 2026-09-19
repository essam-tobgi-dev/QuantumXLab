// Spec 23 §8 — the circuit diagram as standalone SVG: paths and text over the same layout the GL
// canvas draws, with the UI font named and a fallback stack. Pure (no GL, no ImGui), so a report
// can ask for it without a window. The palette is the print palette, not the UI theme: an exported
// figure goes into a document, which is light whatever the app's theme is.
#include "Viz/Views/CircuitView.hpp"
#include "Viz/Views/CircuitViewImpl.hpp"
#include <cmath>
#include <format>

namespace qlab::viz {
namespace {

constexpr double kPx = 34.0; // px per layout unit in the exported file
constexpr const char* kInk = "#1b1f24";
constexpr const char* kFaint = "#5a626b";
constexpr const char* kPaper = "#ffffff";
constexpr const char* kWarn = "#b5651d";
constexpr const char* kAccent = "#2f6fd0";

std::string esc(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::string num(double v) {
    std::string s = std::format("{:.2f}", v);
    while (s.size() > 1 && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}

// Emitter in SVG pixels; `x`/`y` convert from layout units.
struct Svg {
    std::string out;
    double ox = 0.0, oy = 0.0;
    double x(double lx) const { return (lx - ox) * kPx; }
    double y(double ly) const { return (ly - oy) * kPx; }
    void line(double x0, double y0, double x1, double y1, const char* stroke, double w,
              const char* dash = nullptr) {
        out += std::format(R"(<line x1="{}" y1="{}" x2="{}" y2="{}" stroke="{}" stroke-width="{}")",
                           num(x(x0)), num(y(y0)), num(x(x1)), num(y(y1)), stroke, num(w));
        if (dash)
            out += std::format(R"( stroke-dasharray="{}")", dash);
        out += "/>\n";
    }
    void rect(const Rect& r, const char* fill, const char* stroke, double w) {
        out += std::format(
            R"(<rect x="{}" y="{}" width="{}" height="{}" rx="3" fill="{}" stroke="{}" stroke-width="{}"/>)"
            "\n",
            num(x(r.x0)), num(y(r.y0)), num(r.width() * kPx), num(r.height() * kPx), fill, stroke,
            num(w));
    }
    void circle(double cxL, double cyL, double rL, const char* fill, const char* stroke, double w) {
        out += std::format(
            R"(<circle cx="{}" cy="{}" r="{}" fill="{}" stroke="{}" stroke-width="{}"/>)"
            "\n",
            num(x(cxL)), num(y(cyL)), num(rL * kPx), fill, stroke, num(w));
    }
    // Half-circle bulging upward (SVG's y grows down, so sweep-flag 1 goes over the top).
    void arcUp(double cxL, double cyL, double rL, const char* stroke, double w) {
        out += std::format(
            R"(<path d="M {} {} A {} {} 0 0 1 {} {}" fill="none" stroke="{}" stroke-width="{}"/>)"
            "\n",
            num(x(cxL - rL)), num(y(cyL)), num(rL * kPx), num(rL * kPx), num(x(cxL + rL)),
            num(y(cyL)), stroke, num(w));
    }
    void text(double lx, double ly, std::string_view s, double sizePx, const char* fill,
              const char* anchor = "middle") {
        if (s.empty())
            return;
        out += std::format(R"(<text x="{}" y="{}" font-size="{}" fill="{}" text-anchor="{}" )"
                           R"(dominant-baseline="central">{}</text>)"
                           "\n",
                           num(x(lx)), num(y(ly)), num(sizePx), fill, anchor, esc(s));
    }
};

void glyphSvg(Svg& s, const layout::CircuitLayout& lay, const layout::Glyph& g) {
    using layout::GlyphKind;
    const double cx = g.bounds.cx();
    const char* mark = g.routingSwap ? kWarn : kInk;
    const auto rowY = [&](std::uint32_t r) { return lay.rowY(r); };
    if (g.rowMax > g.rowMin && !lay.timed && g.kind != GlyphKind::Barrier &&
        g.kind != GlyphKind::Region)
        s.line(cx, rowY(g.rowMin), cx, rowY(g.rowMax), mark, 1.4);
    if (lay.timed) {
        if (g.kind == GlyphKind::Cx || g.kind == GlyphKind::Cz || g.kind == GlyphKind::Swap)
            for (const Rect& p : g.parts)
                if (p.width() > 0.03)
                    s.rect(p, kPaper, "#c8ccd2", 1.1); // the gate's duration band
        for (std::size_t k = 1; k < g.parts.size(); ++k)
            s.line(g.parts[k - 1].cx(), g.parts[k - 1].cy(), g.parts[k].cx(), g.parts[k].cy(), mark,
                   1.3);
    }
    for (std::size_t k = 0; k < g.controlRows.size(); ++k) {
        const bool open = k < g.negControl.size() && g.negControl[k] != 0;
        s.circle(cx, rowY(g.controlRows[k]), detail::kControlDot, open ? kPaper : mark, mark, 1.4);
    }

    switch (g.kind) {
    case GlyphKind::Barrier:
        s.line(cx, rowY(g.rowMin) - 0.45, cx, rowY(g.rowMax) + 0.45, kFaint, 1.4, "5 4");
        return;
    case GlyphKind::Cx:
        for (std::uint32_t t : g.targetRows) {
            const double y = rowY(t);
            s.circle(cx, y, detail::kTargetRing, "none", mark, 1.6);
            s.line(cx - detail::kTargetRing, y, cx + detail::kTargetRing, y, mark, 1.6);
            s.line(cx, y - detail::kTargetRing, cx, y + detail::kTargetRing, mark, 1.6);
        }
        return;
    case GlyphKind::Cz:
        for (std::uint32_t t : g.targetRows)
            s.circle(cx, rowY(t), detail::kControlDot, mark, mark, 1.4);
        return;
    case GlyphKind::Swap:
        for (std::uint32_t t : g.targetRows) {
            const double y = rowY(t), a = detail::kSwapArm;
            s.line(cx - a, y - a, cx + a, y + a, mark, 1.8);
            s.line(cx - a, y + a, cx + a, y - a, mark, 1.8);
        }
        if (g.routingSwap)
            s.text(cx, rowY(g.rowMin) - 0.42, "routed", 9.0, kWarn);
        return;
    case GlyphKind::Region:
        s.rect(g.bounds, "none", kAccent, 1.3);
        s.text(g.bounds.x0 + 0.08, g.bounds.y0 - 0.14, g.label, 11.0, kAccent, "start");
        if (g.elseColumn && *g.elseColumn < lay.columnX.size()) {
            const double x = lay.columnX[*g.elseColumn] - 0.15;
            s.line(x, g.bounds.y0, x, g.bounds.y1, kAccent, 1.2, "5 4");
            s.text(x, g.bounds.y0 - 0.14, "else", 10.0, kAccent);
        }
        return;
    case GlyphKind::Classical:
        s.rect(g.bounds, kPaper, "#c8ccd2", 1.1);
        s.text(g.bounds.cx(), g.bounds.cy(), g.label, 10.0, kFaint);
        return;
    default:
        break;
    }

    const Rect box = lay.timed && !g.parts.empty() ? g.parts.front() : detail::targetBox(lay, g);
    if (lay.timed)
        for (std::size_t k = 1; k < g.parts.size() && k < g.targetRows.size(); ++k)
            s.rect(g.parts[k], kPaper, kInk, 1.3);
    s.rect(box, kPaper, mark, 1.4);
    if (g.kind == GlyphKind::Delay)
        for (double hx = box.x0 + 0.08; hx < box.x1; hx += 0.11)
            s.line(hx, box.y1, std::min(hx + 0.16, box.x1), box.y0, "#c8ccd2", 0.9);
    if (g.kind == GlyphKind::Measure) {
        s.arcUp(box.cx(), box.cy() + 0.12, 0.2, kInk, 1.4); // meter face with its needle
        s.line(box.cx(), box.cy() + 0.12, box.cx() + 0.15, box.cy() - 0.09, kInk, 1.4);
        if (g.classicalRow) {
            const double y1 = rowY(*g.classicalRow), gap = detail::kClassicalGap;
            s.line(cx - gap, box.y1, cx - gap, y1, kFaint, 1.1);
            s.line(cx + gap, box.y1, cx + gap, y1, kFaint, 1.1);
            s.line(cx - 0.1, y1 - 0.1, cx, y1, kFaint, 1.1);
            s.line(cx + 0.1, y1 - 0.1, cx, y1, kFaint, 1.1);
            s.text(cx + 0.16, y1 - 0.22, g.params, 9.0, kFaint, "start");
        }
        return;
    }
    if (g.params.empty() || box.height() < 0.55) {
        s.text(box.cx(), box.cy(), g.params.empty() ? g.label : g.label + "(" + g.params + ")",
               12.0, kInk);
    } else {
        s.text(box.cx(), box.cy() - 0.12, g.label, 12.0, kInk);
        s.text(box.cx(), box.cy() + 0.16, g.params, 10.0, kFaint);
    }
}

} // namespace

std::string CircuitView::exportSvg() const {
    if (layout_.rows.empty())
        return {};
    const Rect content = detail::contentExtent(layout_);
    Svg s;
    s.ox = content.x0;
    s.oy = content.y0;
    const double w = content.width() * kPx, h = content.height() * kPx;
    s.out = std::format(
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="{}" height="{}" viewBox="0 0 {} {}" )"
        R"(font-family="Inter, 'Helvetica Neue', Helvetica, Arial, sans-serif">)"
        "\n",
        num(w), num(h), num(w), num(h));
    s.out += std::format(R"(<title>{} circuit</title>)"
                         "\n"
                         R"(<rect width="{}" height="{}" fill="{}"/>)"
                         "\n",
                         circuitStageName(shown_), num(w), num(h), kPaper);
    for (std::uint32_t row = 0; row < layout_.rows.size(); ++row) {
        const layout::WireLabel& wl = layout_.rows[row];
        const double y = layout_.rowY(row);
        if (wl.classical) {
            s.line(0.0, y - detail::kClassicalGap, layout_.width, y - detail::kClassicalGap, kFaint,
                   1.1);
            s.line(0.0, y + detail::kClassicalGap, layout_.width, y + detail::kClassicalGap, kFaint,
                   1.1);
        } else {
            s.line(0.0, y, layout_.width, y, kFaint, 1.2);
        }
        s.text(-0.12, y, detail::wireLabelText(wl), 11.0, kInk, "end");
    }
    for (const layout::Glyph& g : layout_.glyphs)
        if (g.kind == layout::GlyphKind::Region)
            glyphSvg(s, layout_, g);
    for (const layout::Glyph& g : layout_.glyphs)
        if (g.kind != layout::GlyphKind::Region)
            glyphSvg(s, layout_, g);
    s.out += "</svg>\n";
    return s.out;
}

} // namespace qlab::viz
