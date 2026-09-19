#include "UI/Math/MathParser.hpp"
#include "UI/Math/MathSymbols.hpp"

namespace qlab::ui::math {

NodePtr MathParser::parseCommand(const MathToken& t) {
    const std::string& c = t.text;
    if (c == "frac" || c == "tfrac" || c == "dfrac" || c == "binom") {
        auto n = MathNode::make(NodeKind::Frac);
        n->src = t.src;
        n->flag = (c == "tfrac");
        n->text = c;
        n->children.push_back(parseArgument());
        n->children.push_back(parseArgument());
        n->src.end = peek().src.begin;
        return n;
    }
    if (c == "sqrt") {
        auto n = MathNode::make(NodeKind::Sqrt);
        n->src = t.src;
        bool present = false;
        NodePtr idx = parseOptionalArg(present);
        n->children.push_back(parseArgument());
        n->children.push_back(present ? std::move(idx) : nullptr);
        n->src.end = peek().src.begin;
        return n;
    }
    if (c == "left")
        return parseLeftRight(t);
    if (c == "begin") {
        return parseEnvironment(t);
    }
    if (c == "end") {
        warn("stray \\end");
        rawGroupText();
        return nullptr;
    }
    if (c == "tag" || c == "label" || c == "nonumber" || c == "notag" || c == "displaystyle" ||
        c == "textstyle" || c == "scriptstyle" || c == "limits" || c == "nolimits" ||
        c == "phantom" || c == "vphantom" || c == "hphantom" || c == "smash" || c == "mathstrut" ||
        c == "allowbreak") {
        if (c == "tag" || c == "label" || c == "phantom" || c == "vphantom" || c == "hphantom" ||
            c == "smash") {
            if (at(TokKind::LBrace)) {
                rawGroupText();
            } else if (!at(TokKind::End))
                next();
        }
        return nullptr;
    }
    if (c == "ket" || c == "bra" || c == "braket" || c == "ketbra") {
        auto n = MathNode::make(NodeKind::Delim);
        n->src = t.src;
        auto content = MathNode::make(NodeKind::Row);
        if (c == "ket") {
            n->text = "|";
            n->text2 = "⟩";
            content->children.push_back(parseArgument());
        } else if (c == "bra") {
            n->text = "⟨";
            n->text2 = "|";
            content->children.push_back(parseArgument());
        } else if (c == "braket") {
            n->text = "⟨";
            n->text2 = "⟩";
            content->children.push_back(parseArgument());
            content->children.push_back(MathNode::symbol("|", AtomClass::Ord, {}));
            content->children.push_back(parseArgument());
        } else {
            n->text = "|";
            n->text2 = "|";
            content->children.push_back(parseArgument());
            content->children.push_back(MathNode::symbol("⟩⟨", AtomClass::Ord, {}));
            content->children.push_back(parseArgument());
        }
        n->children.push_back(std::move(content));
        n->src.end = peek().src.begin;
        return n;
    }
    if (isStyleCommand(c)) {
        if (c == "text" || c == "textrm" || c == "operatorname" || c == "mathrm" || c == "textbf" ||
            c == "textit" || c == "mathsf" || c == "mathtt") {
            auto n = MathNode::make(NodeKind::Text);
            n->src = t.src;
            n->text = rawGroupText();
            n->styleName = c;
            n->cls = (c == "operatorname") ? AtomClass::Op : AtomClass::Ord;
            n->src.end = peek().src.begin;
            return n;
        }
        auto n = MathNode::make(NodeKind::Style);
        n->src = t.src;
        n->styleName = c;
        n->children.push_back(parseArgument());
        n->src.end = peek().src.begin;
        return n;
    }
    if (auto acc = lookupAccent(c)) {
        auto n = MathNode::make(NodeKind::Accent);
        n->src = t.src;
        n->text = std::string(acc->glyph);
        n->flag = acc->wide;
        n->em = acc->below ? 1.0 : 0.0;
        n->children.push_back(parseArgument());
        n->src.end = peek().src.begin;
        return n;
    }
    if (auto big = lookupBigOp(c)) {
        auto n = MathNode::make(NodeKind::BigOp);
        n->src = t.src;
        n->text = std::string(big->glyph);
        n->flag = !big->integral; // limits above/below in display style unless integral
        if (atCommand("limits")) {
            next();
            n->flag = true;
        } else if (atCommand("nolimits")) {
            next();
            n->flag = false;
        }
        n->children.resize(2);
        return n;
    }
    if (isFunctionOperator(c)) {
        auto n = MathNode::make(NodeKind::Text);
        n->src = t.src;
        n->text = c;
        n->styleName = "operatorname";
        n->cls = AtomClass::Op;
        // \lim, \max, \min take limits below in display style
        if (c == "lim" || c == "max" || c == "min" || c == "sup" || c == "inf" || c == "argmax" ||
            c == "argmin") {
            auto big = MathNode::make(NodeKind::BigOp);
            big->src = t.src;
            big->text = c;
            big->flag = true;
            big->styleName = "text"; // rendered as upright text, not a large glyph
            big->children.resize(2);
            return big;
        }
        return n;
    }
    if (auto sp = lookupSpace(c)) {
        auto n = MathNode::make(NodeKind::Space);
        n->em = *sp;
        n->src = t.src;
        if (c == "hspace" || c == "mkern") {
            if (at(TokKind::LBrace))
                rawGroupText();
        }
        return n;
    }
    if (auto sym = lookupSymbol(c)) {
        return MathNode::symbol(std::string(sym->glyph), sym->cls, t.src);
    }
    if (c == "not") { // \not= → ≠ approximation: mark next relation
        NodePtr n = parseAtom();
        if (n && n->kind == NodeKind::Symbol)
            n->text = "≠";
        return n;
    }
    // Old-style font switches: \rm, \bf, \it, \sf, \tt, \cal apply to the rest of the
    // enclosing group ({\rm anc} is the common form in the theory corpus).
    if (c == "rm" || c == "bf" || c == "it" || c == "sf" || c == "tt" || c == "cal" || c == "bb") {
        auto n = MathNode::make(NodeKind::Style);
        n->src = t.src;
        n->styleName = c == "rm"    ? "mathrm"
                       : c == "bf"  ? "mathbf"
                       : c == "it"  ? "mathit"
                       : c == "sf"  ? "mathsf"
                       : c == "tt"  ? "mathtt"
                       : c == "cal" ? "mathcal"
                                    : "mathbb";
        n->children.push_back(parseRow(true));
        n->src.end = peek().src.begin;
        return n;
    }
    // Manual delimiter sizes: \big( \Big) \bigl[ \Bigr] \bigg \Bigg …
    if (c.size() >= 3 && (c.compare(0, 3, "big") == 0 || c.compare(0, 3, "Big") == 0)) {
        std::string rest = c.substr(3);
        if (rest.empty() || rest == "l" || rest == "r" || rest == "m" || rest == "g" ||
            rest == "gl" || rest == "gr" || rest == "gm") {
            double scale = 1.2;
            if (c[0] == 'B')
                scale = 1.8;
            if (rest.starts_with("g"))
                scale *= 1.45; // \bigg / \Bigg
            std::string d = parseDelimiterToken();
            if (d.empty()) {
                warn("\\" + c + " without a delimiter");
                return nullptr;
            }
            auto n = MathNode::symbol(d,
                                      rest == "l" || rest == "gl"   ? AtomClass::Open
                                      : rest == "r" || rest == "gr" ? AtomClass::Close
                                                                    : AtomClass::Ord,
                                      t.src);
            n->em = scale; // layoutSymbol scales the glyph by this factor
            n->src.end = peek().src.begin;
            return n;
        }
    }
    // Modular arithmetic: \bmod is a binary operator, \pmod{n} renders as "(mod n)".
    if (c == "bmod") {
        auto n = MathNode::make(NodeKind::Text);
        n->src = t.src;
        n->text = "mod";
        n->styleName = "mathrm";
        n->cls = AtomClass::Bin;
        return n;
    }
    if (c == "pmod") {
        auto arg = parseArgument();
        auto row = MathNode::make(NodeKind::Row);
        row->src = t.src;
        row->cls = AtomClass::Ord;
        auto sp = MathNode::make(NodeKind::Space);
        sp->em = 0.44;
        row->children.push_back(std::move(sp));
        auto inner = MathNode::make(NodeKind::Delim);
        inner->text = "(";
        inner->text2 = ")";
        auto content = MathNode::make(NodeKind::Row);
        auto mod = MathNode::make(NodeKind::Text);
        mod->text = "mod";
        mod->styleName = "mathrm";
        mod->cls = AtomClass::Ord;
        content->children.push_back(std::move(mod));
        auto sp2 = MathNode::make(NodeKind::Space);
        sp2->em = 0.22;
        content->children.push_back(std::move(sp2));
        content->children.push_back(std::move(arg));
        inner->children.push_back(std::move(content));
        row->children.push_back(std::move(inner));
        row->src.end = peek().src.begin;
        return row;
    }
    // Extensible arrows with a label above (\xrightarrow{…}, \xleftarrow{…}).
    if (c == "xrightarrow" || c == "xleftarrow" || c == "xrightleftharpoons") {
        bool present = false;
        parseOptionalArg(present); // below-label form is accepted and ignored
        auto label = parseArgument();
        auto scripts = MathNode::make(NodeKind::Scripts);
        scripts->src = t.src;
        auto arrow = MathNode::symbol(c == "xleftarrow"    ? "⟵"
                                      : c == "xrightarrow" ? "⟶"
                                                           : "⇌",
                                      AtomClass::Rel, t.src);
        scripts->children.push_back(std::move(arrow));
        scripts->children.push_back(nullptr);
        scripts->children.push_back(std::move(label));
        scripts->cls = AtomClass::Rel;
        scripts->src.end = peek().src.begin;
        return scripts;
    }
    if (c == "over") {
        warn("\\over is not supported; use \\frac");
        return nullptr;
    }
    if (c == "mathop") {
        auto n = parseArgument();
        n->cls = AtomClass::Op;
        return n;
    }
    if (c == "mathbin") {
        auto n = parseArgument();
        n->cls = AtomClass::Bin;
        return n;
    }
    if (c == "mathrel") {
        auto n = parseArgument();
        n->cls = AtomClass::Rel;
        return n;
    }
    if (c == "color" || c == "textcolor") {
        if (at(TokKind::LBrace))
            rawGroupText();
        return c == "textcolor" ? parseArgument() : nullptr;
    }
    // Unknown: render verbatim
    warn("unknown command \\" + c);
    auto n = MathNode::make(NodeKind::Unknown);
    n->text = "\\" + c;
    n->src = t.src;
    return n;
}

NodePtr MathParser::parseLeftRight(const MathToken& leftTok) {
    auto n = MathNode::make(NodeKind::Delim);
    n->src = leftTok.src;
    n->text = parseDelimiterToken();
    n->children.push_back(parseRow(false));
    if (atCommand("right")) {
        next();
        n->text2 = parseDelimiterToken();
    } else {
        warn("\\left without \\right");
        n->text2 = "";
    }
    n->src.end = peek().src.begin;
    return n;
}

NodePtr MathParser::parseEnvironment(const MathToken& beginTok) {
    std::string env = rawGroupText();
    auto n = MathNode::make(NodeKind::Matrix);
    n->src = beginTok.src;
    n->text = env;
    bool known = env == "pmatrix" || env == "bmatrix" || env == "vmatrix" || env == "Vmatrix" ||
                 env == "Bmatrix" || env == "matrix" || env == "cases" || env == "aligned" ||
                 env == "align" || env == "align*" || env == "array" || env == "gathered" ||
                 env == "gather" || env == "smallmatrix" || env == "split" || env == "equation" ||
                 env == "equation*" || env == "alignat";
    if (!known)
        warn("unknown environment '" + env + "'");
    if (env == "array" || env == "alignat") {
        if (at(TokKind::LBrace))
            rawGroupText();
    } // column spec ignored
    std::vector<std::vector<NodePtr>> grid(1);
    for (;;) {
        grid.back().push_back(parseRow(false, true, env));
        if (at(TokKind::Amp)) {
            next();
            continue;
        }
        if (at(TokKind::NewRow)) {
            next();
            bool present = false;
            (void)parseOptionalArg(present); // optional [dim] after \\ ignored
            if (atCommand("end") || at(TokKind::End))
                break; // trailing \\ before \end
            grid.emplace_back();
            continue;
        }
        break;
    }
    if (atCommand("end")) {
        next();
        std::string e = rawGroupText();
        if (e != env)
            warn("\\end{" + e + "} does not match \\begin{" + env + "}");
    } else
        warn("missing \\end{" + env + "}");
    std::size_t cols = 1;
    for (auto& r : grid)
        cols = std::max(cols, r.size());
    for (auto& r : grid) {
        while (r.size() < cols)
            r.push_back(MathNode::make(NodeKind::Row));
        for (auto& c : r)
            n->children.push_back(std::move(c));
    }
    n->rows = static_cast<int>(grid.size());
    n->cols = static_cast<int>(cols);
    n->src.end = peek().src.begin;
    return n;
}

} // namespace qlab::ui::math
