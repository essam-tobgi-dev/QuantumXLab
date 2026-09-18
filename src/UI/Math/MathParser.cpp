#include "UI/Math/MathParser.hpp"
#include "UI/Math/MathSymbols.hpp"
#include <cctype>

namespace qlab::ui::math {

MathParser::MathParser(std::string_view latex) : src_(latex), toks_(tokenizeMath(latex)) {}

const MathToken& MathParser::peek(std::size_t k) const {
    std::size_t i = pos_ + k;
    return i < toks_.size() ? toks_[i] : toks_.back();
}
MathToken MathParser::next() {
    MathToken t = peek();
    if (pos_ < toks_.size() - 1) ++pos_;
    return t;
}
bool MathParser::atCommand(std::string_view name) const {
    return peek().kind == TokKind::Command && peek().text == name;
}

ParseOutput MathParser::parse() {
    ParseOutput out;
    out.root = parseRow(false);
    while (!at(TokKind::End)) { // stray closers at top level
        MathToken t = next();
        warn("unexpected '" + t.text + "' at offset " + std::to_string(t.src.begin));
    }
    out.warnings = std::move(warnings_);
    out.errors = std::move(errors_);
    return out;
}

NodePtr MathParser::parseRow(bool stopAtRBrace, bool stopAtCell, std::string_view endEnv) {
    auto row = MathNode::make(NodeKind::Row);
    row->src.begin = peek().src.begin;
    if (++depth_ > 200) { errors_.push_back("nesting too deep"); --depth_; return row; }
    for (;;) {
        const MathToken& t = peek();
        if (t.kind == TokKind::End) break;
        if (t.kind == TokKind::RBrace) { if (stopAtRBrace) break; next(); warn("unbalanced '}'"); continue; }
        if (stopAtCell && (t.kind == TokKind::Amp || t.kind == TokKind::NewRow)) break;
        if (!endEnv.empty() && t.kind == TokKind::Command && t.text == "end") break;
        if (t.kind == TokKind::Command && t.text == "right") break; // handled by parseLeftRight
        if (t.kind == TokKind::Amp || t.kind == TokKind::NewRow) { next(); warn("'&' or '\\\\' outside a matrix"); continue; }
        if (t.kind == TokKind::Caret || t.kind == TokKind::Underscore) {
            // script without a base: attach to an empty atom
            auto empty = MathNode::make(NodeKind::Row);
            row->children.push_back(applyScripts(std::move(empty)));
            continue;
        }
        NodePtr atom = parseAtom();
        if (!atom) continue;
        row->children.push_back(applyScripts(std::move(atom)));
    }
    row->src.end = peek().src.begin;
    --depth_;
    return row;
}

NodePtr MathParser::applyScripts(NodePtr base) {
    NodePtr sub, sup;
    for (int guard = 0; guard < 4; ++guard) {
        if (at(TokKind::Underscore)) {
            next();
            if (sub) warn("double subscript");
            sub = parseArgument();
        } else if (at(TokKind::Caret)) {
            next();
            if (sup) warn("double superscript");
            sup = parseArgument();
        } else if (at(TokKind::Char) && peek().text == "'") {
            // prime: turn into superscript ′ (accumulate)
            auto p = MathNode::symbol("′", AtomClass::Ord, next().src);
            if (!sup) { sup = MathNode::make(NodeKind::Row); }
            if (sup->kind != NodeKind::Row) { auto r = MathNode::make(NodeKind::Row); r->children.push_back(std::move(sup)); sup = std::move(r); }
            sup->children.insert(sup->children.begin(), std::move(p));
        } else break;
    }
    if (!sub && !sup) return base;
    if (base->kind == NodeKind::BigOp) { // limits belong to the operator
        base->children.resize(2);
        base->children[0] = std::move(sub);
        base->children[1] = std::move(sup);
        return base;
    }
    auto n = MathNode::make(NodeKind::Scripts);
    n->src = base->src;
    n->children.push_back(std::move(base));
    n->children.push_back(std::move(sub));
    n->children.push_back(std::move(sup));
    return n;
}

NodePtr MathParser::parseArgument() {
    if (at(TokKind::LBrace)) {
        next();
        NodePtr r = parseRow(true);
        if (at(TokKind::RBrace)) next(); else warn("missing '}'");
        return r;
    }
    if (at(TokKind::End)) { warn("missing argument"); return MathNode::make(NodeKind::Row); }
    NodePtr a = parseAtom();
    return a ? std::move(a) : MathNode::make(NodeKind::Row);
}

NodePtr MathParser::parseOptionalArg(bool& present) {
    present = false;
    if (!(at(TokKind::Char) && peek().text == "[")) return nullptr;
    next();
    present = true;
    auto row = MathNode::make(NodeKind::Row);
    while (!at(TokKind::End) && !(at(TokKind::Char) && peek().text == "]")) {
        NodePtr a = parseAtom();
        if (a) row->children.push_back(applyScripts(std::move(a)));
    }
    if (at(TokKind::Char)) next();
    return row;
}

std::string MathParser::rawGroupText() {
    if (!at(TokKind::LBrace)) { MathToken t = next(); return t.text; }
    next();
    std::string s;
    int level = 1;
    std::uint32_t lastEnd = peek().src.begin;
    while (!at(TokKind::End)) {
        MathToken t = next();
        if (t.kind == TokKind::LBrace) ++level;
        if (t.kind == TokKind::RBrace && --level == 0) break;
        if (t.src.begin > lastEnd) s += ' ';
        if (t.kind == TokKind::Command) {
            if (auto sym = lookupSymbol(t.text)) s += std::string(sym->glyph);
            else if (auto sp = lookupSpace(t.text)) s += ' ';
            else s += "\\" + t.text;
        } else s += t.text;
        lastEnd = t.src.end;
    }
    return s;
}

std::string MathParser::parseDelimiterToken() {
    MathToken t = next();
    std::string key = t.kind == TokKind::Command ? "\\" + t.text : t.text;
    if (auto d = lookupDelimiter(key)) return std::string(*d);
    warn("unknown delimiter '" + key + "'");
    return t.text;
}

NodePtr MathParser::parseAtom() {
    MathToken t = next();
    switch (t.kind) {
    case TokKind::End: return nullptr;
    case TokKind::LBrace: {
        NodePtr r = parseRow(true);
        if (at(TokKind::RBrace)) next(); else warn("missing '}'");
        r->src = {t.src.begin, peek().src.begin};
        return r;
    }
    case TokKind::Command: return parseCommand(t);
    case TokKind::Char: {
        std::size_t i = 0;
        char32_t cp = utf8Decode(t.text, i);
        if (cp == '|') return MathNode::symbol("|", AtomClass::Ord, t.src);
        return MathNode::symbol(t.text, classifyChar(cp), t.src);
    }
    default:
        warn("unexpected token '" + t.text + "'");
        return nullptr;
    }
}

} // namespace qlab::ui::math
