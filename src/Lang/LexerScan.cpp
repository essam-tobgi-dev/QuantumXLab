#include "Lang/Lexer.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <optional>

// Spec 13 §1 — the scanner. A lexical error never aborts lexing: the malformed lexeme becomes one
// `Invalid` token carrying the QL1xxx diagnostic, and scanning resumes right after it so the
// closing bracket or `;` that follows stays available to the parser's recovery.
namespace qlab::lang {

namespace {

struct Cursor {
    std::string_view src;
    std::size_t pos = 0;
    std::uint32_t line = 1, col = 1;
    char peek(std::size_t k = 0) const { return pos + k < src.size() ? src[pos + k] : '\0'; }
    bool eof() const { return pos >= src.size(); }
    // Columns count Unicode code points (spec 13 §1): UTF-8 continuation bytes do not advance.
    char advance() {
        char c = src[pos++];
        if (c == '\n') {
            ++line;
            col = 1;
        } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            ++col;
        return c;
    }
};

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}
bool isNonAscii(char c) {
    return static_cast<unsigned char>(c) >= 0x80;
}
bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || isNonAscii(c);
}
bool isIdentChar(char c) {
    return isIdentStart(c) || isDigit(c);
}
bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::optional<DurationUnit> unitOf(std::string_view s) {
    if (s == "ns")
        return DurationUnit::Ns;
    if (s == "us" || s == "\xC2\xB5s" || s == "\xCE\xBCs")
        return DurationUnit::Us; // us, µs, μs
    if (s == "ms")
        return DurationUnit::Ms;
    if (s == "s")
        return DurationUnit::S;
    if (s == "dt")
        return DurationUnit::Dt;
    return std::nullopt;
}

// First UTF-8 encoded code point of `s` that is not ASCII (for the QL1001 message).
std::string firstNonAscii(std::string_view s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (!isNonAscii(s[i]))
            continue;
        std::size_t j = i + 1;
        while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80)
            ++j;
        return std::string(s.substr(i, j - i));
    }
    return std::string(s);
}

TokenKind classifyWord(std::string_view id) {
    if (isTypeKeyword(id))
        return TokenKind::Type;
    if (isKeyword(id))
        return TokenKind::Keyword;
    if (id == "U" || id == "gphase" || id == "CX")
        return TokenKind::Gate;
    if (isBuiltinName(id))
        return TokenKind::Builtin;
    if (isPulseKeyword(id))
        return TokenKind::CalBlock;
    return TokenKind::Identifier;
}

class Scanner {
  public:
    Scanner(std::string_view text, const LexOptions& opts) : text_(text), c_{text}, opts_(opts) {}

    LexResult run() {
        while (!c_.eof()) {
            char ch = c_.peek();
            if (isSpace(ch)) {
                c_.advance();
                continue;
            }
            start_ = c_.pos;
            line_ = c_.line;
            col_ = c_.col;
            if (ch == '/' && c_.peek(1) == '/')
                lineComment();
            else if (ch == '/' && c_.peek(1) == '*')
                blockComment();
            else if (text_.substr(c_.pos).starts_with("pragma") && !isIdentChar(c_.peek(6)))
                pragma();
            else if (ch == '"' || ch == '\'')
                stringLiteral();
            else if (ch == '$' && isDigit(c_.peek(1)))
                physicalQubit();
            else if (isDigit(ch) || (ch == '.' && isDigit(c_.peek(1))))
                number();
            else if (isIdentStart(ch))
                word();
            else
                symbol();
        }
        start_ = c_.pos;
        line_ = c_.line;
        col_ = c_.col;
        Token eof;
        eof.kind = TokenKind::Eof;
        eof.span = span();
        out_.tokens.push_back(std::move(eof));
        return std::move(out_);
    }

  private:
    SourceSpan span() const {
        SourceSpan s;
        s.line = line_;
        s.column = col_;
        s.endLine = c_.line;
        s.endColumn = c_.col;
        s.file = opts_.filename;
        return s;
    }
    std::string lexeme() const { return std::string(text_.substr(start_, c_.pos - start_)); }
    void report(std::string_view id, const std::string& arg) {
        out_.diagnostics.push_back(Diagnostics::make(id, span(), arg));
    }
    void push(Token t) {
        t.span = span();
        out_.tokens.push_back(std::move(t));
    }
    void comment() {
        if (!opts_.keepComments)
            return;
        Token t;
        t.kind = TokenKind::Comment;
        t.text = lexeme();
        push(std::move(t));
    }

