#pragma once
// Tokenizer for the LaTeX subset (spec 20 §6).
#include "UI/Math/MathAst.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace qlab::ui::math {

enum class TokKind : std::uint8_t {
    Command, // \name or \<single non-letter>  (text = name without backslash)
    Char,    // a single UTF-8 character (text)
    LBrace,
    RBrace,
    Caret,
    Underscore,
    Amp,
    NewRow, // braces, ^, _, & and the row break
    End
};

struct MathToken {
    TokKind kind = TokKind::End;
    std::string text;
    SourceRange src;
};

std::vector<MathToken> tokenizeMath(std::string_view latex);

// UTF-8 helpers shared by parser/layout.
std::size_t utf8SeqLen(unsigned char lead);
char32_t utf8Decode(std::string_view s, std::size_t& i);
std::string utf8Encode(char32_t cp);

} // namespace qlab::ui::math
