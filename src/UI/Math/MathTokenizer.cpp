#include "UI/Math/MathTokenizer.hpp"
#include <cctype>

namespace qlab::ui::math {

std::size_t utf8SeqLen(unsigned char lead) {
    if (lead < 0x80)
        return 1;
    if ((lead >> 5) == 0x6)
        return 2;
    if ((lead >> 4) == 0xE)
        return 3;
    if ((lead >> 3) == 0x1E)
        return 4;
    return 1;
}
char32_t utf8Decode(std::string_view s, std::size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t n = utf8SeqLen(c);
    if (i + n > s.size()) {
        ++i;
        return c;
    }
    char32_t cp;
    if (n == 1)
        cp = c;
    else if (n == 2)
        cp = c & 0x1F;
    else if (n == 3)
        cp = c & 0x0F;
    else
        cp = c & 0x07;
    for (std::size_t k = 1; k < n; ++k)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += n;
    return cp;
}
std::string utf8Encode(char32_t cp) {
    std::string s;
    if (cp < 0x80)
        s += static_cast<char>(cp);
    else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

std::vector<MathToken> tokenizeMath(std::string_view s) {
    std::vector<MathToken> out;
    std::size_t i = 0;
    auto push = [&](TokKind k, std::string text, std::size_t b, std::size_t e) {
        out.push_back(
            {k, std::move(text), {static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(e)}});
    };
    while (i < s.size()) {
        char c = s[i];
        if (c == '%') { // comment to end of line
            while (i < s.size() && s[i] != '\n')
                ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        std::size_t b = i;
        if (c == '\\') {
            if (i + 1 >= s.size()) {
                push(TokKind::Char, "\\", b, i + 1);
                ++i;
                continue;
            }
            char n = s[i + 1];
            if (n == '\\') {
                push(TokKind::NewRow, "\\\\", b, i + 2);
                i += 2;
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(n))) {
                std::size_t j = i + 1;
                while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j])))
                    ++j;
                push(TokKind::Command, std::string(s.substr(i + 1, j - i - 1)), b, j);
                i = j;
                // \operatorname* etc: swallow a trailing '*'
                if (i < s.size() && s[i] == '*' && out.back().text == "operatorname") {
                    ++i;
                }
                continue;
            }
            // single non-letter command: \, \; \{ \} \| \  etc. (may be multi-byte)
            std::size_t j = i + 1;
            utf8Decode(s, j);
            push(TokKind::Command, std::string(s.substr(i + 1, j - i - 1)), b, j);
            i = j;
            continue;
        }
        switch (c) {
        case '{':
            push(TokKind::LBrace, "{", b, i + 1);
            ++i;
            continue;
        case '}':
            push(TokKind::RBrace, "}", b, i + 1);
            ++i;
            continue;
        case '^':
            push(TokKind::Caret, "^", b, i + 1);
            ++i;
            continue;
        case '_':
            push(TokKind::Underscore, "_", b, i + 1);
            ++i;
            continue;
        case '&':
            push(TokKind::Amp, "&", b, i + 1);
            ++i;
            continue;
        default:
            break;
        }
        std::size_t j = i;
        utf8Decode(s, j);
        push(TokKind::Char, std::string(s.substr(i, j - i)), b, j);
        i = j;
    }
    push(TokKind::End, "", s.size(), s.size());
    return out;
}

} // namespace qlab::ui::math
