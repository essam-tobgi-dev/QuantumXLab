#pragma once
// Spec 20 §6 — expression tree of the fallback LaTeX layout engine.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace qlab::ui::math {

// TeX atom classes used for inter-atom spacing.
enum class AtomClass : std::uint8_t { Ord, Op, Bin, Rel, Open, Close, Punct, Inner };

enum class NodeKind : std::uint8_t {
    Row,     // horizontal list of children
    Symbol,  // single glyph (codepoint) or short text run; `text` holds UTF-8
    Text,    // upright text run (\text, \mathrm, \operatorname)
    Frac,    // children[0] numerator, children[1] denominator
    Scripts, // children[0] base, children[1] sub (may be null), children[2] sup (may be null)
    Sqrt,    // children[0] radicand, children[1] index (may be null)
    Accent,  // children[0] base; `text` = accent glyph; `flag` = wide (overline/underline)
    Style,   // children[0] row; `styleName` = mathbf/mathcal/mathbb/mathrm
    BigOp,   // `text` = operator glyph; children[0] sub, [1] sup (may be null); `flag`=limits
    Delim,   // `text` = left delimiter, `text2` = right delimiter; children[0] content row
    Matrix,  // rows × cols: children in row-major order; `rows`, `cols`; `text` = env name
    Space,   // `em` = width in em
    Unknown, // unknown command rendered verbatim in a distinguishable style
};

struct MathNode;
using NodePtr = std::unique_ptr<MathNode>;

struct SourceRange {
    std::uint32_t begin = 0; // byte offsets into the LaTeX source
    std::uint32_t end = 0;
};

struct MathNode {
    NodeKind kind = NodeKind::Row;
    AtomClass cls = AtomClass::Ord;
    std::string text;      // glyph(s) / operator name / delimiter
    std::string text2;     // right delimiter for Delim
    std::string styleName; // for Style
    double em = 0.0;       // Space width
    bool flag = false;     // BigOp: limits mode; Accent: wide; Frac: \tfrac (text style forced)
    int rows = 0, cols = 0;
    std::vector<NodePtr> children;
    SourceRange src;

    static NodePtr make(NodeKind k) {
        auto n = std::make_unique<MathNode>();
        n->kind = k;
        return n;
    }
    static NodePtr symbol(std::string glyph, AtomClass c, SourceRange s) {
        auto n = make(NodeKind::Symbol);
        n->text = std::move(glyph);
        n->cls = c;
        n->src = s;
        return n;
    }
};

// Style levels (TeX D, T, S, SS). Sizes scale 1.0 / 1.0 / 0.7 / 0.5.
enum class MathStyleLevel : std::uint8_t { Display, Text, Script, ScriptScript };
constexpr double styleScale(MathStyleLevel s) {
    switch (s) {
    case MathStyleLevel::Display:
    case MathStyleLevel::Text:
        return 1.0;
    case MathStyleLevel::Script:
        return 0.7;
    case MathStyleLevel::ScriptScript:
        return 0.5;
    }
    return 1.0;
}
constexpr MathStyleLevel scriptStyle(MathStyleLevel s) {
    return s == MathStyleLevel::ScriptScript ? MathStyleLevel::ScriptScript
           : s == MathStyleLevel::Script     ? MathStyleLevel::ScriptScript
                                             : MathStyleLevel::Script;
}
constexpr MathStyleLevel crampedFracStyle(MathStyleLevel s) {
    return s == MathStyleLevel::Display ? MathStyleLevel::Text : scriptStyle(s);
}

} // namespace qlab::ui::math
