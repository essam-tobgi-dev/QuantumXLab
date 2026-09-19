#include "Lang/Parser.hpp"
#include "Lang/ParserUtil.hpp"

// Spec 13 §2 — statement dispatch and statement-level error recovery (spec 13 §8).
namespace qlab::lang {

namespace {
// A leaf statement left incomplete by a syntax error is dropped, so Sema never reports errors that
// only follow from the malformed text. Declarations, definitions and compound statements are kept
// (with ErrorExpr placeholders) so their names still resolve and their bodies are still analysed.
bool dropOnError(const Stmt& s) {
    return !(s.is<QubitDecl>() || s.is<ClassicalDecl>() || s.is<GateDecl>() || s.is<DefStmt>() ||
             s.is<IfStmt>() || s.is<ForStmt>() || s.is<WhileStmt>() || s.is<SwitchStmt>() ||
             s.is<BoxStmt>() || s.is<CalStmt>() || s.is<DefcalStmt>());
}
} // namespace

StmtPtr Parser::parseStatement() {
    panic_ = false;
    if (nesting_ >= detail::kMaxNesting) {
        if (detail::lastDiagnosticIs(diags_, "QL2015"))
            panic_ = true;
        else
            error("QL2015", here(), detail::kMaxNesting);
        if (!checkPunct('{'))
            advance();
        synchronize();
        return nullptr;
    }
    detail::NestingGuard guard(nesting_);
    const std::size_t start = pos_;
    // Reports QL2011 for a token that cannot begin a statement and skips the statement.
    auto unexpected = [&](const std::string& what) -> StmtPtr {
        error("QL2011", here(), what);
        if (!checkPunct('{'))
            advance(); // a '{' is left for synchronize() to balance
        synchronize();
        return nullptr;
    };
    auto dispatch = [&]() -> StmtPtr {
        const Token& t = peek();
        if (t.kind == TokenKind::Punct && t.text == ";") {
            advance();
            return nullptr;
        }
        if (t.kind == TokenKind::Pragma)
            return parsePragma();
        if (t.kind == TokenKind::Invalid) { // already reported by the lexer
            panic_ = true;
            advance();
            synchronize();
            return nullptr;
        }
        if (t.kind == TokenKind::Keyword) {
            const std::string k = t.text;
            if (k == "OPENQASM") {
                const bool duplicate = sawVersion_;
                SourceSpan at = here();
                StmtPtr s = parseVersion();
                if (duplicate)
                    error("QL2001", at, " (duplicate)");
                return s;
            }
            if (k == "include")
                return parseInclude();
            if (k == "qubit")
                return parseQubitDecl();
            if (k == "const") {
                advance();
                return parseClassicalDecl(IoKind::Const);
            }
            if (k == "input") {
                advance();
                return parseClassicalDecl(IoKind::Input);
            }
            if (k == "output") {
                advance();
                return parseClassicalDecl(IoKind::Output);
            }
            if (k == "gate")
                return parseGateDef();
            if (k == "def")
                return parseDef();
            if (k == "extern")
                return parseExtern();
            if (k == "measure")
                return parseMeasure();
            if (k == "reset")
                return parseReset();
            if (k == "barrier")
                return parseBarrier();
            if (k == "delay")
                return parseDelay();
            if (k == "box")
                return parseBox();
            if (k == "if")
                return parseIf();
            if (k == "for")
                return parseFor();
            if (k == "while")
                return parseWhile();
            if (k == "switch")
                return parseSwitch();
            if (k == "return")
                return parseReturn();
            if (k == "cal")
                return parseCal();
            if (k == "defcal")
                return parseDefcal();
            if (k == "break") {
                SourceSpan sp = advance().span;
                expectPunct(';');
                return mk(BreakStmt{}, sp);
            }
            if (k == "continue") {
                SourceSpan sp = advance().span;
                expectPunct(';');
                return mk(ContinueStmt{}, sp);
            }
            if (k == "end") {
                SourceSpan sp = advance().span;
                expectPunct(';');
                return mk(EndStmt{}, sp);
            }
            if (k == "ctrl" || k == "negctrl")
                return parseGateCallOrAssign();
            if (k == "defcalgrammar") {
                SourceSpan sp = advance().span;
                if (check(TokenKind::String)) {
                    std::string g = advance().text;
                    if (g != "openpulse")
                        error("QL2010", sp, "\"openpulse\"", "\"" + g + "\"");
                } else {
                    expected("\"openpulse\"");
                }
                expectPunct(';');
                return nullptr;
            }
            return unexpected(k); // let, else, in, case, default, ...
        }
        if (t.kind == TokenKind::Type) {
            if (t.text == "qreg") { // OpenQASM 2 legacy: qreg q[n];
                SourceSpan sp = advance().span;
                auto name = expectIdent("register name");
                QubitDecl d;
                d.name = name.value_or("?");
                d.size = parseDesignator();
                expectPunct(';');
                return mk(std::move(d), spanFrom(sp));
            }
            if (t.text == "creg") {
                SourceSpan sp = advance().span;
                auto name = expectIdent("register name");
                ClassicalDecl d;
                d.type.base = BaseType::Bit;
                d.name = name.value_or("?");
                d.type.width = parseDesignator();
                expectPunct(';');
                return mk(std::move(d), spanFrom(sp));
            }
            if (t.text == "qubit")
                return parseQubitDecl();
            return parseClassicalDecl(IoKind::None);
        }
        const bool unary =
            t.kind == TokenKind::Operator && (t.text == "-" || t.text == "!" || t.text == "~");
        if (t.kind == TokenKind::Identifier || t.kind == TokenKind::Gate ||
            t.kind == TokenKind::PhysicalQubit || t.kind == TokenKind::CalBlock ||
            t.kind == TokenKind::Builtin || t.kind == TokenKind::Number ||
            (t.kind == TokenKind::Punct && t.text == "(") || unary)
            return parseGateCallOrAssign();
        return unexpected(t.kind == TokenKind::Eof ? std::string("end of file") : t.text);
    };

    StmtPtr s = dispatch();
    if (!panic_)
        return s;
    if (s && (inGateBody_ || dropOnError(*s)))
        s.reset();
    const Token& last = toks_[pos_ > 0 ? pos_ - 1 : 0];
    const bool terminated =
        pos_ > start && (detail::isPunct(last, ';') || detail::isPunct(last, '}'));
    if (!terminated)
        synchronize();
    return s;
}

} // namespace qlab::lang
