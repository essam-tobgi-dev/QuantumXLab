#include "Lang/Parser.hpp"
#include "Lang/ParserUtil.hpp"
#include "Lang/StdGates.hpp"
#include <format>

namespace qlab::lang {

namespace {
bool isModifierWord(const Token& t) {
    return (t.kind == TokenKind::Keyword || t.kind == TokenKind::Builtin) &&
           (t.text == "ctrl" || t.text == "negctrl" || t.text == "inv" || t.text == "pow");
}
bool isNameToken(const Token& t) {
    return t.kind == TokenKind::Identifier || t.kind == TokenKind::Gate ||
           t.kind == TokenKind::CalBlock ||
           (t.kind == TokenKind::Builtin && !detail::isBuiltinConstant(t.text) &&
            t.text != "durationof");
}
} // namespace

std::vector<GateModifier> Parser::parseModifiers() {
    std::vector<GateModifier> mods;
    while (isModifierWord(peek())) {
        // A modifier must be followed by '@' (possibly after its parenthesised argument).
        std::size_t k = 1;
        if (detail::isPunct(peek(k), '(')) {
            int d = 0;
            for (;; ++k) {
                if (peek(k).kind == TokenKind::Eof)
                    break;
                if (detail::isPunct(peek(k), '('))
                    ++d;
                if (detail::isPunct(peek(k), ')') && --d == 0) {
                    ++k;
                    break;
                }
            }
        }
        if (!detail::isPunct(peek(k), '@'))
            break;
        const std::string t = advance().text;
        GateModifier m;
        m.kind = t == "ctrl"      ? ModifierKind::Ctrl
                 : t == "negctrl" ? ModifierKind::NegCtrl
                 : t == "inv"     ? ModifierKind::Inv
                                  : ModifierKind::Pow;
        if (matchPunct('(')) {
            m.arg = parseExprOrError();
            expectPunct(')');
        } else if (m.kind == ModifierKind::Pow)
            expected("'(' after pow");
        expectPunct('@');
        mods.push_back(std::move(m));
    }
    return mods;
}

ExprPtr Parser::parseOperand() {
    SourceSpan sp = here();
    if (check(TokenKind::PhysicalQubit)) {
        const Token& t = advance();
        return mkE(PhysicalQubitRef{static_cast<std::uint32_t>(t.ival)}, t.span);
    }
    auto id = expectIdent("qubit operand");
    if (!id)
        return nullptr;
    ExprPtr base = mkE(Ident{*id}, spanFrom(sp));
    if (checkPunct('['))
        return parseIndexOrRange(std::move(base));
    return base;
}

std::vector<ExprPtr> Parser::parseOperandList() {
    std::vector<ExprPtr> ops;
    do {
        ExprPtr o = parseOperand();
        if (!o)
            break;
        ops.push_back(std::move(o));
    } while (matchPunct(','));
    return ops;
}

StmtPtr Parser::parseGateCall(std::vector<GateModifier> mods, SourceSpan start) {
    GateCall c;
    c.modifiers = std::move(mods);
    auto name = expectIdent("gate name");
    if (!name) {
        synchronize();
        return nullptr;
    }
    c.name = *name;
    if (matchPunct('(')) {
        if (!checkPunct(')'))
            c.params = parseExprList(')');
        expectPunct(')');
    }
    // The operand list is mandatory except for gphase, which acts on no qubit (spec 13 §2).
    if (c.name != "gphase" || !checkPunct(';'))
        c.qubits = parseOperandList();
    expectPunct(';');
    return mk(std::move(c), spanFrom(start));
}

StmtPtr Parser::parseGateCallOrAssign() {
    SourceSpan sp = here();
    if (isModifierWord(peek())) {
        auto mods = parseModifiers();
        if (!mods.empty())
            return parseGateCall(std::move(mods), sp);
    }
    // OpenQASM does not separate gate calls from expression statements syntactically, so decide by
    // lookahead past the name and its optional parameter list: a following operand makes a gate
    // call, and so does a stdgates/built-in name not followed by an operator or index (`x;` and
    // `rx(0.1);` are gate calls missing operands). A subroutine call `f(q[0]);` stays an
    // expression.
    if (isNameToken(peek())) {
        std::size_t k = 1;
        if (detail::isPunct(peek(k), '(')) {
            int d = 0;
            for (;; ++k) {
                if (peek(k).kind == TokenKind::Eof)
                    break;
                if (detail::isPunct(peek(k), '('))
                    ++d;
                if (detail::isPunct(peek(k), ')') && --d == 0) {
                    ++k;
                    break;
                }
            }
        }
        const Token& n = peek(k);
        const bool operandFollows = n.kind == TokenKind::PhysicalQubit || isNameToken(n);
        const bool continuesExpr =
            n.kind == TokenKind::Operator || detail::isPunct(n, '[') || detail::isPunct(n, '.');
        if (operandFollows || (StdGates::find(peek().text) && !continuesExpr))
            return parseGateCall({}, sp);
    }
    if (inGateBody_) {
        error("QL2012", sp, peek().text);
        advance();
        synchronize();
        return nullptr;
    }
    // Assignment or expression statement.
    ExprPtr lhs = parseExprOrError();
    static const std::pair<const char*, AssignOp> aops[] = {
        {"=", AssignOp::Set},     {"+=", AssignOp::Add},    {"-=", AssignOp::Sub},
        {"*=", AssignOp::Mul},    {"/=", AssignOp::Div},    {"%=", AssignOp::Mod},
        {"**=", AssignOp::Pow},   {"&=", AssignOp::BitAnd}, {"|=", AssignOp::BitOr},
        {"^=", AssignOp::BitXor}, {"<<=", AssignOp::Shl},   {">>=", AssignOp::Shr}};
    if (check(TokenKind::Operator)) {
        for (auto& [s, op] : aops) {
            if (peek().text != s)
                continue;
            advance();
            AssignStmt a;
            a.target = std::move(lhs);
            a.op = op;
            if (checkKw("measure")) { // c = measure q;
                SourceSpan ms = advance().span;
                MeasureExpr m;
                m.qubits = parseOperandList();
                a.value = mkE(std::move(m), spanFrom(ms));
            } else
                a.value = parseExprOrError();
            expectPunct(';');
            return mk(std::move(a), spanFrom(sp));
        }
    }
    expectPunct(';');
    return mk(ExprStmt{std::move(lhs)}, spanFrom(sp));
}

StmtPtr Parser::parseMeasure() {
    SourceSpan sp = advance().span;
    MeasureStmt m;
    m.qubits = parseOperandList();
    if (matchOp("->")) {
        SourceSpan at = here();
        ExprPtr target = parseOperand();
        m.target = target ? std::move(target) : mkE(ErrorExpr{}, at); // never an engaged null
    }
    expectPunct(';');
    if (inGateBody_)
        error("QL2012", sp, "measure");
    return mk(std::move(m), spanFrom(sp));
}
StmtPtr Parser::parseReset() {
    SourceSpan sp = advance().span;
    ResetStmt r;
    r.qubits = parseOperandList();
    expectPunct(';');
    if (inGateBody_)
        error("QL2010", sp, "gate call", "'reset'"); // spec 13 §8
    return mk(std::move(r), spanFrom(sp));
}
StmtPtr Parser::parseBarrier() {
    SourceSpan sp = advance().span;
    BarrierStmt b;
    if (!checkPunct(';'))
        b.qubits = parseOperandList();
    expectPunct(';');
    return mk(std::move(b), spanFrom(sp));
}
StmtPtr Parser::parseDelay() {
    SourceSpan sp = advance().span;
    DelayStmt d;
    // The designator is mandatory (spec 13 §3): keep `duration` non-null even when it is missing
    // or malformed so that later passes never see a null child.
    if (expectPunct('[')) {
        d.duration = parseExprOrError();
        expectPunct(']');
    } else
        d.duration = mkE(ErrorExpr{}, sp);
    if (!checkPunct(';'))
        d.qubits = parseOperandList();
    expectPunct(';');
    if (inGateBody_)
        error("QL2012", sp, "delay");
    return mk(std::move(d), spanFrom(sp));
}
StmtPtr Parser::parseBox() {
    SourceSpan sp = advance().span;
    BoxStmt b;
    b.duration = parseDesignator();
    b.body = parseBlock();
    return mk(std::move(b), spanFrom(sp));
}
StmtPtr Parser::parseIf() {
    SourceSpan sp = advance().span;
    IfStmt s;
    expectPunct('(');
    s.cond = parseExprOrError();
    expectPunct(')');
    s.thenBody = parseBlockOrStmt();
    if (matchText("else")) {
        s.hasElse = true;
        s.elseBody = parseBlockOrStmt();
    }
    return mk(std::move(s), spanFrom(sp));
}
StmtPtr Parser::parseFor() {
    SourceSpan sp = advance().span;
    ForStmt f;
    if (!parseTypeSpec(f.type)) {
        synchronize();
        return nullptr;
    }
    auto v = expectIdent("loop variable");
    f.var = v.value_or("?");
    expectKw("in");
    if (checkPunct('{'))
        f.iterable = parseSetLiteral();
    else if (checkPunct('[')) {
        SourceSpan rs = advance().span;
        RangeExpr r;
        r.start = parseExprOrError();
        expectPunct(':');
        ExprPtr b = parseExprOrError();
        if (matchPunct(':')) {
            r.step = std::move(b);
            r.stop = parseExprOrError();
        } else
            r.stop = std::move(b);
        expectPunct(']');
        f.iterable = mkE(std::move(r), spanFrom(rs));
    } else
        f.iterable = parseExprOrError();
    f.body = parseBlockOrStmt();
    return mk(std::move(f), spanFrom(sp));
}
StmtPtr Parser::parseWhile() {
    SourceSpan sp = advance().span;
    WhileStmt w;
    expectPunct('(');
    w.cond = parseExprOrError();
    expectPunct(')');
    w.body = parseBlockOrStmt();
    return mk(std::move(w), spanFrom(sp));
}
StmtPtr Parser::parseSwitch() {
    SourceSpan sp = advance().span;
    SwitchStmt s;
    expectPunct('(');
    s.subject = parseExprOrError();
    expectPunct(')');
    if (expectPunct('{')) {
        int d = 1;
        while (!atEnd() && d > 0) {
            if (checkPunct('{'))
                ++d;
            if (checkPunct('}'))
                --d;
            advance();
        }
    }
    return mk(std::move(s), spanFrom(sp));
}
StmtPtr Parser::parseReturn() {
    SourceSpan sp = advance().span;
    ReturnStmt r;
    if (!checkPunct(';')) {
        if (checkKw("measure")) {
            SourceSpan ms = advance().span;
            MeasureExpr m;
            m.qubits = parseOperandList();
            r.value = mkE(std::move(m), spanFrom(ms));
        } else
            r.value = parseExprOrError();
    }
    expectPunct(';');
    return mk(std::move(r), spanFrom(sp));
}

} // namespace qlab::lang
