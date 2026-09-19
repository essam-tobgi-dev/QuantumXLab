#include "Lang/Parser.hpp"
#include "Lang/ParserUtil.hpp"
#include <format>

namespace qlab::lang {

StmtPtr Parser::parseVersion() {
    SourceSpan sp = advance().span; // OPENQASM
    VersionStmt v;
    if (check(TokenKind::Number)) {
        v.version = advance().text;
        if (v.version != "3.0" && v.version != "3")
            error("QL2003", sp, v.version);
    } else {
        expected("version number");
    }
    expectPunct(';');
    sawVersion_ = true;
    return mk(std::move(v), spanFrom(sp));
}

namespace {
// An unterminated string literal is the only Invalid token one code point longer than its text
// (the opening quote is not part of the text; spec 13 §1).
bool isUnterminatedString(const Token& t) {
    if (t.kind != TokenKind::Invalid || t.span.line != t.span.endLine)
        return false;
    std::uint32_t codePoints = 0;
    for (char c : t.text)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            ++codePoints;
    return t.span.endColumn - t.span.column == codePoints + 1;
}
} // namespace

StmtPtr Parser::parseInclude() {
    SourceSpan sp = advance().span;
    IncludeStmt inc;
    if (check(TokenKind::String)) {
        inc.path = advance().text;
        expectPunct(';');
    } else if (isUnterminatedString(peek())) {
        // QL1002 is already reported: the string ran to the end of its line and swallowed the ';'.
        // Recover the path so the gates it provides do not cascade into QL3010 errors.
        std::string path = advance().text;
        while (!path.empty() && (path.back() == ';' || path.back() == ' ' || path.back() == '\t' ||
                                 path.back() == '\r'))
            path.pop_back();
        inc.path = std::move(path);
        matchPunct(';');
    } else {
        expected("include path string");
        expectPunct(';');
    }
    return mk(std::move(inc), spanFrom(sp));
}

StmtPtr Parser::parsePragma() {
    const Token& t = advance();
    PragmaStmt p;
    p.raw = t.text;
    std::string s = t.text;
    // split on whitespace; first word is the name
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
    std::size_t j = i;
    while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j])))
        ++j;
    p.name = s.substr(i, j - i);
    p.isQlab = p.name.starts_with("qlab.");
    if (p.isQlab)
        p.name = p.name.substr(5);
    // args: tokens separated by whitespace, keeping {…} groups together
    std::string cur;
    int brace = 0;
    for (std::size_t k = j; k < s.size(); ++k) {
        char c = s[k];
        if (c == '{')
            ++brace;
        if (c == '}')
            --brace;
        if (std::isspace(static_cast<unsigned char>(c)) && brace == 0) {
            if (!cur.empty()) {
                p.args.push_back(cur);
                cur.clear();
            }
        } else
            cur += c;
    }
    if (!cur.empty())
        p.args.push_back(cur);
    return mk(std::move(p), t.span);
}

std::optional<ExprPtr> Parser::parseDesignator() {
    if (!checkPunct('['))
        return std::nullopt;
    advance();
    ExprPtr e = parseExprOrError();
    expectPunct(']');
    return e;
}

bool Parser::parseTypeSpec(TypeSpec& out) {
    if (!check(TokenKind::Type)) {
        expected("type");
        return false;
    }
    const std::string& n = peek().text;
    static const std::pair<const char*, BaseType> map[] = {
        {"bit", BaseType::Bit},           {"bool", BaseType::Bool},
        {"int", BaseType::Int},           {"uint", BaseType::Uint},
        {"float", BaseType::Float},       {"angle", BaseType::Angle},
        {"duration", BaseType::Duration}, {"stretch", BaseType::Stretch},
        {"complex", BaseType::Complex},   {"qubit", BaseType::Qubit}};
    bool found = false;
    for (auto& [k, v] : map)
        if (n == k) {
            out.base = v;
            found = true;
            break;
        }
    if (!found) {
        expected("type");
        return false;
    }
    advance();
    if (out.base == BaseType::Complex && checkPunct('[')) { // complex[float[64]]
        advance();
        TypeSpec inner;
        parseTypeSpec(inner);
        expectPunct(']');
        return true;
    }
    out.width = parseDesignator();
    return true;
}