    void lineComment() {
        while (!c_.eof() && c_.peek() != '\n')
            c_.advance();
        comment();
    }
    void blockComment() {
        c_.advance();
        c_.advance();
        bool closed = false;
        while (!c_.eof()) {
            if (c_.peek() == '*' && c_.peek(1) == '/') {
                c_.advance();
                c_.advance();
                closed = true;
                break;
            }
            c_.advance();
        }
        if (!closed)
            report("QL1002", "block comment");
        comment();
    }
    // `pragma` swallows the rest of the line; the token text is what follows the word (spec 13 §7).
    void pragma() {
        for (int i = 0; i < 6; ++i)
            c_.advance();
        while (!c_.eof() && (c_.peek() == ' ' || c_.peek() == '\t'))
            c_.advance();
        std::size_t ps = c_.pos;
        while (!c_.eof() && c_.peek() != '\n')
            c_.advance();
        Token t;
        t.kind = TokenKind::Pragma;
        t.text = std::string(text_.substr(ps, c_.pos - ps));
        while (!t.text.empty() && (t.text.back() == ';' || isSpace(t.text.back())))
            t.text.pop_back();
        push(std::move(t));
    }
    // An unterminated string ends at the end of its line and becomes an Invalid token whose text is
    // the content read so far (the parser uses it to recover an `include` path).
    void stringLiteral() {
        char quote = c_.advance();
        std::string s;
        bool closed = false;
        while (!c_.eof() && c_.peek() != '\n') {
            char d = c_.advance();
            if (d == quote) {
                closed = true;
                break;
            }
            s += d;
        }
        Token t;
        t.text = std::move(s);
        if (!closed) {
            t.kind = TokenKind::Invalid;
            report("QL1002", "string literal");
        } else {
            bool bits = !t.text.empty();
            for (char d : t.text)
                if (d != '0' && d != '1' && d != '_')
                    bits = false;
            t.kind = bits ? TokenKind::BitString : TokenKind::String;
        }
        push(std::move(t));
    }
    void physicalQubit() {
        c_.advance(); // '$'
        while (isIdentChar(c_.peek()))
            c_.advance();
        Token t;
        t.text = lexeme();
        t.kind = TokenKind::PhysicalQubit;
        std::string_view digits = std::string_view(t.text).substr(1);
        std::uint32_t v = 0;
        auto r = std::from_chars(digits.data(), digits.data() + digits.size(), v);
        if (r.ec != std::errc{} || r.ptr != digits.data() + digits.size()) {
            t.kind = TokenKind::Invalid;
            report("QL1003", t.text);
        } else
            t.ival = v;
        push(std::move(t));
    }
    void number();
    void radixNumber();
    void word();
    void symbol();

    std::string_view text_;
    Cursor c_;
    const LexOptions& opts_;
    LexResult out_;
    std::size_t start_ = 0;
    std::uint32_t line_ = 1, col_ = 1;
};

