#include "Lang/Parser.hpp"
#include "Lang/ParserUtil.hpp"
#include <algorithm>
#include <format>
#include <optional>
#include <type_traits>

namespace qlab::lang {

namespace detail {
int exprDepth(const Expr& e, int cap) {
    int child = 0;
    auto sub = [&](const ExprPtr& c) {
        if (c && child < cap) child = std::max(child, exprDepth(*c, cap - 1));
    };
    std::visit([&](const auto& n) {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, UnaryExpr>) sub(n.operand);
        else if constexpr (std::is_same_v<T, BinaryExpr> || std::is_same_v<T, ConcatExpr>) { sub(n.lhs); sub(n.rhs); }
        else if constexpr (std::is_same_v<T, CallExpr>) { for (auto& a : n.args) sub(a); }
        else if constexpr (std::is_same_v<T, CastExpr>) { if (n.type.width) sub(*n.type.width); sub(n.operand); }
        else if constexpr (std::is_same_v<T, IndexExpr>) { sub(n.base); sub(n.index); }
        else if constexpr (std::is_same_v<T, RangeExpr>) { sub(n.start); if (n.step) sub(*n.step); sub(n.stop); }
        else if constexpr (std::is_same_v<T, SliceExpr>) { sub(n.base); sub(n.range); }
        else if constexpr (std::is_same_v<T, SetExpr>) { for (auto& i : n.items) sub(i); }
        else if constexpr (std::is_same_v<T, MeasureExpr>) { for (auto& q : n.qubits) sub(q); }
    }, e.node);
    return 1 + child;
}
} // namespace detail

namespace {
struct OpInfo { const char* text; int prec; BinaryOp op; bool rightAssoc; };
// Spec 13 §2.1: higher prec binds tighter. '**' is level 3 (right-assoc).
const OpInfo kOps[] = {
    {"||", 1, BinaryOp::Or, false}, {"&&", 2, BinaryOp::And, false}, {"|", 3, BinaryOp::BitOr, false},
    {"^", 4, BinaryOp::BitXor, false}, {"&", 5, BinaryOp::BitAnd, false},
    {"==", 6, BinaryOp::Eq, false}, {"!=", 6, BinaryOp::Ne, false},
    {"<", 7, BinaryOp::Lt, false}, {"<=", 7, BinaryOp::Le, false}, {">", 7, BinaryOp::Gt, false}, {">=", 7, BinaryOp::Ge, false},
    {"<<", 8, BinaryOp::Shl, false}, {">>", 8, BinaryOp::Shr, false},
    {"+", 9, BinaryOp::Add, false}, {"-", 9, BinaryOp::Sub, false},
    {"*", 10, BinaryOp::Mul, false}, {"/", 10, BinaryOp::Div, false}, {"%", 10, BinaryOp::Mod, false},
    {"**", 11, BinaryOp::Pow, true},
};
const OpInfo* findOp(const Token& t) {
    if (t.kind != TokenKind::Operator) return nullptr;
    for (auto& o : kOps) if (t.text == o.text) return &o;
    return nullptr;
}
} // namespace

ExprPtr Parser::parseExpr() { return parseBinary(1); }

ExprPtr Parser::parseExprOrError() {
    SourceSpan sp = peek().span;
    int before = errorCount_;
    if (ExprPtr e = parseExpr()) return e;
    // Report only if the failing sub-parse stayed silent, so one bad token yields one
    // diagnostic (spec 13 §8). The ErrorExpr keeps the AST free of null children.
    if (errorCount_ == before) expected("expression");
    return mkE(ErrorExpr{}, sp);
}

// Operator chains are built iteratively, so their depth is bounded here (QL2015): every consumer of
// the AST (Sema, dumps, destructors) recurses on it. On overflow the rest of the chain is left
// unconsumed for the enclosing statement's recovery.
ExprPtr Parser::parseBinary(int minPrec) {
    if (nesting_ >= detail::kMaxNesting) {
        if (detail::lastDiagnosticIs(diags_, "QL2015")) panic_ = true;
        else error("QL2015", here(), detail::kMaxNesting);
        return nullptr;
    }
    detail::NestingGuard guard(nesting_);
    ExprPtr lhs = parseUnary();
    if (!lhs) return nullptr;
    int depth = detail::exprDepth(*lhs, detail::kMaxDepth);
    for (;;) {
        const bool concat = checkOp("++");
        const OpInfo* op = concat ? nullptr : findOp(peek());
        if (concat ? minPrec > 1 : (!op || op->prec < minPrec)) break;
        if (nesting_ + depth >= detail::kMaxDepth) {
            if (detail::lastDiagnosticIs(diags_, "QL2015")) panic_ = true;
            else error("QL2015", here(), detail::kMaxDepth);
            break;
        }
        SourceSpan sp = lhs->span;
        SourceSpan opSpan = advance().span;
        ExprPtr rhs = concat ? parseBinary(2) : parseBinary(op->rightAssoc ? op->prec : op->prec + 1);
        if (!rhs) rhs = mkE(ErrorExpr{}, opSpan); // keep the partial tree instead of discarding it
        depth = 1 + std::max(depth, detail::exprDepth(*rhs, detail::kMaxDepth));
        lhs = concat ? mkE(ConcatExpr{std::move(lhs), std::move(rhs)}, spanFrom(sp))
                     : mkE(BinaryExpr{op->op, std::move(lhs), std::move(rhs)}, spanFrom(sp));
    }
    return lhs;
}

