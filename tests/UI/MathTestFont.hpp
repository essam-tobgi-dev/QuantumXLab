#pragma once
// Headless font metrics + recording canvas for the math-layout tests (spec 20 §6).
#include "UI/Math/MathLayout.hpp"
#include "UI/Math/MathTokenizer.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace qlab::ui::test {

// Fixed-pitch metrics: every glyph is 0.6 em wide, ascent 0.72 em, descent 0.22 em.
// Wide glyphs (big operators, stretchy delimiters) get 1.0 em so scaling is observable.
class MonospaceTestFont : public math::MathFont {
  public:
    math::GlyphMetrics metrics(std::string_view g, double sizePx, math::GlyphStyle) const override {
        double w = 0.6;
        // Big operators and large delimiters are wider and taller.
        static const char* kWide[] = {"∑", "∏", "∫", "∮", "⎧", "⎨", "⎩", "√"};
        for (const char* s : kWide)
            if (g == s) {
                w = 1.0;
                break;
            }
        // Count codepoints so multi-character runs (\text) measure sensibly.
        std::size_t n = 0;
        for (std::size_t i = 0; i < g.size();) {
            i += math::utf8SeqLen(static_cast<unsigned char>(g[i]));
            ++n;
        }
        if (n == 0)
            n = 1;
        return {w * sizePx * static_cast<double>(n), 0.72 * sizePx, 0.22 * sizePx};
    }
    double xHeight(double sizePx) const override { return 0.45 * sizePx; }
    double axisHeight(double sizePx) const override { return 0.25 * sizePx; }
    double ruleThickness(double sizePx) const override { return std::max(1.0, 0.05 * sizePx); }
};

struct DrawnGlyph {
    std::string text;
    double x = 0, baselineY = 0, size = 0;
    math::GlyphStyle style = math::GlyphStyle::Italic;
};
struct DrawnLine {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0, thickness = 0;
};
struct DrawnRect {
    double x = 0, y = 0, w = 0, h = 0;
    bool filled = false;
};

class RecordingCanvas : public math::MathCanvas {
  public:
    void drawGlyph(std::string_view utf8, double x, double baselineY, double sizePx,
                   math::GlyphStyle style) override {
        glyphs.push_back({std::string(utf8), x, baselineY, sizePx, style});
    }
    void drawLine(double x0, double y0, double x1, double y1, double t) override {
        lines.push_back({x0, y0, x1, y1, t});
    }
    void drawRect(double x, double y, double w, double h, bool filled) override {
        rects.push_back({x, y, w, h, filled});
    }
    void clear() {
        glyphs.clear();
        lines.clear();
        rects.clear();
    }

    // Find the first drawn glyph whose text equals `t`.
    const DrawnGlyph* find(std::string_view t) const {
        for (const auto& g : glyphs)
            if (g.text == t)
                return &g;
        return nullptr;
    }
    int count(std::string_view t) const {
        int n = 0;
        for (const auto& g : glyphs)
            if (g.text == t)
                ++n;
        return n;
    }
    std::string plain() const {
        std::vector<const DrawnGlyph*> sorted;
        for (const auto& g : glyphs)
            sorted.push_back(&g);
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const DrawnGlyph* a, const DrawnGlyph* b) { return a->x < b->x; });
        std::string s;
        for (auto* g : sorted)
            s += g->text;
        return s;
    }

    std::vector<DrawnGlyph> glyphs;
    std::vector<DrawnLine> lines;
    std::vector<DrawnRect> rects;
};

} // namespace qlab::ui::test