// Decimal integer, float, imaginary and duration literals. Any other identifier characters glued to
// the literal make the whole lexeme malformed: QL1004 when the suffix looks like a time unit
// (ends in 's', e.g. `10ks`), QL1003 otherwise (e.g. `2pi`, `1e`).
void Scanner::number() {
    std::string digits;
    bool isFloat = false;
    auto take = [&] {
        while (isDigit(c_.peek()) || c_.peek() == '_') {
            char d = c_.advance();
            if (d != '_')
                digits += d;
        }
    };
    if (c_.peek() == '0' && std::string_view("xXbBoO").find(c_.peek(1)) != std::string_view::npos)
        return radixNumber();
    take();
    if (c_.peek() == '.' && isDigit(c_.peek(1))) {
        isFloat = true;
        digits += c_.advance();
        take();
    } else if (c_.peek() == '.' && !isIdentStart(c_.peek(1))) {
        isFloat = true;
        c_.advance();
        digits += ".0";
    }
    if ((c_.peek() == 'e' || c_.peek() == 'E') &&
        (isDigit(c_.peek(1)) ||
         ((c_.peek(1) == '+' || c_.peek(1) == '-') && isDigit(c_.peek(2))))) {
        isFloat = true;
        digits += c_.advance();
        if (c_.peek() == '+' || c_.peek() == '-')
            digits += c_.advance();
        while (isDigit(c_.peek()))
            digits += c_.advance();
    }
    std::size_t sufStart = c_.pos;
    while (isIdentChar(c_.peek()))
        c_.advance();
    std::string_view suffix = text_.substr(sufStart, c_.pos - sufStart);

    Token t;
    t.text = lexeme();
    t.kind = TokenKind::Number;
    t.isFloat = isFloat;
    std::string_view err;
    std::string arg = t.text;
    t.fval = std::strtod(digits.c_str(), nullptr);
    if (!std::isfinite(t.fval))
        err = "QL1003";
    if (!isFloat) {
        auto r = std::from_chars(digits.data(), digits.data() + digits.size(), t.ival);
        if (r.ec != std::errc{} || r.ptr != digits.data() + digits.size())
            err = "QL1003";
    }
    if (suffix.empty()) {
    } else if (suffix == "im") {
        t.isImaginary = true;
    } else if (auto unit = unitOf(suffix)) {
        t.kind = TokenKind::Duration;
        t.unit = *unit;
    } else if (suffix.back() == 's') {
        err = "QL1004";
        arg = std::string(suffix);
    } else {
        err = "QL1003";
    }
    if (!err.empty()) {
        t.kind = TokenKind::Invalid;
        report(err, arg);
    }
    push(std::move(t));
}

// 0x…, 0o…, 0b… integers; values up to 2^64 - 1 are kept as their two's-complement int64 image.
void Scanner::radixNumber() {
    c_.advance();
    char base = c_.advance();
    std::string digits;
    while (isIdentChar(c_.peek())) {
        char d = c_.advance();
        if (d != '_')
            digits += d;
    }
    int radix = (base == 'x' || base == 'X') ? 16 : (base == 'b' || base == 'B') ? 2 : 8;
    Token t;
    t.text = lexeme();
    t.kind = TokenKind::Number;
    std::uint64_t v = 0;
    auto r = std::from_chars(digits.data(), digits.data() + digits.size(), v, radix);
    if (digits.empty() || r.ec != std::errc{} || r.ptr != digits.data() + digits.size()) {
        t.kind = TokenKind::Invalid;
        report("QL1003", t.text);
    } else {
        t.ival = static_cast<std::int64_t>(v);
        t.fval = static_cast<double>(v);
    }
    push(std::move(t));
}

// Identifiers are ASCII (spec 13 §1); the only non-ASCII words are the constants π, τ and ε.
void Scanner::word() {
    while (isIdentChar(c_.peek()))
        c_.advance();
    Token t;
    t.text = lexeme();
    bool ascii = true;
    for (char ch : t.text)
        if (isNonAscii(ch))
            ascii = false;
    if (!ascii && !isBuiltinName(t.text)) {
        t.kind = TokenKind::Invalid;
        report("QL1001", firstNonAscii(t.text));
    } else {
        t.kind = classifyWord(t.text);
    }
    push(std::move(t));
}

void Scanner::symbol() {
    static constexpr std::array<std::string_view, 30> ops = {
        "<<=", ">>=", "**=", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "**", "++", "+=", "-=",
        "*=",  "/=",  "%=",  "&=", "|=", "^=", "->", "+",  "-",  "*",  "/",  "%",  "&",  "|",  "^"};
    for (auto o : ops) {
        if (!text_.substr(c_.pos).starts_with(o))
            continue;
        for (std::size_t i = 0; i < o.size(); ++i)
            c_.advance();
        Token t;
        t.kind = TokenKind::Operator;
        t.text = std::string(o);
        push(std::move(t));
        return;
    }
    char ch = c_.advance();
    Token t;
    t.text = std::string(1, ch);
    if (std::string_view("~!<>=").find(ch) != std::string_view::npos)
        t.kind = TokenKind::Operator;
    else if (std::string_view("(){}[];,:@.?").find(ch) != std::string_view::npos)
        t.kind = TokenKind::Punct;
    else {
        t.kind = TokenKind::Invalid;
        report("QL1001", t.text);
    }
    push(std::move(t));
}

} // namespace

LexResult lex(std::string_view text, const LexOptions& opts) {
    return Scanner(text, opts).run();
}

} // namespace qlab::lang
