// Spec 21 §3.13 — circuit diagram: the GL scene (wires, glyphs, playhead, time axis). Every mark
// goes through the renderer's overlay line batch and the SDF text batch, in that order: the batch
// expands a segment into a screen-space quad of exactly `widthPx` pixels, so a fill keeps its
// colour (the box mesh carries baked shading) and follows the zoom, and marks painted later cover
// the wires underneath — a mesh would be drawn in the main pass, i.e. UNDER every wire.
#include "Viz/Math/Color.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Views/CircuitView.hpp"
#include "Viz/Views/CircuitViewImpl.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string_view>

namespace qlab::viz {
namespace {

using detail::kClassicalGap;
using detail::kControlDot;
using detail::kSwapArm;
using detail::kTargetRing;
using layout::Glyph;
using layout::GlyphKind;

glm::vec4 ink(const glm::vec4& color, float alpha) {
    return GlBackend::exact(glm::vec3(color), alpha);
}

// Layout units → the renderer's world frame (x right, y up): the layout's y grows down the page.
struct Pen {
    gfx::Renderer* r = nullptr;
    glm::dvec2 center{0.0};
    double zoom = 1.0;
    float px = 1.0f;

    glm::dvec3 at(double x, double y, double z = 0.0) const {
        return {x - center.x, center.y - y, z};
    }
    void line(glm::dvec2 a, glm::dvec2 b, const glm::vec4& c, float widthPx,
              float dashPx = 0.0f) const {
        r->linesNoDepth().segment(at(a.x, a.y), at(b.x, b.y), c, widthPx * px, dashPx * px);
    }
    // Filled rectangle: a horizontal segment along the midline, as many pixels wide as it is tall.
    void fill(const Rect& b, const glm::vec4& c) const {
        if (b.height() <= 0.0 || b.width() <= 0.0)
            return;
        line({b.x0, b.cy()}, {b.x1, b.cy()}, c, static_cast<float>(b.height() * zoom));
    }
    void frame(const Rect& b, const glm::vec4& c, float widthPx, float dashPx = 0.0f) const {
        line({b.x0, b.y0}, {b.x1, b.y0}, c, widthPx, dashPx);
        line({b.x1, b.y0}, {b.x1, b.y1}, c, widthPx, dashPx);
        line({b.x1, b.y1}, {b.x0, b.y1}, c, widthPx, dashPx);
        line({b.x0, b.y1}, {b.x0, b.y0}, c, widthPx, dashPx);
    }
    // Filled disc from horizontal chords — a mesh would land under the wires (see the file header).
    void dot(glm::dvec2 p, double radius, const glm::vec4& c) const {
        constexpr int kChords = 7;
        for (int k = 0; k < kChords; ++k) {
            const double t = 2.0 * (k + 0.5) / kChords - 1.0;
            const double half = radius * std::sqrt(std::max(0.0, 1.0 - t * t));
            line({p.x - half, p.y + radius * t}, {p.x + half, p.y + radius * t}, c,
                 static_cast<float>(2.0 * radius * zoom / kChords) + 0.5f);
        }
    }
    // Angles are layout angles: the layout's y grows down the page, so t ∈ (π, 2π) is the upper
    // half.
    void arc(glm::dvec2 p, double radius, double a0, double a1, const glm::vec4& c, float widthPx,
             int seg) const {
        for (int k = 0; k < seg; ++k) {
            const double t0 = a0 + (a1 - a0) * k / seg, t1 = a0 + (a1 - a0) * (k + 1) / seg;
            line({p.x + radius * std::cos(t0), p.y + radius * std::sin(t0)},
                 {p.x + radius * std::cos(t1), p.y + radius * std::sin(t1)}, c, widthPx);
        }
    }
    void ring(glm::dvec2 p, double radius, const glm::vec4& c, float widthPx, int seg = 24) const {
        arc(p, radius, 0.0, 2.0 * std::numbers::pi, c, widthPx, seg);
    }
    void label(glm::dvec2 p, std::string_view s, float sizePx, const glm::vec4& c,
               gfx::TextAnchor a = gfx::TextAnchor::Center) const {
        if (sizePx >= 6.0f && !s.empty())
            r->text().label3D(at(p.x, p.y), s, sizePx, c, a);
    }
};

// Name and parameters inside a box; the parameters take a second line when there is room, and a box
// too narrow to hold them (a virtual gate's tick on the time axis) is labelled above instead.
void boxLabel(const Pen& pen, const Rect& box, const Glyph& g, const glm::vec4& text,
              float fontPx) {
    const std::string full = g.params.empty() ? g.label : g.label + "(" + g.params + ")";
    if (box.width() < 0.34)
        return pen.label({box.cx(), box.y0 - 0.05}, full, 0.8f * fontPx, text,
                         gfx::TextAnchor::BottomCenter);
    if (g.params.empty() || box.height() < 0.55)
        return pen.label({box.cx(), box.cy()}, g.label, fontPx, text);
    pen.label({box.cx(), box.cy() - 0.12}, g.label, fontPx, text);
    pen.label({box.cx(), box.cy() + 0.16}, g.params, 0.82f * fontPx, ink(text, 0.8f));
}

// One glyph. `alpha` carries the playhead dimming, `outline` marks the gate the playhead is on.
void drawGlyph(const Pen& pen, const VizTheme& theme, const layout::CircuitLayout& lay,
               const Glyph& g, float alpha, bool outline, float fontPx) {
    const glm::vec4 stroke = ink(theme.textPrimary, alpha);
    const glm::vec4 faint = ink(theme.textSecondary, 0.85f * alpha);
    const glm::vec4 fill = ink(theme.bgRaised, 0.97f * alpha);
    const glm::vec4 mark = ink(g.routingSwap ? theme.warn : theme.textPrimary, alpha);
    const glm::vec4 hilite = ink(theme.accent, 1.0f);
    const double cx = g.bounds.cx();
    const auto rowY = [&](std::uint32_t row) { return lay.rowY(row); };
    const auto span = [&] {
        if (g.rowMax > g.rowMin && !lay.timed)
            pen.line({cx, rowY(g.rowMin)}, {cx, rowY(g.rowMax)}, mark, 1.6f);
    };
    const auto controls = [&] {
        for (std::size_t k = 0; k < g.controlRows.size(); ++k) {
            const glm::dvec2 c{cx, rowY(g.controlRows[k])};
            if (k < g.negControl.size() && g.negControl[k] != 0) { // open dot: active on |0⟩
                pen.dot(c, kControlDot, ink(theme.bgPanel, alpha));
                pen.ring(c, kControlDot, mark, 1.6f, 16);
            } else {
                pen.dot(c, kControlDot, mark);
            }
        }
    };
    // Timed layout: a band per wire shows how long the gate holds it — a gate drawn as marks rather
    // than a box (CX, CZ, SWAP) would otherwise hide its duration — and a line joins the wires, so
    // a concurrent gate between them stays visible (spec 21 §3.13).
    if (lay.timed && g.kind != GlyphKind::Region) {
        if (g.kind == GlyphKind::Cx || g.kind == GlyphKind::Cz || g.kind == GlyphKind::Swap)
            for (const Rect& p : g.parts)
                if (p.width() > 0.03) {
                    pen.fill(p, fill);
                    pen.frame(p, ink(theme.border, alpha), 1.2f);
                }
        for (std::size_t k = 1; k < g.parts.size(); ++k)
            pen.line({g.parts[k - 1].cx(), g.parts[k - 1].cy()}, {g.parts[k].cx(), g.parts[k].cy()},
                     mark, 1.4f);
    }

    switch (g.kind) {
    case GlyphKind::Barrier:
        pen.line({cx, rowY(g.rowMin) - 0.45}, {cx, rowY(g.rowMax) + 0.45}, faint, 1.6f, 5.0f);
        return;
    case GlyphKind::Cx:
        span();
        controls();
        for (std::uint32_t t : g.targetRows) {
            const glm::dvec2 c{cx, rowY(t)};
            pen.ring(c, kTargetRing, mark, 1.8f);
            pen.line({c.x - kTargetRing, c.y}, {c.x + kTargetRing, c.y}, mark, 1.8f);
            pen.line({c.x, c.y - kTargetRing}, {c.x, c.y + kTargetRing}, mark, 1.8f);
        }
        if (outline)
            pen.frame(g.bounds.inset(-0.06), hilite, 2.0f);
        return;
    case GlyphKind::Cz:
        span();
        controls();
        for (std::uint32_t t : g.targetRows)
            pen.dot({cx, rowY(t)}, kControlDot, mark);
        if (outline)
            pen.frame(g.bounds.inset(-0.06), hilite, 2.0f);
        return;
    case GlyphKind::Swap:
        span();
        controls();
        for (std::uint32_t t : g.targetRows) {
            const double y = rowY(t);
            pen.line({cx - kSwapArm, y - kSwapArm}, {cx + kSwapArm, y + kSwapArm}, mark, 2.0f);
            pen.line({cx - kSwapArm, y + kSwapArm}, {cx + kSwapArm, y - kSwapArm}, mark, 2.0f);
        }
        if (g.routingSwap)
            pen.label({cx, rowY(g.rowMin) - 0.4}, "routed", 0.72f * fontPx, ink(theme.warn, alpha));
        if (outline)
            pen.frame(g.bounds.inset(-0.06), hilite, 2.0f);
        return;
    case GlyphKind::Region: // bracketed block with its branch / loop header
        pen.frame(g.bounds, ink(theme.accent, 0.55f * alpha), 1.6f, 6.0f);
        pen.label({g.bounds.x0 + 0.06, g.bounds.y0 - 0.05}, g.label, 0.9f * fontPx,
                  ink(theme.accent, alpha), gfx::TextAnchor::BottomLeft);
        if (g.elseColumn && *g.elseColumn < lay.columnX.size()) {
            const double x = lay.columnX[*g.elseColumn] - 0.15;
            pen.line({x, g.bounds.y0}, {x, g.bounds.y1}, ink(theme.accent, 0.55f * alpha), 1.4f,
                     5.0f);
            pen.label({x, g.bounds.y0 - 0.05}, "else", 0.85f * fontPx, ink(theme.accent, alpha),
                      gfx::TextAnchor::BottomCenter);
        }
        return;
    case GlyphKind::Classical:
        pen.fill(g.bounds, fill);
        pen.frame(g.bounds, ink(theme.border, alpha), 1.2f);
        pen.label({g.bounds.cx(), g.bounds.cy()}, g.label, 0.9f * fontPx, faint);
        return;
    default:
        break;
    }

    // Boxed glyphs: Box (with its control dots), Measure, Reset, Delay. The timed layout gives one
    // part per wire, targets first; a control wire keeps its dot rather than a second box.
    span();
    const Rect box = lay.timed && !g.parts.empty() ? g.parts.front() : detail::targetBox(lay, g);
    if (lay.timed)
        for (std::size_t k = 1; k < g.parts.size() && k < g.targetRows.size(); ++k) {
            pen.fill(g.parts[k], fill);
            pen.frame(g.parts[k], ink(theme.border, alpha), 1.4f);
        }
    pen.fill(box, fill);
    pen.frame(box, outline ? hilite : ink(theme.border, alpha), outline ? 2.2f : 1.5f);
    controls();
    if (g.kind == GlyphKind::Delay) // hatched, so an idle window never reads as a gate
        for (double x = box.x0 + 0.08; x < box.x1; x += 0.11)
            pen.line({x, box.y1}, {std::min(x + 0.16, box.x1), box.y0},
                     ink(theme.textDisabled, alpha), 1.0f);
    if (g.kind == GlyphKind::Measure) {
        const glm::dvec2 m{box.cx(),
                           box.cy() + 0.12}; // meter face: an arc with its needle (T01 §7)
        pen.arc(m, 0.2, std::numbers::pi, 2.0 * std::numbers::pi, stroke, 1.6f, 14);
        pen.line(m, {m.x + 0.15, m.y - 0.21}, stroke, 1.6f);
        if (g.classicalRow) { // double line down to the register, with the bit name beside it
            const double y0 = box.y1, y1 = rowY(*g.classicalRow);
            pen.line({cx - kClassicalGap, y0}, {cx - kClassicalGap, y1}, faint, 1.3f);
            pen.line({cx + kClassicalGap, y0}, {cx + kClassicalGap, y1}, faint, 1.3f);
            pen.line({cx - 0.1, y1 - 0.1}, {cx, y1}, faint, 1.3f);
            pen.line({cx + 0.1, y1 - 0.1}, {cx, y1}, faint, 1.3f);
            pen.label({cx + 0.14, y1 - 0.1}, g.params, 0.8f * fontPx, faint,
                      gfx::TextAnchor::BottomLeft);
        }
        return;
    }
    boxLabel(pen, box, g, stroke, fontPx);
}

} // namespace

GlCanvas::SceneFn CircuitView::scene(const VizTheme& theme, float pxScale) const {
    return [this, &theme, pxScale](gfx::Renderer& r, GlBackend&) {
        const glm::vec2 body = bodySize();
        const Pen pen{&r, pan_ + glm::dvec2(body.x, body.y) / (2.0 * zoom_), zoom_, pxScale};
        const auto fontPx = static_cast<float>(std::clamp(0.30 * zoom_, 6.0, 22.0)) * pxScale;
        const glm::vec4 wire = ink(theme.textSecondary, 0.7f);

        // Wires and their labels; a classical register is a double line (spec 21 §3.13).
        for (std::uint32_t row = 0; row < layout_.rows.size(); ++row) {
            const layout::WireLabel& w = layout_.rows[row];
            const double y = layout_.rowY(row);
            if (w.classical) {
                pen.line({0.0, y - kClassicalGap}, {layout_.width, y - kClassicalGap}, wire, 1.2f);
                pen.line({0.0, y + kClassicalGap}, {layout_.width, y + kClassicalGap}, wire, 1.2f);
            } else {
                pen.line({0.0, y}, {layout_.width, y}, wire, 1.4f);
            }
            // Centred in the label gutter, so the longest label still clears the first column.
            pen.label({-0.5 * detail::kLabelGutter, y}, detail::wireLabelText(w), fontPx,
                      GlBackend::exact(w.classical ? theme.textSecondary : theme.textPrimary),
                      gfx::TextAnchor::Center);
        }

        // Time axis of the scheduled layout, labelled in ns.
        if (layout_.timed && layout_.nsPerUnit > 0.0) {
            const double y = layout_.height + 0.3;
            pen.line({0.0, y}, {layout_.width, y}, ink(theme.border, 1.0f), 1.4f);
            const double step = std::max(1.0, std::ceil(70.0 / std::max(1.0, zoom_))) * 2.0;
            for (double x = 0.0; x + 0.35 <= layout_.width; x += step) {
                pen.line({x + 0.35, y}, {x + 0.35, y + 0.14}, ink(theme.border, 1.0f), 1.2f);
                pen.label({x + 0.35, y + 0.2}, math::formatSig(x * layout_.nsPerUnit, 3) + " ns",
                          0.8f * fontPx, GlBackend::exact(theme.textSecondary),
                          gfx::TextAnchor::TopCenter);
            }
        }

        // Regions first (they sit behind their bodies), then every other glyph in layout order.
        for (const Glyph& g : layout_.glyphs)
            if (g.kind == GlyphKind::Region)
                drawGlyph(pen, theme, layout_, g, alphaOf(g), false, fontPx);
        for (const Glyph& g : layout_.glyphs) {
            if (g.kind == GlyphKind::Region)
                continue;
            const bool current = isCurrent(g);
            // The playhead column, so the current gate is findable at any zoom. The colour is mixed
            // toward the panel rather than given a low alpha: the batch blends in scene-linear HDR,
            // where 20 % of a bright accent still reads as a saturated line.
            if (current)
                pen.line({g.bounds.cx(), 0.0}, {g.bounds.cx(), layout_.height},
                         GlBackend::exact(math::mixColor(glm::vec3(theme.bgPanel),
                                                         glm::vec3(theme.accent), 0.5f),
                                          1.0f),
                         1.8f, 8.0f);
            drawGlyph(pen, theme, layout_, g, alphaOf(g), current, fontPx);
        }
    };
}

} // namespace qlab::viz