StmtPtr Parser::parseQubitDecl() {
    SourceSpan sp = advance().span;
    QubitDecl d;
    d.size = parseDesignator();
    auto name = expectIdent("qubit name");
    d.name = name.value_or("?");
    expectPunct(';');
    if (inGateBody_)
        error("QL2012", sp, "qubit declaration");
    return mk(std::move(d), spanFrom(sp));
}

StmtPtr Parser::parseClassicalDecl(IoKind io) {
    SourceSpan sp = here();
    ClassicalDecl d;
    d.io = io;
    if (!parseTypeSpec(d.type)) {
        synchronize();
        return nullptr;
    }
    auto name = expectIdent("variable name");
    if (!name) {
        synchronize();
        return nullptr;
    }
    d.name = *name;
    if (matchOp("="))
        d.init = parseExprOrError();
    expectPunct(';');
    if (inGateBody_)
        error("QL2012", sp, "classical declaration");
    return mk(std::move(d), spanFrom(sp));
}

std::vector<std::string> Parser::parseIdList() {
    std::vector<std::string> ids;
    do {
        auto id = expectIdent();
        if (!id)
            break;
        ids.push_back(*id);
    } while (matchPunct(','));
    return ids;
}

StmtPtr Parser::parseGateDef() {
    SourceSpan sp = advance().span;
    GateDecl g;
    auto name = expectIdent("gate name");
    g.name = name.value_or("?");
    if (matchPunct('(')) {
        if (!checkPunct(')'))
            g.params = parseIdList();
        expectPunct(')');
    }
    g.qubits = parseIdList();
    bool saved = inGateBody_;
    inGateBody_ = true;
    g.body = parseBlock();
    inGateBody_ = saved;
    return mk(std::move(g), spanFrom(sp));
}

StmtPtr Parser::parseDef() {
    SourceSpan sp = advance().span;
    DefStmt d;
    auto name = expectIdent("subroutine name");
    d.name = name.value_or("?");
    if (expectPunct('(')) {
        while (!checkPunct(')') && !atEnd()) {
            DefParam p;
            if (checkText("qubit")) {
                advance();
                p.isQubit = true;
                p.type.base = BaseType::Qubit;
                p.qubitSize = parseDesignator();
            } else if (!parseTypeSpec(p.type)) {
                synchronize();
                return nullptr;
            }
            auto pn = expectIdent("parameter name");
            p.name = pn.value_or("?");
            d.params.push_back(std::move(p));
            if (!matchPunct(','))
                break;
        }
        expectPunct(')');
    }
    if (matchOp("->"))
        parseTypeSpec(d.returnType);
    d.body = parseBlock();
    return mk(std::move(d), spanFrom(sp));
}

StmtPtr Parser::parseExtern() {
    SourceSpan sp = advance().span;
    ExternStmt e;
    if (checkText("port")) { // OpenPulse `extern port` outside cal: still an extern
        advance();
        auto n = expectIdent("port name");
        e.name = n.value_or("port");
        expectPunct(';');
        return mk(std::move(e), spanFrom(sp));
    }
    auto name = expectIdent("extern name");
    e.name = name.value_or("?");
    // The signature is not modelled (Sema rejects extern with QL3100): skip it to the terminator,
    // stopping at the next statement if the ';' is missing.
    synchronize();
    if (!detail::isPunct(toks_[pos_ > 0 ? pos_ - 1 : 0], ';'))
        expectPunct(';');
    return mk(std::move(e), spanFrom(sp));
}

} // namespace qlab::lang