ExprPtr Parser::parseUnary() {
    if (nesting_ >= detail::kMaxNesting) {
        if (detail::lastDiagnosticIs(diags_, "QL2015")) panic_ = true;
        else error("QL2015", here(), detail::kMaxNesting);
        return nullptr;
    }
    detail::NestingGuard guard(nesting_);
    SourceSpan sp = here();
    std::optional<UnaryOp> op;
    if (checkOp("-")) op = UnaryOp::Neg;
    else if (checkOp("!")) op = UnaryOp::Not;
    else if (checkOp("~")) op = UnaryOp::BitNot;
    if (op) {
        advance();
        ExprPtr o = parseUnary();
        if (!o) o = mkE(ErrorExpr{}, here());
        return mkE(UnaryExpr{*op, std::move(o)}, spanFrom(sp));
    }
    ExprPtr p = parsePrimary();
    if (!p) return nullptr;
    return parsePostfix(std::move(p));
}

ExprPtr Parser::parsePostfix(ExprPtr base) {
    while (checkPunct('[')) {
        if (nesting_ + detail::exprDepth(*base, detail::kMaxDepth) >= detail::kMaxDepth) {
            if (detail::lastDiagnosticIs(diags_, "QL2015")) panic_ = true;
            else error("QL2015", here(), detail::kMaxDepth);
            break;
        }
        base = parseIndexOrRange(std::move(base));
    }
    return base;
}

ExprPtr Parser::parseIndexOrRange(ExprPtr base) {
    SourceSpan sp = base->span;
    expectPunct('[');
    ExprPtr a = parseExprOrError();
    if (matchPunct(':')) {
        ExprPtr b = parseExprOrError();
        RangeExpr r; r.start = std::move(a);
        if (matchPunct(':')) { r.step = std::move(b); r.stop = parseExprOrError(); } else r.stop = std::move(b);
        expectPunct(']');
        SourceSpan rs = spanFrom(sp);
        return mkE(SliceExpr{std::move(base), mkE(std::move(r), rs)}, rs);
    }
    expectPunct(']');
    return mkE(IndexExpr{std::move(base), std::move(a)}, spanFrom(sp));
}

std::vector<ExprPtr> Parser::parseExprList(char close) {
    std::vector<ExprPtr> list;
    while (!atEnd() && !checkPunct(close)) {
        list.push_back(parseExprOrError());
        if (!matchPunct(',')) break;
    }
    return list;
}

ExprPtr Parser::parseSetLiteral() {
    SourceSpan sp = advance().span; // '{'
    SetExpr s; s.items = parseExprList('}');
    expectPunct('}');
    return mkE(std::move(s), spanFrom(sp));
}

ExprPtr Parser::parsePrimary() {
    const Token& t = peek();
    SourceSpan sp = t.span;
    switch (t.kind) {
    case TokenKind::Invalid: // reported by the lexer: a silent placeholder (spec 13 §1)
        panic_ = true;
        advance();
        return mkE(ErrorExpr{}, sp);
    case TokenKind::Number: {
        advance();
        if (t.isImaginary) return mkE(ImagLit{t.fval}, sp);
        if (t.isFloat) return mkE(FloatLit{t.fval}, sp);
        return mkE(IntLit{t.ival}, sp);
    }
    case TokenKind::Duration: advance(); return mkE(DurationLit{t.fval, t.unit}, sp);
    case TokenKind::BitString: advance(); return mkE(BitStringLit{t.text}, sp);
    case TokenKind::String: advance(); return mkE(BitStringLit{t.text}, sp);
    case TokenKind::PhysicalQubit: advance(); return mkE(PhysicalQubitRef{static_cast<std::uint32_t>(t.ival)}, sp);
    case TokenKind::Keyword:
        if (t.text == "measure") { advance(); MeasureExpr m; m.qubits = parseOperandList(); return mkE(std::move(m), spanFrom(sp)); }
        break;
    case TokenKind::Identifier:
        if (t.text == "true" || t.text == "false") { advance(); return mkE(BoolLit{t.text == "true"}, sp); }
        [[fallthrough]];
    case TokenKind::Gate:
    case TokenKind::CalBlock:
    case TokenKind::Builtin: {
        advance();
        if (detail::isBuiltinConstant(t.text)) return mkE(ConstantRef{std::string(detail::canonicalConstant(t.text))}, sp);
        if (t.text == "durationof") {
            expectPunct('('); DurationOfExpr d; d.body = parseBlock(); expectPunct(')');
            return mkE(std::move(d), spanFrom(sp));
        }
        if (checkPunct('(')) {
            advance();
            CallExpr c; c.callee = t.text;
            c.args = parseExprList(')');
            expectPunct(')');
            return mkE(std::move(c), spanFrom(sp));
        }
        return mkE(Ident{t.text}, sp);
    }
    case TokenKind::Type: { // cast: int(x), float[64](x), bit(x)...
        TypeSpec ts;
        if (!parseTypeSpec(ts)) return nullptr;
        if (!expectPunct('(')) return nullptr;
        ExprPtr o = parseExprOrError(); expectPunct(')');
        return mkE(CastExpr{std::move(ts), std::move(o)}, spanFrom(sp));
    }
    case TokenKind::Punct:
        if (t.text == "(") { advance(); ExprPtr e = parseExprOrError(); expectPunct(')'); return e; }
        if (t.text == "{") return parseSetLiteral();
        if (t.text == "[") { // sample array literal [c1, c2, ...]
            advance(); SetExpr s; s.items = parseExprList(']'); expectPunct(']'); return mkE(std::move(s), spanFrom(sp));
        }
        break;
    default: break;
    }
    expected("expression");
    return nullptr;
}

} // namespace qlab::lang
