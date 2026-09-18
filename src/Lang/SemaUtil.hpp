#pragma once
// Internal helpers shared by the Sema translation units. Not part of the public Lang API
// (Lang.hpp does not include this header).
#include "Lang/Ast.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace qlab::lang::detail {

// True when the expression holds a parser ErrorExpr placeholder: the syntax error is already
// reported, so checks that depend on the value (QL3040, QL3090, …) stay silent (spec 13 §8).
bool containsErrorExpr(const Expr& e);

// Source-like rendering of an operand or small expression for diagnostic messages: `q[0]`, `$3`,
// `c[0:2]`. Falls back to the S-expression dump for exotic nodes.
std::string exprText(const Expr& e);

// Statement kind as a user reads it ("if", "measure", "classical declaration", …).
const char* stmtKindName(const Stmt& s);

// Checked conversions for constant folding: C++ leaves signed overflow and out-of-range
// float-to-integer conversions undefined, so folding goes through these (two's complement wrap).
inline std::int64_t wrapAdd(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}
inline std::int64_t wrapSub(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b));
}
inline std::int64_t wrapMul(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}
// Truncation toward zero, saturating at the int64 range; NaN maps to 0.
std::int64_t saturatingToInt(double v);

} // namespace qlab::lang::detail
