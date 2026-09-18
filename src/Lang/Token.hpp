#pragma once
// Spec 13 §1 / §11 — token stream shared by the parser and the editor highlighter.
#include "Core/Error.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qlab::lang {

enum class TokenKind {
    Keyword,       // reserved words (OPENQASM include qubit gate if ...)
    Type,          // int uint float angle bool bit duration stretch complex qubit
    Gate,          // U gphase and stdgates names
    Builtin,       // pi euler tau sin cos ... casts
    Identifier,
    PhysicalQubit, // $k
    Number,        // integer / float / imaginary literal
    Duration,      // 100ns, 2.5us, 4dt
    String,        // "stdgates.inc"
    BitString,     // "0101"
    Comment,
    Pragma,        // whole `pragma ...` line
    CalBlock,      // OpenPulse contextual keyword inside cal/defcal
    Operator,
    Punct,
    Invalid,
    Eof
};

const char* tokenKindName(TokenKind k);

enum class DurationUnit { None, Ns, Us, Ms, S, Dt };

struct Token {
    TokenKind kind = TokenKind::Invalid;
    std::string text;      // exact source text (for Pragma: the text after `pragma`)
    SourceSpan span;
    // Parsed literal payload
    bool isFloat = false;
    bool isImaginary = false;
    std::int64_t ival = 0;
    double fval = 0.0;
    DurationUnit unit = DurationUnit::None;

    bool is(TokenKind k) const { return kind == k; }
    bool is(TokenKind k, std::string_view t) const { return kind == k && text == t; }
    bool isText(std::string_view t) const { return text == t && kind != TokenKind::String && kind != TokenKind::BitString; }
};

// Converts a duration literal to picoseconds (dt durations are not physical; see Sema).
std::int64_t durationToPs(double value, DurationUnit unit);
const char* durationUnitName(DurationUnit u);

} // namespace qlab::lang
