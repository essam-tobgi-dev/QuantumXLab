#pragma once
// Internal helpers shared by the parser translation units. Not part of the public Lang API
// (Lang.hpp does not include this header).
#include "Lang/Ast.hpp"
#include "Lang/Diagnostics.hpp"
#include "Lang/Token.hpp"
#include <string_view>
#include <vector>

namespace qlab::lang::detail {

// Deeper input is diagnosed (QL2015) instead of exhausting the stack: the editor re-parses every
// buffer on a JobSystem worker (spec 14 §11), and malformed text must never crash it.
// kMaxNesting bounds parser recursion (nested statements, unary chains, parenthesised operands);
// kMaxDepth bounds the depth of the expression trees that Sema, dumps and destructors recurse on.
inline constexpr int kMaxNesting = 128;
inline constexpr int kMaxDepth = 256;

struct NestingGuard {
    int& depth;
    explicit NestingGuard(int& d) : depth(d) { ++depth; }
    ~NestingGuard() { --depth; }
    NestingGuard(const NestingGuard&) = delete;
    NestingGuard& operator=(const NestingGuard&) = delete;
};

inline bool isPunct(const Token& t, char c) {
    return t.kind == TokenKind::Punct && t.text.size() == 1 && t.text[0] == c;
}

// pi/π, tau/τ, euler/ε: reserved constants, never identifiers (spec 13 §1–§2.1).
inline bool isBuiltinConstant(std::string_view s) {
    return s == "pi" || s == "tau" || s == "euler" || s == "\xCF\x80" || s == "\xCF\x84" ||
           s == "\xCE\xB5";
}
inline std::string_view canonicalConstant(std::string_view s) {
    if (s == "\xCF\x80")
        return "pi";
    if (s == "\xCF\x84")
        return "tau";
    if (s == "\xCE\xB5")
        return "euler";
    return s;
}

// Tokens a statement can begin with. Recovery stops before such a token when it starts a later
// line, because statements are conventionally one per line and the ';' may have been lost.
inline bool startsStatement(const Token& t) {
    switch (t.kind) {
    case TokenKind::Identifier:
    case TokenKind::Type:
    case TokenKind::Gate:
    case TokenKind::CalBlock:
    case TokenKind::Pragma:
        return true;
    case TokenKind::Builtin:
        return !isBuiltinConstant(t.text);
    case TokenKind::Keyword:
        return t.text != "in" && t.text != "else" && t.text != "case" && t.text != "default";
    default:
        return false;
    }
}

// Nesting overflow is reported once while the parser unwinds out of the too-deep region, even
// though every nested statement resets panic mode.
inline bool lastDiagnosticIs(const std::vector<Diagnostic>& diags, std::string_view id) {
    return !diags.empty() && diags.back().id() == id;
}

// Depth of an expression tree, or a value above `cap` once it exceeds `cap` (recursion <= cap).
int exprDepth(const Expr& e, int cap);

} // namespace qlab::lang::detail
