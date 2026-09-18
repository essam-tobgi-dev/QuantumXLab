#include "Lang/Parser.hpp"
#include "Lang/ParserUtil.hpp"
#include <format>

// Spec 13 §2, §8 — token stream access, expectations and error recovery. Recovery contract: a
// lexical or syntax error yields one diagnostic per statement (panic mode, Parser.hpp), failed
// expectations never consume tokens, and progress is guaranteed by the statement loops and
// synchronize().
namespace qlab::lang {

ParseResult parse(std::string_view text, std::string filename) {
    LexOptions o; o.filename = filename;
    LexResult lr = lex(text, o);
    ParseResult out;
    out.diagnostics = lr.diagnostics;
    Parser p(std::move(lr.tokens), filename);
    out.ast = p.parseProgram();
    auto pd = p.takeDiagnostics();
    out.diagnostics.insert(out.diagnostics.end(), std::make_move_iterator(pd.begin()), std::make_move_iterator(pd.end()));
    return out;
}

Parser::Parser(std::vector<Token> tokens, std::string filename)
    : toks_(std::move(tokens)), filename_(std::move(filename)) {
    if (toks_.empty() || toks_.back().kind != TokenKind::Eof) { Token e; e.kind = TokenKind::Eof; toks_.push_back(e); }
}

const Token& Parser::peek(std::size_t k) const {
    std::size_t i = pos_ + k;
    return i < toks_.size() ? toks_[i] : toks_.back();
}
const Token& Parser::advance() {
    const Token& t = toks_[pos_];
    if (pos_ + 1 < toks_.size()) ++pos_;
    return t;
}
bool Parser::checkText(std::string_view t) const { return peek().kind != TokenKind::Eof && peek().isText(t); }
bool Parser::match(TokenKind k) { if (check(k)) { advance(); return true; } return false; }
bool Parser::matchText(std::string_view t) { if (checkText(t)) { advance(); return true; } return false; }
bool Parser::matchPunct(char c) { if (checkPunct(c)) { advance(); return true; } return false; }
bool Parser::matchOp(std::string_view t) { if (checkOp(t)) { advance(); return true; } return false; }

void Parser::expected(std::string_view what) {
    // The lexer already reported the Invalid token (spec 13 §1): enter panic mode silently.
    if (check(TokenKind::Invalid)) { panic_ = true; return; }
    std::string found = atEnd() ? std::string("end of file") : std::format("'{}'", peek().text);
    error("QL2010", here(), std::string(what), found);
}

bool Parser::expectPunct(char c, std::string_view what) {
    if (matchPunct(c)) return true;
    std::string label = what.empty() ? std::format("'{}'", c) : std::string(what);
    if (c == ';' && pos_ > 0 && !check(TokenKind::Invalid)) {
        // A terminator missing before a line break, a '}' or the end of file leaves a complete
        // statement: report it at the end of that statement and carry on as if the ';' were there.
        const Token& prev = toks_[pos_ - 1];
        const bool closes = atEnd() || checkPunct('}');
        const bool lineBreak = peek().span.line > prev.span.endLine && detail::startsStatement(peek());
        if (closes || lineBreak) {
            SourceSpan at = prev.span;
            at.line = at.endLine; at.column = at.endColumn;
            const bool wasPanic = panic_;
            error("QL2010", std::move(at), label, atEnd() ? "end of file" : closes ? "'}'" : "end of line");
            panic_ = wasPanic;
            return false;
        }
    }
    expected(label);
    if (c != ')' && c != ']') return false;
    // Close-delimiter recovery: if the delimiter occurs later in this statement at bracket depth 0,
    // skip through it so the enclosing construct stays aligned; otherwise behave as if present.
    int depth = 0;
    for (std::size_t k = 0;; ++k) {
        const Token& t = peek(k);
        if (t.kind == TokenKind::Eof || t.kind == TokenKind::Pragma) return false;
        if (t.kind != TokenKind::Punct || t.text.size() != 1) continue;
        const char p = t.text[0];
        if (depth == 0 && (p == ';' || p == '{' || p == '}')) return false;
        if (p == '(' || p == '[') { ++depth; continue; }
        if (p != ')' && p != ']') continue;
        if (depth > 0) { --depth; continue; }
        if (p == c)
            for (std::size_t i = 0; i <= k; ++i) advance();
        return false;
    }
}
bool Parser::expectOp(std::string_view t) { if (matchOp(t)) return true; expected(std::format("'{}'", t)); return false; }
bool Parser::expectKw(std::string_view t) { if (matchText(t)) return true; expected(std::format("'{}'", t)); return false; }
std::optional<std::string> Parser::expectIdent(std::string_view what) {
    // Gate names, builtin function names and pulse keywords are legal identifiers in most
    // positions; the reserved constants pi/π, tau/τ, euler/ε never are.
    if (check(TokenKind::Identifier) || check(TokenKind::Gate) || check(TokenKind::CalBlock) ||
        (check(TokenKind::Builtin) && !detail::isBuiltinConstant(peek().text)))
        return advance().text;
    expected(what);
    return std::nullopt;
}
SourceSpan Parser::spanFrom(const SourceSpan& start) const {
    SourceSpan s = start;
    const Token& prev = pos_ > 0 ? toks_[pos_ - 1] : toks_[0];
    s.endLine = prev.span.endLine; s.endColumn = prev.span.endColumn;
    return s;
}

void Parser::synchronize() {
    int depth = 0; // ( [ { opened inside the skipped region
    while (!atEnd()) {
        const Token& t = peek();
        if (depth == 0) {
            if (t.kind == TokenKind::Pragma || checkPunct('}')) return;
            if (pos_ > 0 && t.span.line > toks_[pos_ - 1].span.endLine && detail::startsStatement(t)) return;
        }
        if (t.kind == TokenKind::Punct && t.text.size() == 1) {
            const char p = t.text[0];
            if (p == '(' || p == '[' || p == '{') ++depth;
            else if ((p == ')' || p == ']' || p == '}') && depth > 0) --depth;
            else if (p == ';' && depth == 0) { advance(); return; }
        }
        advance();
    }
}

Ast Parser::parseProgram() {
    Ast ast; ast.filename = filename_;
    // Header: the first statement must be OPENQASM (comments are already stripped). A header that
    // appears later is reported once, here, and not again as a duplicate.
    if (checkKw("OPENQASM")) {
        if (auto s = parseVersion()) ast.statements.push_back(std::move(s));
    } else {
        error("QL2001", here(), "");
    }
    while (!atEnd()) {
        panic_ = false;
        if (checkPunct('}')) { error("QL2011", here(), "}"); advance(); continue; }
        std::size_t before = pos_;
        StmtPtr s = parseStatement();
        if (s) ast.statements.push_back(std::move(s));
        if (pos_ == before && !atEnd()) advance(); // no progress: force one token
    }
    return ast;
}

StmtList Parser::parseBlock() {
    StmtList list;
    if (!expectPunct('{')) { synchronize(); return list; }
    ++depth_;
    while (!atEnd() && !checkPunct('}')) {
        std::size_t before = pos_;
        StmtPtr s = parseStatement();
        if (s) list.push_back(std::move(s));
        if (pos_ == before && !atEnd()) advance();
    }
    --depth_;
    panic_ = false; // the block's own closing brace belongs to the enclosing statement
    expectPunct('}');
    return list;
}
StmtList Parser::parseBlockOrStmt() {
    if (checkPunct('{')) return parseBlock();
    StmtList list;
    if (auto s = parseStatement()) list.push_back(std::move(s));
    return list;
}

} // namespace qlab::lang
